#include "OutlierUpgradeEffectToolWidget.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "EditorFramework/AssetImportData.h"
#include "EditorReimportHandler.h"
#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "GameplayEffect.h"
#include "GameplayTagsManager.h"
#include "Containers/StringConv.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"
#include "GAS/Data/OutlierShooterSuitAbilityDataRow.h"
#include "Upgrade/OutlierUpgradeTypes.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "OutlierUpgradeEffectTool"

namespace OutlierUpgradeEffectTool
{
	// EffectType <-> 친화 라벨 / enum 문자열
	const TArray<EOutlierUpgradeEffectType>& AllEffectTypes()
	{
		static const TArray<EOutlierUpgradeEffectType> Types = {
			EOutlierUpgradeEffectType::Attribute,
			EOutlierUpgradeEffectType::GrantAbility,
			EOutlierUpgradeEffectType::AbilityConfig,
			EOutlierUpgradeEffectType::FunctionOverride,
			EOutlierUpgradeEffectType::ApplyEffect
		};
		return Types;
	}

	FString EffectTypeLabel(EOutlierUpgradeEffectType Type)
	{
		switch (Type)
		{
		case EOutlierUpgradeEffectType::Attribute:        return TEXT("캐릭터 스텟 (Attribute)");
		case EOutlierUpgradeEffectType::GrantAbility:     return TEXT("능력 해금 (GrantAbility)");
		case EOutlierUpgradeEffectType::AbilityConfig:    return TEXT("스킬 스텟 (AbilityConfig)");
		case EOutlierUpgradeEffectType::FunctionOverride: return TEXT("능력 변환 (FunctionOverride)");
		case EOutlierUpgradeEffectType::ApplyEffect:      return TEXT("능력 시 추가효과 (ApplyEffect)");
		default:                                          return TEXT("Unknown");
		}
	}

	FString EffectTypeEnumString(EOutlierUpgradeEffectType Type)
	{
		switch (Type)
		{
		case EOutlierUpgradeEffectType::Attribute:        return TEXT("EOutlierUpgradeEffectType::Attribute");
		case EOutlierUpgradeEffectType::GrantAbility:     return TEXT("EOutlierUpgradeEffectType::GrantAbility");
		case EOutlierUpgradeEffectType::AbilityConfig:    return TEXT("EOutlierUpgradeEffectType::AbilityConfig");
		case EOutlierUpgradeEffectType::FunctionOverride: return TEXT("EOutlierUpgradeEffectType::FunctionOverride");
		case EOutlierUpgradeEffectType::ApplyEffect:      return TEXT("EOutlierUpgradeEffectType::ApplyEffect");
		default:                                          return TEXT("EOutlierUpgradeEffectType::Attribute");
		}
	}

	EOutlierUpgradeEffectType EffectTypeFromLabel(const FString& Label)
	{
		for (EOutlierUpgradeEffectType Type : AllEffectTypes())
		{
			if (EffectTypeLabel(Type) == Label)
			{
				return Type;
			}
		}
		return EOutlierUpgradeEffectType::Attribute;
	}

	FString OpLabel(EOutlierUpgradeModOp Op)
	{
		switch (Op)
		{
		case EOutlierUpgradeModOp::Additive:       return TEXT("더하기 (Additive)");
		case EOutlierUpgradeModOp::Multiplicative: return TEXT("곱하기 (Multiplicative)");
		case EOutlierUpgradeModOp::Override:       return TEXT("덮어쓰기 (Override)");
		default:                                   return TEXT("Additive");
		}
	}

	FString OpEnumString(EOutlierUpgradeModOp Op)
	{
		switch (Op)
		{
		case EOutlierUpgradeModOp::Additive:       return TEXT("EOutlierUpgradeModOp::Additive");
		case EOutlierUpgradeModOp::Multiplicative: return TEXT("EOutlierUpgradeModOp::Multiplicative");
		case EOutlierUpgradeModOp::Override:       return TEXT("EOutlierUpgradeModOp::Override");
		default:                                   return TEXT("EOutlierUpgradeModOp::Additive");
		}
	}

	EOutlierUpgradeModOp OpFromLabel(const FString& Label)
	{
		if (Label == OpLabel(EOutlierUpgradeModOp::Multiplicative)) return EOutlierUpgradeModOp::Multiplicative;
		if (Label == OpLabel(EOutlierUpgradeModOp::Override))       return EOutlierUpgradeModOp::Override;
		return EOutlierUpgradeModOp::Additive;
	}

	// EffectType 별 TargetTag 네임스페이스 프리픽스
	FString TargetTagPrefix(EOutlierUpgradeEffectType Type)
	{
		switch (Type)
		{
		case EOutlierUpgradeEffectType::Attribute:        return TEXT("Attribute");
		case EOutlierUpgradeEffectType::GrantAbility:     return TEXT("Ability.Shooter");
		case EOutlierUpgradeEffectType::AbilityConfig:    return TEXT("Ability.Shooter");
		case EOutlierUpgradeEffectType::FunctionOverride: return TEXT("Upgrade.Shooter");
		case EOutlierUpgradeEffectType::ApplyEffect:      return TEXT("State");
		default:                                          return FString();
		}
	}

	void GatherTagsByPrefix(const FString& Prefix, TArray<TSharedPtr<FName>>& Out)
	{
		Out.Reset();
		FGameplayTagContainer All;
		UGameplayTagsManager::Get().RequestAllGameplayTags(All, false);

		TArray<FName> Names;
		for (const FGameplayTag& Tag : All)
		{
			const FString S = Tag.ToString();
			if (Prefix.IsEmpty() || S.StartsWith(Prefix))
			{
				Names.Add(Tag.GetTagName());
			}
		}
		Names.Sort(FNameLexicalLess());
		for (const FName& N : Names)
		{
			Out.Add(MakeShared<FName>(N));
		}
	}

	// FOutlierShooterSuitAbilityDataRow 의 float 필드 이름들 ( 값은 몰라도 됨, 리플렉션 )
	void GatherConfigFieldNames(TArray<TSharedPtr<FName>>& Out)
	{
		Out.Reset();
		const UScriptStruct* Struct = FOutlierShooterSuitAbilityDataRow::StaticStruct();
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			if (It->IsA(FFloatProperty::StaticClass()))
			{
				Out.Add(MakeShared<FName>(It->GetFName()));
			}
		}
	}

	// /Game/GAS/Effects/Upgrade 아래 GameplayEffect Blueprint 들 -> key( "GE_" 접두 제거 )
	void GatherEffectClassKeys(TArray<TSharedPtr<FName>>& Out)
	{
		Out.Reset();
		IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

		FARFilter Filter;
		Filter.PackagePaths.Add(TEXT("/Game/Blueprint/Gas"));   // GE BP 실제 위치 ( Upgrade 하위 포함 )
		Filter.bRecursivePaths = true;
		Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());

		TArray<FAssetData> Assets;
		AR.GetAssets(Filter, Assets);

		TArray<FName> Keys;
		for (const FAssetData& AssetData : Assets)
		{
			const UBlueprint* BP = Cast<UBlueprint>(AssetData.GetAsset());
			if (!BP || !BP->GeneratedClass || !BP->GeneratedClass->IsChildOf(UGameplayEffect::StaticClass()))
			{
				continue;
			}
			FString KeyStr = AssetData.AssetName.ToString();
			KeyStr.RemoveFromStart(TEXT("GE_"));
			Keys.AddUnique(FName(*KeyStr));
		}
		Keys.Sort(FNameLexicalLess());
		for (const FName& K : Keys)
		{
			Out.Add(MakeShared<FName>(K));
		}
	}

	// 지정 row struct 를 쓰는 DataTable 애셋들 수집
	void GatherDataTablesByRowStruct(const UScriptStruct* RowStruct, TArray<TWeakObjectPtr<UDataTable>>& Out)
	{
		Out.Reset();
		IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

		TArray<FAssetData> Assets;
		AR.GetAssetsByClass(UDataTable::StaticClass()->GetClassPathName(), Assets);
		for (const FAssetData& AssetData : Assets)
		{
			UDataTable* Table = Cast<UDataTable>(AssetData.GetAsset());
			if (Table && Table->GetRowStruct() == RowStruct)
			{
				Out.Add(Table);
			}
		}
	}

	// CSV 필드 wrapping: gameplay tag -> "(TagName=""...."")"
	FString WrapTag(const FGameplayTag& Tag)
	{
		if (!Tag.IsValid())
		{
			return FString();
		}
		return FString::Printf(TEXT("\"(TagName=\"\"%s\"\")\""), *Tag.ToString());
	}

	FString NameOrEmpty(const FName& N)
	{
		return N.IsNone() ? FString() : N.ToString();
	}
}

using namespace OutlierUpgradeEffectTool;

namespace
{
	using FEffectRowItem = TSharedPtr<FOutlierUpgradeEffectToolRow>;

	// 다중 컬럼 행 ( DataTable 처럼 표로 보이게 )
	class SEffectTableRow : public SMultiColumnTableRow<FEffectRowItem>
	{
	public:
		SLATE_BEGIN_ARGS(SEffectTableRow) {}
			SLATE_ARGUMENT(FEffectRowItem, Item)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
		{
			Item = InArgs._Item;
			SMultiColumnTableRow<FEffectRowItem>::Construct(FSuperRowType::FArguments(), OwnerTable);
		}

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
		{
			if (!Item.IsValid())
			{
				return SNullWidget::NullWidget;
			}

			FString Text;
			if (ColumnName == TEXT("Name"))        { Text = Item->Name.ToString(); }
			else if (ColumnName == TEXT("Node"))   { Text = Item->NodeRowName.ToString(); }
			else if (ColumnName == TEXT("Type"))   { Text = EffectTypeLabel(Item->EffectType); }
			else if (ColumnName == TEXT("Target")) { Text = Item->TargetTag.ToString(); }
			else if (ColumnName == TEXT("Field"))  { Text = NameOrEmpty(Item->ConfigField); }
			else if (ColumnName == TEXT("Op"))     { Text = OpLabel(Item->Op); }
			else if (ColumnName == TEXT("Mag"))    { Text = FString::SanitizeFloat(Item->Magnitude); }
			else if (ColumnName == TEXT("Key"))    { Text = NameOrEmpty(Item->EffectClassKey); }

			return SNew(SBox).Padding(4, 2).VAlign(VAlign_Center)
				[ SNew(STextBlock).Text(FText::FromString(Text)) ];
		}

	private:
		FEffectRowItem Item;
	};
}

void SOutlierUpgradeEffectToolWidget::Construct(const FArguments& InArgs)
{
	// 고정 옵션(EffectType / Op)
	for (EOutlierUpgradeEffectType Type : AllEffectTypes())
	{
		EffectTypeLabelOptions.Add(MakeShared<FString>(EffectTypeLabel(Type)));
	}
	OpLabelOptions.Add(MakeShared<FString>(OpLabel(EOutlierUpgradeModOp::Additive)));
	OpLabelOptions.Add(MakeShared<FString>(OpLabel(EOutlierUpgradeModOp::Multiplicative)));
	OpLabelOptions.Add(MakeShared<FString>(OpLabel(EOutlierUpgradeModOp::Override)));

	GatherConfigFieldNames(ConfigFieldOptions);
	RefreshEffectClassKeyOptions();
	RefreshDataTableChoices();
	RefreshTargetTagOptions();

	auto MakeNameCombo = [this](TArray<FNamePtr>& Options, TFunction<void(FName)> OnPick, TFunction<FText()> GetText, TFunction<bool()> IsEnabled, TSharedPtr<SComboBox<FNamePtr>>& OutRef) -> TSharedRef<SWidget>
	{
		TSharedRef<SComboBox<FNamePtr>> Combo = SNew(SComboBox<FNamePtr>)
			.OptionsSource(&Options)
			.IsEnabled_Lambda([IsEnabled]() { return IsEnabled(); })
			.OnGenerateWidget_Lambda([](FNamePtr Item) { return SNew(STextBlock).Text(FText::FromName(Item.IsValid() ? *Item : NAME_None)); })
			.OnSelectionChanged_Lambda([OnPick](FNamePtr Item, ESelectInfo::Type) { if (Item.IsValid()) OnPick(*Item); })
			[
				SNew(STextBlock).Text_Lambda([GetText]() { return GetText(); })
			];
		OutRef = Combo;
		return Combo;
	};

	ChildSlot
	[
		SNew(SVerticalBox)

		// ── 테이블 선택 ──
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2)
			[ SNew(STextBlock).Text(LOCTEXT("EffectTable", "Effect DataTable:")) ]
			+ SHorizontalBox::Slot().FillWidth(1).Padding(2)
			[
				SNew(SComboBox<TSharedPtr<FString>>)
				.OptionsSource(&EffectTableChoiceLabels)
				.OnGenerateWidget_Lambda([](TSharedPtr<FString> S) { return SNew(STextBlock).Text(FText::FromString(S.IsValid() ? *S : FString())); })
				.OnSelectionChanged_Lambda([this](TSharedPtr<FString> S, ESelectInfo::Type)
				{
					const int32 Idx = EffectTableChoiceLabels.IndexOfByKey(S);
					if (EffectTableChoices.IsValidIndex(Idx))
					{
						EffectTable = EffectTableChoices[Idx];
						ReloadRowsFromEffectTable();
					}
				})
				[ SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(EffectTable.IsValid() ? EffectTable->GetName() : TEXT("선택...")); }) ]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 2, 2, 2)
			[ SNew(STextBlock).Text(LOCTEXT("NodeTable", "Node DataTable:")) ]
			+ SHorizontalBox::Slot().FillWidth(1).Padding(2)
			[
				SNew(SComboBox<TSharedPtr<FString>>)
				.OptionsSource(&NodeTableChoiceLabels)
				.OnGenerateWidget_Lambda([](TSharedPtr<FString> S) { return SNew(STextBlock).Text(FText::FromString(S.IsValid() ? *S : FString())); })
				.OnSelectionChanged_Lambda([this](TSharedPtr<FString> S, ESelectInfo::Type)
				{
					const int32 Idx = NodeTableChoiceLabels.IndexOfByKey(S);
					if (NodeTableChoices.IsValidIndex(Idx))
					{
						NodeTable = NodeTableChoices[Idx];
						RefreshNodeRowNameOptions();
					}
				})
				[ SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(NodeTable.IsValid() ? NodeTable->GetName() : TEXT("선택...")); }) ]
			]
		]

		// ── 행 리스트 ──
		+ SVerticalBox::Slot().FillHeight(1).Padding(4)
		[
			SAssignNew(RowListView, SListView<FRowPtr>)
			.ListItemsSource(&Rows)
			.OnGenerateRow(this, &SOutlierUpgradeEffectToolWidget::GenerateRow)
			.OnSelectionChanged(this, &SOutlierUpgradeEffectToolWidget::OnRowSelected)
			.SelectionMode(ESelectionMode::Single)
			.HeaderRow(
				SNew(SHeaderRow)
				+ SHeaderRow::Column(TEXT("Name")).DefaultLabel(LOCTEXT("H_Name", "Name")).FillWidth(0.18f)
				+ SHeaderRow::Column(TEXT("Node")).DefaultLabel(LOCTEXT("H_Node", "Node")).FillWidth(0.18f)
				+ SHeaderRow::Column(TEXT("Type")).DefaultLabel(LOCTEXT("H_Type", "종류")).FillWidth(0.16f)
				+ SHeaderRow::Column(TEXT("Target")).DefaultLabel(LOCTEXT("H_Target", "대상")).FillWidth(0.20f)
				+ SHeaderRow::Column(TEXT("Field")).DefaultLabel(LOCTEXT("H_Field", "Field")).FillWidth(0.10f)
				+ SHeaderRow::Column(TEXT("Op")).DefaultLabel(LOCTEXT("H_Op", "Op")).FillWidth(0.08f)
				+ SHeaderRow::Column(TEXT("Mag")).DefaultLabel(LOCTEXT("H_Mag", "Mag")).FillWidth(0.06f)
				+ SHeaderRow::Column(TEXT("Key")).DefaultLabel(LOCTEXT("H_Key", "GE")).FillWidth(0.12f)
			)
		]

		// ── 편집 패널 ──
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SNew(SGridPanel)
			// NodeRowName
			+ SGridPanel::Slot(0, 0).Padding(2) [ SNew(STextBlock).Text(LOCTEXT("NodeRowName", "NodeRowName")) ]
			+ SGridPanel::Slot(1, 0).Padding(2)
			[
				MakeNameCombo(NodeRowNameOptions,
					[this](FName N) { EditRow.NodeRowName = N; if (!bNameEditedManually) EditRow.Name = BuildAutoName(); },
					[this]() { return EditRow.NodeRowName.IsNone() ? LOCTEXT("Pick", "선택...") : FText::FromName(EditRow.NodeRowName); },
					[]() { return true; },
					NodeRowNameCombo)
			]
			// EffectType
			+ SGridPanel::Slot(0, 1).Padding(2) [ SNew(STextBlock).Text(LOCTEXT("EffectType", "종류 (EffectType)")) ]
			+ SGridPanel::Slot(1, 1).Padding(2)
			[
				SNew(SComboBox<FStringPtr>)
				.OptionsSource(&EffectTypeLabelOptions)
				.OnGenerateWidget_Lambda([](FStringPtr S) { return SNew(STextBlock).Text(FText::FromString(S.IsValid() ? *S : FString())); })
				.OnSelectionChanged_Lambda([this](FStringPtr S, ESelectInfo::Type)
				{
					if (S.IsValid())
					{
						EditRow.EffectType = EffectTypeFromLabel(*S);
						// 대상 태그가 새 종류의 네임스페이스와 안 맞으면 비운다 ( 예: ApplyEffect 인데 Attribute.* 태그 )
						{
							const FString NewPrefix = TargetTagPrefix(EditRow.EffectType);
							if (EditRow.TargetTag.IsValid() && !EditRow.TargetTag.ToString().StartsWith(NewPrefix))
							{
								EditRow.TargetTag = FGameplayTag();
							}
						}
						// 해당 종류에 안 쓰는 필드는 초기화 ( 잘못된 값이 CSV 에 안 써지도록 )
						if (EditRow.EffectType != EOutlierUpgradeEffectType::AbilityConfig) { EditRow.ConfigField = NAME_None; }
						if (EditRow.EffectType != EOutlierUpgradeEffectType::ApplyEffect)   { EditRow.EffectClassKey = NAME_None; }
						// 수치형(Attribute/AbilityConfig)이 아니면 Op/Mag 는 무의미 -> Op 는 Override 로 고정
						const bool bNumeric =
							EditRow.EffectType == EOutlierUpgradeEffectType::Attribute ||
							EditRow.EffectType == EOutlierUpgradeEffectType::AbilityConfig;
						if (!bNumeric) { EditRow.Op = EOutlierUpgradeModOp::Override; }
						RefreshTargetTagOptions();
						if (TargetTagCombo.IsValid()) TargetTagCombo->RefreshOptions();
						if (!bNameEditedManually) EditRow.Name = BuildAutoName();
					}
				})
				[ SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(EffectTypeLabel(EditRow.EffectType)); }) ]
			]
			// TargetTag
			+ SGridPanel::Slot(0, 2).Padding(2) [ SNew(STextBlock).Text(LOCTEXT("TargetTag", "대상 (TargetTag)")) ]
			+ SGridPanel::Slot(1, 2).Padding(2)
			[
				MakeNameCombo(TargetTagOptions,
					[this](FName N) { EditRow.TargetTag = FGameplayTag::RequestGameplayTag(N, false); if (!bNameEditedManually) EditRow.Name = BuildAutoName(); },
					[this]() { return EditRow.TargetTag.IsValid() ? FText::FromName(EditRow.TargetTag.GetTagName()) : LOCTEXT("Pick", "선택..."); },
					[]() { return true; },
					TargetTagCombo)
			]
			// ConfigField
			+ SGridPanel::Slot(0, 3).Padding(2) [ SNew(STextBlock).Text(LOCTEXT("ConfigField", "필드 (ConfigField / 스킬스텟용)")) ]
			+ SGridPanel::Slot(1, 3).Padding(2)
			[
				SNew(SComboBox<FNamePtr>)
				.OptionsSource(&ConfigFieldOptions)
				.IsEnabled_Lambda([this]() { return EditRow.EffectType == EOutlierUpgradeEffectType::AbilityConfig; })
				.OnGenerateWidget_Lambda([](FNamePtr I) { return SNew(STextBlock).Text(FText::FromName(I.IsValid() ? *I : NAME_None)); })
				.OnSelectionChanged_Lambda([this](FNamePtr I, ESelectInfo::Type) { if (I.IsValid()) EditRow.ConfigField = *I; })
				[ SNew(STextBlock).Text_Lambda([this]() { return FText::FromName(EditRow.ConfigField); }) ]
			]
			// Op
			+ SGridPanel::Slot(0, 4).Padding(2) [ SNew(STextBlock).Text(LOCTEXT("Op", "연산 (Op)")) ]
			+ SGridPanel::Slot(1, 4).Padding(2)
			[
				SNew(SComboBox<FStringPtr>)
				.OptionsSource(&OpLabelOptions)
				.IsEnabled_Lambda([this]() { return EditRow.EffectType == EOutlierUpgradeEffectType::Attribute || EditRow.EffectType == EOutlierUpgradeEffectType::AbilityConfig; })
				.OnGenerateWidget_Lambda([](FStringPtr S) { return SNew(STextBlock).Text(FText::FromString(S.IsValid() ? *S : FString())); })
				.OnSelectionChanged_Lambda([this](FStringPtr S, ESelectInfo::Type) { if (S.IsValid()) EditRow.Op = OpFromLabel(*S); })
				[ SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(OpLabel(EditRow.Op)); }) ]
			]
			// Magnitude
			+ SGridPanel::Slot(0, 5).Padding(2) [ SNew(STextBlock).Text(LOCTEXT("Mag", "수치 (Magnitude)")) ]
			+ SGridPanel::Slot(1, 5).Padding(2)
			[
				SNew(SSpinBox<float>)
				.MinValue(TOptional<float>()).MaxValue(TOptional<float>())
				.IsEnabled_Lambda([this]() { return EditRow.EffectType == EOutlierUpgradeEffectType::Attribute || EditRow.EffectType == EOutlierUpgradeEffectType::AbilityConfig; })
				.Value_Lambda([this]() { return EditRow.Magnitude; })
				.OnValueChanged_Lambda([this](float V) { EditRow.Magnitude = V; })
			]
			// EffectClassKey
			+ SGridPanel::Slot(0, 6).Padding(2) [ SNew(STextBlock).Text(LOCTEXT("EffectClassKey", "GE 키 (ApplyEffect용)")) ]
			+ SGridPanel::Slot(1, 6).Padding(2)
			[
				MakeNameCombo(EffectClassKeyOptions,
					[this](FName N) { EditRow.EffectClassKey = N; },
					[this]() { return EditRow.EffectClassKey.IsNone() ? LOCTEXT("Pick", "선택...") : FText::FromName(EditRow.EffectClassKey); },
					[this]() { return EditRow.EffectType == EOutlierUpgradeEffectType::ApplyEffect; },
					EffectClassKeyCombo)
			]
			// Name (자동생성 + 수정가능)
			+ SGridPanel::Slot(0, 7).Padding(2) [ SNew(STextBlock).Text(LOCTEXT("Name", "행 이름 (Name / 자동)")) ]
			+ SGridPanel::Slot(1, 7).Padding(2)
			[
				SNew(SEditableTextBox)
				.Text_Lambda([this]() { return FText::FromName(EditRow.Name); })
				.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type) { EditRow.Name = FName(*T.ToString()); bNameEditedManually = true; })
			]
		]

		// ── 버튼 ──
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[ SNew(SButton).Text(LOCTEXT("AddUpdate", "추가/수정")).OnClicked(this, &SOutlierUpgradeEffectToolWidget::OnAddRow) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[ SNew(SButton).Text(LOCTEXT("Delete", "삭제")).OnClicked(this, &SOutlierUpgradeEffectToolWidget::OnDeleteRow) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(12, 2, 2, 2)
			[ SNew(SButton).Text(LOCTEXT("Save", "저장 + Reimport")).OnClicked(this, &SOutlierUpgradeEffectToolWidget::OnSaveAndReimport) ]
		]

		// ── 상태 ──
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[ SNew(STextBlock).Text_Lambda([this]() { return StatusText; }) ]
	];

	SetStatus(LOCTEXT("Ready", "Effect / Node DataTable 을 선택하세요."));
}

void SOutlierUpgradeEffectToolWidget::RefreshDataTableChoices()
{
	GatherDataTablesByRowStruct(FOutlierUpgradeEffectRow::StaticStruct(), EffectTableChoices);
	GatherDataTablesByRowStruct(FOutlierUpgradeNodeRow::StaticStruct(), NodeTableChoices);

	EffectTableChoiceLabels.Reset();
	for (const TWeakObjectPtr<UDataTable>& T : EffectTableChoices)
	{
		EffectTableChoiceLabels.Add(MakeShared<FString>(T.IsValid() ? T->GetName() : TEXT("?")));
	}
	NodeTableChoiceLabels.Reset();
	for (const TWeakObjectPtr<UDataTable>& T : NodeTableChoices)
	{
		NodeTableChoiceLabels.Add(MakeShared<FString>(T.IsValid() ? T->GetName() : TEXT("?")));
	}
}

void SOutlierUpgradeEffectToolWidget::ReloadRowsFromEffectTable()
{
	Rows.Reset();
	if (UDataTable* Table = EffectTable.Get())
	{
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			const FOutlierUpgradeEffectRow* Src = reinterpret_cast<const FOutlierUpgradeEffectRow*>(Pair.Value);
			if (!Src)
			{
				continue;
			}
			FRowPtr New = MakeShared<FOutlierUpgradeEffectToolRow>();
			New->Name = Pair.Key;
			New->NodeRowName = Src->NodeRowName;
			New->EffectType = Src->EffectType;
			New->TargetTag = Src->TargetTag;
			New->ConfigField = Src->ConfigField;
			New->Op = Src->Op;
			New->Magnitude = Src->Magnitude;
			New->EffectClassKey = Src->EffectClassKey;
			Rows.Add(New);
		}
		Rows.Sort([](const FRowPtr& A, const FRowPtr& B) { return A->Name.Compare(B->Name) < 0; });
	}
	if (RowListView.IsValid())
	{
		RowListView->RequestListRefresh();
	}
	SetStatus(FText::Format(LOCTEXT("Loaded", "{0}개 행 로드됨."), FText::AsNumber(Rows.Num())));
}

void SOutlierUpgradeEffectToolWidget::RefreshTargetTagOptions()
{
	GatherTagsByPrefix(TargetTagPrefix(EditRow.EffectType), TargetTagOptions);
}

void SOutlierUpgradeEffectToolWidget::RefreshNodeRowNameOptions()
{
	NodeRowNameOptions.Reset();
	if (UDataTable* Table = NodeTable.Get())
	{
		TArray<FName> Names = Table->GetRowNames();
		Names.Sort(FNameLexicalLess());
		for (const FName& N : Names)
		{
			NodeRowNameOptions.Add(MakeShared<FName>(N));
		}
	}
	if (NodeRowNameCombo.IsValid())
	{
		NodeRowNameCombo->RefreshOptions();
	}
}

void SOutlierUpgradeEffectToolWidget::RefreshEffectClassKeyOptions()
{
	GatherEffectClassKeys(EffectClassKeyOptions);
	if (EffectClassKeyCombo.IsValid())
	{
		EffectClassKeyCombo->RefreshOptions();
	}
}

TSharedRef<ITableRow> SOutlierUpgradeEffectToolWidget::GenerateRow(FRowPtr Row, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SEffectTableRow, OwnerTable).Item(Row);
}

void SOutlierUpgradeEffectToolWidget::OnRowSelected(FRowPtr Row, ESelectInfo::Type SelectInfo)
{
	if (Row.IsValid())
	{
		PullSelectedRowToEdit(*Row);
	}
}

void SOutlierUpgradeEffectToolWidget::PullSelectedRowToEdit(const FOutlierUpgradeEffectToolRow& Row)
{
	EditRow = Row;
	bNameEditedManually = true; // 기존 행 편집 중엔 자동 rename 안 함
	RefreshTargetTagOptions();
	if (TargetTagCombo.IsValid()) TargetTagCombo->RefreshOptions();
}

FName SOutlierUpgradeEffectToolWidget::BuildAutoName() const
{
	FString NodePart = EditRow.NodeRowName.ToString();
	NodePart.RemoveFromStart(TEXT("Shooter_"));

	FString Leaf;
	if (EditRow.TargetTag.IsValid())
	{
		const FString TagStr = EditRow.TargetTag.ToString();
		FString Discard;
		if (!TagStr.Split(TEXT("."), &Discard, &Leaf, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
		{
			Leaf = TagStr;
		}
	}
	if (!EditRow.ConfigField.IsNone())
	{
		Leaf = EditRow.ConfigField.ToString();
	}

	FString Candidate = Leaf.IsEmpty() ? NodePart : FString::Printf(TEXT("%s_%s"), *NodePart, *Leaf);

	// 유일성 보장
	int32 Suffix = 2;
	FString Unique = Candidate;
	auto Exists = [this](const FString& S)
	{
		for (const FRowPtr& R : Rows)
		{
			if (R->Name.ToString() == S) { return true; }
		}
		return false;
	};
	while (Exists(Unique))
	{
		Unique = FString::Printf(TEXT("%s_%d"), *Candidate, Suffix++);
	}
	return FName(*Unique);
}

FReply SOutlierUpgradeEffectToolWidget::OnAddRow()
{
	// ── 검증 ──
	if (EditRow.NodeRowName.IsNone())
	{
		SetStatus(LOCTEXT("NeedNode", "NodeRowName 을 선택하세요."));
		return FReply::Handled();
	}
	if (!EditRow.TargetTag.IsValid())
	{
		SetStatus(LOCTEXT("NeedTarget", "대상(TargetTag) 을 선택하세요."));
		return FReply::Handled();
	}
	if (EditRow.EffectType == EOutlierUpgradeEffectType::AbilityConfig && EditRow.ConfigField.IsNone())
	{
		SetStatus(LOCTEXT("NeedField", "스킬 스텟(AbilityConfig)은 ConfigField 가 필요합니다."));
		return FReply::Handled();
	}
	if (EditRow.EffectType == EOutlierUpgradeEffectType::ApplyEffect && EditRow.EffectClassKey.IsNone())
	{
		SetStatus(LOCTEXT("NeedKey", "능력 시 추가효과(ApplyEffect)는 GE 키가 필요합니다."));
		return FReply::Handled();
	}

	// 종류에 안 쓰는 필드는 확실히 비워서 기록 ( 방어 )
	if (EditRow.EffectType != EOutlierUpgradeEffectType::AbilityConfig) { EditRow.ConfigField = NAME_None; }
	if (EditRow.EffectType != EOutlierUpgradeEffectType::ApplyEffect)   { EditRow.EffectClassKey = NAME_None; }

	if (EditRow.Name.IsNone())
	{
		EditRow.Name = BuildAutoName();
	}

	// 같은 Name 있으면 수정, 없으면 추가
	FRowPtr Target;
	for (const FRowPtr& R : Rows)
	{
		if (R->Name == EditRow.Name) { Target = R; break; }
	}
	if (!Target.IsValid())
	{
		Target = MakeShared<FOutlierUpgradeEffectToolRow>();
		Rows.Add(Target);
	}
	*Target = EditRow;

	Rows.Sort([](const FRowPtr& A, const FRowPtr& B) { return A->Name.Compare(B->Name) < 0; });
	if (RowListView.IsValid()) { RowListView->RequestListRefresh(); }
	SetStatus(FText::Format(LOCTEXT("Applied", "'{0}' 반영됨 (저장 전)."), FText::FromName(EditRow.Name)));
	return FReply::Handled();
}

FReply SOutlierUpgradeEffectToolWidget::OnDeleteRow()
{
	Rows.RemoveAll([this](const FRowPtr& R) { return R->Name == EditRow.Name; });
	if (RowListView.IsValid()) { RowListView->RequestListRefresh(); }
	SetStatus(FText::Format(LOCTEXT("Deleted", "'{0}' 삭제됨 (저장 전)."), FText::FromName(EditRow.Name)));
	return FReply::Handled();
}

FReply SOutlierUpgradeEffectToolWidget::OnSaveAndReimport()
{
	UDataTable* Table = EffectTable.Get();
	if (!Table)
	{
		SetStatus(LOCTEXT("NoTable", "Effect DataTable 을 먼저 선택하세요."));
		return FReply::Handled();
	}

	FString CsvPath;
	if (Table->AssetImportData)
	{
		CsvPath = Table->AssetImportData->GetFirstFilename();
	}
	if (CsvPath.IsEmpty() || !FPaths::FileExists(CsvPath))
	{
		SetStatus(LOCTEXT("NoCsv", "이 DataTable 의 소스 CSV 경로를 찾지 못했습니다."));
		return FReply::Handled();
	}

	// CSV 문자열 빌드
	FString Csv = TEXT("Name,NodeRowName,EffectType,TargetTag,ConfigField,Op,Magnitude,EffectClassKey\r\n");
	for (const FRowPtr& R : Rows)
	{
		Csv += FString::Printf(TEXT("%s,%s,%s,%s,%s,%s,%s,%s\r\n"),
			*R->Name.ToString(),
			*R->NodeRowName.ToString(),
			*EffectTypeEnumString(R->EffectType),
			*WrapTag(R->TargetTag),
			*NameOrEmpty(R->ConfigField),
			*OpEnumString(R->Op),
			*FString::SanitizeFloat(R->Magnitude),
			*NameOrEmpty(R->EffectClassKey));
	}

	// UTF-8 with BOM 으로 직접 기록 ( UE DataTable CSV 임포트가 한글을 안 깨뜨리도록 )
	FTCHARToUTF8 Utf8Csv(*Csv);
	TArray<uint8> Bytes;
	Bytes.Reserve(Utf8Csv.Length() + 3);
	Bytes.Add(0xEF);
	Bytes.Add(0xBB);
	Bytes.Add(0xBF);
	Bytes.Append(reinterpret_cast<const uint8*>(Utf8Csv.Get()), Utf8Csv.Length());
	if (!FFileHelper::SaveArrayToFile(Bytes, *CsvPath))
	{
		SetStatus(FText::Format(LOCTEXT("WriteFail", "CSV 쓰기 실패: {0}"), FText::FromString(CsvPath)));
		return FReply::Handled();
	}

	// Reimport
	FReimportManager::Instance()->Reimport(
		Table,
		/*bAskForNewFileIfMissing*/ false,
		/*bShowNotification*/ true,
		CsvPath,
		/*SpecifiedReimportHandler*/ nullptr,
		/*SourceFileIndex*/ INDEX_NONE,
		/*bForceNewFile*/ false,
		/*bAutomated*/ true);

	ReloadRowsFromEffectTable();
	SetStatus(FText::Format(LOCTEXT("Saved", "저장 + Reimport 완료 ({0})."), FText::FromString(FPaths::GetCleanFilename(CsvPath))));
	return FReply::Handled();
}

void SOutlierUpgradeEffectToolWidget::SetStatus(const FText& InText)
{
	StatusText = InText;
}

#undef LOCTEXT_NAMESPACE
