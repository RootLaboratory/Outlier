#include "OutlierUpgradeEffectToolWidget.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "EditorFramework/AssetImportData.h"
#include "EditorReimportHandler.h"
#include "Engine/DataTable.h"
#include "GameplayTagsManager.h"
#include "Containers/StringConv.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"
#include "GAS/Data/OutlierPartnerAbilityConfig.h"
#include "GAS/Data/OutlierShooterSuitAbilityDataRow.h"
#include "Upgrade/OutlierUpgradeTypes.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
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
			EOutlierUpgradeEffectType::FunctionOverride
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
		default:                                          return FString();
		}
	}

	// FunctionOverride 전용: 선택된 Node 의 AbilityTag ( "Ability.Shooter.X" ) 를
	// 대응하는 Upgrade 네임스페이스 ( "Upgrade.Shooter.X" ) 로 바꿔, 그 하위 태그만 후보로 남긴다.
	// AbilityTag 가 없으면 기존 "Upgrade.Shooter" 전체로 폴백한다.
	FString FunctionOverrideTargetTagPrefix(const FGameplayTag& AbilityTag)
	{
		static const FString AbilityRoot = TEXT("Ability.");
		static const FString UpgradeRoot = TEXT("Upgrade.");
		if (AbilityTag.IsValid())
		{
			const FString S = AbilityTag.ToString();
			if (S.StartsWith(AbilityRoot))
			{
				return UpgradeRoot + S.RightChop(AbilityRoot.Len());
			}
		}
		return TargetTagPrefix(EOutlierUpgradeEffectType::FunctionOverride);
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

	// Role 별 AbilityConfig 투영 대상 struct 의 float 필드 이름들 ( 값은 몰라도 됨, 리플렉션 ).
	// ConfigField 는 런타임에 FindFProperty 로 이 struct 에서 이름을 찾으므로, 여기 없는 이름을 쓰면
	// 그 Effect 는 게임에서 아무 효과도 못 낸다 ( 조용히 실패 ) - 반드시 이 목록에서 골라야 한다.
	void GatherConfigFieldNames(TArray<TSharedPtr<FName>>& Out, EOutlierUpgradeRole Role)
	{
		Out.Reset();
		const UScriptStruct* Struct = (Role == EOutlierUpgradeRole::Partner)
			? FOutlierPartnerAbilityConfig::StaticStruct()
			: FOutlierShooterSuitAbilityDataRow::StaticStruct();
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			if (It->IsA(FFloatProperty::StaticClass()))
			{
				Out.Add(MakeShared<FName>(It->GetFName()));
			}
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

	FString RoleEnumString(EOutlierUpgradeRole Role)
	{
		switch (Role)
		{
		case EOutlierUpgradeRole::Shooter: return TEXT("EOutlierUpgradeRole::Shooter");
		case EOutlierUpgradeRole::Partner: return TEXT("EOutlierUpgradeRole::Partner");
		default:                           return TEXT("EOutlierUpgradeRole::None");
		}
	}

	FString RoleLabel(EOutlierUpgradeRole Role)
	{
		switch (Role)
		{
		case EOutlierUpgradeRole::Shooter: return TEXT("Shooter");
		case EOutlierUpgradeRole::Partner: return TEXT("Partner");
		default:                           return TEXT("None");
		}
	}

	EOutlierUpgradeRole RoleFromLabel(const FString& Label)
	{
		if (Label == RoleLabel(EOutlierUpgradeRole::Shooter)) { return EOutlierUpgradeRole::Shooter; }
		if (Label == RoleLabel(EOutlierUpgradeRole::Partner)) { return EOutlierUpgradeRole::Partner; }
		return EOutlierUpgradeRole::None;
	}

	// ParentId 콤보에서 "부모 없음(루트)" 을 나타내는 표시 텍스트.
	FText ParentIdDisplayText(FName ParentId)
	{
		return ParentId.IsNone() ? LOCTEXT("NoParent", "(없음 / 루트)") : FText::FromName(ParentId);
	}

	// 콤마/따옴표/줄바꿈이 들어있는 자유 텍스트( DisplayName, Desc 등 )를 CSV 필드로 안전하게 감싼다.
	FString CsvField(const FString& In)
	{
		if (In.Contains(TEXT(",")) || In.Contains(TEXT("\"")) || In.Contains(TEXT("\n")) || In.Contains(TEXT("\r")))
		{
			FString Escaped = In;
			Escaped.ReplaceInline(TEXT("\""), TEXT("\"\""));
			return FString::Printf(TEXT("\"%s\""), *Escaped);
		}
		return In;
	}

	// UTF-8 with BOM 으로 직접 기록 ( UE DataTable CSV 임포트가 한글을 안 깨뜨리도록 )
	bool WriteUtf8CsvFile(const FString& Path, const FString& Csv)
	{
		FTCHARToUTF8 Utf8Csv(*Csv);
		TArray<uint8> Bytes;
		Bytes.Reserve(Utf8Csv.Length() + 3);
		Bytes.Add(0xEF);
		Bytes.Add(0xBB);
		Bytes.Add(0xBF);
		Bytes.Append(reinterpret_cast<const uint8*>(Utf8Csv.Get()), Utf8Csv.Length());
		return FFileHelper::SaveArrayToFile(Bytes, *Path);
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

			return SNew(SBox).Padding(4, 2).VAlign(VAlign_Center)
				[ SNew(STextBlock).Text(FText::FromString(Text)) ];
		}

	private:
		FEffectRowItem Item;
	};

	using FNodeRowItem = TSharedPtr<FOutlierUpgradeNodeToolRow>;

	// Node 테이블 리스트용 다중 컬럼 행.
	class SNodeTableRow : public SMultiColumnTableRow<FNodeRowItem>
	{
	public:
		SLATE_BEGIN_ARGS(SNodeTableRow) {}
			SLATE_ARGUMENT(FNodeRowItem, Item)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
		{
			Item = InArgs._Item;
			SMultiColumnTableRow<FNodeRowItem>::Construct(FSuperRowType::FArguments(), OwnerTable);
		}

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
		{
			if (!Item.IsValid())
			{
				return SNullWidget::NullWidget;
			}

			FString Text;
			if (ColumnName == TEXT("RowName"))          { Text = Item->RowName.ToString(); }
			else if (ColumnName == TEXT("Role"))        { Text = RoleLabel(Item->Role); }
			else if (ColumnName == TEXT("TreeId"))      { Text = NameOrEmpty(Item->TreeId); }
			else if (ColumnName == TEXT("NodeId"))      { Text = NameOrEmpty(Item->NodeId); }
			else if (ColumnName == TEXT("ParentId"))    { Text = NameOrEmpty(Item->ParentId); }
			else if (ColumnName == TEXT("Cost"))        { Text = FString::FromInt(Item->Cost); }
			else if (ColumnName == TEXT("DisplayName")) { Text = Item->DisplayName.ToString(); }

			return SNew(SBox).Padding(4, 2).VAlign(VAlign_Center)
				[ SNew(STextBlock).Text(FText::FromString(Text)) ];
		}

	private:
		FNodeRowItem Item;
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
	RoleLabelOptions.Add(MakeShared<FString>(RoleLabel(EOutlierUpgradeRole::None)));
	RoleLabelOptions.Add(MakeShared<FString>(RoleLabel(EOutlierUpgradeRole::Shooter)));
	RoleLabelOptions.Add(MakeShared<FString>(RoleLabel(EOutlierUpgradeRole::Partner)));

	RefreshConfigFieldOptions();
	RefreshDataTableChoices();
	RefreshTargetTagOptions();
	RefreshNodeAbilityTagOptions();

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
						ReloadRowsFromNodeTable();
					}
				})
				[ SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(NodeTable.IsValid() ? NodeTable->GetName() : TEXT("선택...")); }) ]
			]
		]

		// ── Effect / Node 전환 탭 ── ( 기본은 Effect )
		+ SVerticalBox::Slot().AutoHeight().Padding(4, 0, 4, 4)
		[
			SNew(SHorizontalBox)
			// 항상 눌리는 버튼으로 두고, 현재 탭은 색으로만 표시한다.
			// ( 이전엔 현재 탭 버튼을 IsEnabled=false 로 꺼뒀는데, 그러면 "눌러도 반응이 없다"로 보여서 헷갈렸다. )
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[
				SNew(SButton)
				.Text(LOCTEXT("TabEffect", "Effect 관리"))
				.ButtonColorAndOpacity_Lambda([this]() { return bShowNodePanel ? FLinearColor::White : FLinearColor(0.35f, 0.6f, 1.0f); })
				.OnClicked(this, &SOutlierUpgradeEffectToolWidget::OnSwitchToEffectPanel)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[
				SNew(SButton)
				.Text(LOCTEXT("TabNode", "Node 관리"))
				.ButtonColorAndOpacity_Lambda([this]() { return bShowNodePanel ? FLinearColor(0.35f, 0.6f, 1.0f) : FLinearColor::White; })
				.OnClicked(this, &SOutlierUpgradeEffectToolWidget::OnSwitchToNodePanel)
			]
		]

		// ── 행 리스트 ( Effect ) ──
		+ SVerticalBox::Slot().FillHeight(1).Padding(4)
		[
			SAssignNew(RowListView, SListView<FRowPtr>)
			.Visibility_Lambda([this]() { return bShowNodePanel ? EVisibility::Collapsed : EVisibility::Visible; })
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
			)
		]

		// ── 행 리스트 ( Node ) ──
		+ SVerticalBox::Slot().FillHeight(1).Padding(4)
		[
			SAssignNew(NodeListView, SListView<FNodeRowPtr>)
			.Visibility_Lambda([this]() { return bShowNodePanel ? EVisibility::Visible : EVisibility::Collapsed; })
			.ListItemsSource(&NodeRows)
			.OnGenerateRow(this, &SOutlierUpgradeEffectToolWidget::GenerateNodeRow)
			.OnSelectionChanged(this, &SOutlierUpgradeEffectToolWidget::OnNodeRowSelected)
			.SelectionMode(ESelectionMode::Single)
			.HeaderRow(
				SNew(SHeaderRow)
				+ SHeaderRow::Column(TEXT("RowName")).DefaultLabel(LOCTEXT("H_RowName", "RowName")).FillWidth(0.20f)
				+ SHeaderRow::Column(TEXT("Role")).DefaultLabel(LOCTEXT("H_Role", "Role")).FillWidth(0.10f)
				+ SHeaderRow::Column(TEXT("TreeId")).DefaultLabel(LOCTEXT("H_TreeId", "TreeId")).FillWidth(0.14f)
				+ SHeaderRow::Column(TEXT("NodeId")).DefaultLabel(LOCTEXT("H_NodeId", "NodeId")).FillWidth(0.10f)
				+ SHeaderRow::Column(TEXT("ParentId")).DefaultLabel(LOCTEXT("H_ParentId", "ParentId")).FillWidth(0.20f)
				+ SHeaderRow::Column(TEXT("Cost")).DefaultLabel(LOCTEXT("H_Cost", "Cost")).FillWidth(0.06f)
				+ SHeaderRow::Column(TEXT("DisplayName")).DefaultLabel(LOCTEXT("H_DisplayName", "DisplayName")).FillWidth(0.20f)
			)
		]

		// ── 편집 패널 ( Effect ) ──
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return bShowNodePanel ? EVisibility::Collapsed : EVisibility::Visible; })
			[
			SNew(SGridPanel)
			// NodeRowName
			+ SGridPanel::Slot(0, 0).Padding(2) [ SNew(STextBlock).Text(LOCTEXT("NodeRowName", "NodeRowName")) ]
			+ SGridPanel::Slot(1, 0).Padding(2)
			[
				MakeNameCombo(NodeRowNameOptions,
					[this](FName N)
					{
						EditRow.NodeRowName = N;
						// FunctionOverride 의 대상 태그 후보는 Node 의 AbilityTag 하위로 좁혀지므로,
						// Node 가 바뀌면 후보를 다시 모으고 더 이상 맞지 않는 선택은 비운다.
						if (EditRow.EffectType == EOutlierUpgradeEffectType::FunctionOverride)
						{
							const FString NewPrefix = FunctionOverrideTargetTagPrefix(GetSelectedNodeAbilityTag());
							if (EditRow.TargetTag.IsValid() && !EditRow.TargetTag.ToString().StartsWith(NewPrefix))
							{
								EditRow.TargetTag = FGameplayTag();
							}
							RefreshTargetTagOptions();
							if (TargetTagCombo.IsValid()) TargetTagCombo->RefreshOptions();
						}
						// ConfigField 후보도 Node 의 Role ( Shooter / Partner ) 에 맞는 struct 로 다시 모은다.
						// ( 이게 없으면 예전 Role 의 필드 이름이 그대로 남아서, 존재하지 않는 필드를 가리키는
						//   깨진 ConfigField 가 CSV 에 저장될 수 있다. )
						RefreshConfigFieldOptions();
						if (ConfigFieldCombo.IsValid()) ConfigFieldCombo->RefreshOptions();
						if (!bNameEditedManually) EditRow.Name = BuildAutoName();
					},
					[this]() { return EditRow.NodeRowName.IsNone() ? LOCTEXT("Pick", "선택...") : FText::FromName(EditRow.NodeRowName); },
					[]() { return true; },
					NodeRowNameCombo)
			]
			// 이 Effect가 물고 있는 Node 로 바로 이동 ( Node 관리 탭으로 전환 + 해당 Node 미리 선택 )
			+ SGridPanel::Slot(2, 0).Padding(8, 2, 2, 2)
			[
				SNew(SButton)
				.Text(LOCTEXT("GoToNode", "Node 보기"))
				.ToolTipText(LOCTEXT("GoToNodeTip", "Node 관리 탭으로 이동합니다. NodeRowName 이 선택되어 있으면 그 Node 를 바로 보여줍니다."))
				.OnClicked(this, &SOutlierUpgradeEffectToolWidget::OnSwitchToNodePanel)
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
						// 대상 태그가 새 종류의 네임스페이스와 안 맞으면 비운다.
						{
							const FString NewPrefix = TargetTagPrefix(EditRow.EffectType);
							if (EditRow.TargetTag.IsValid() && !EditRow.TargetTag.ToString().StartsWith(NewPrefix))
							{
								EditRow.TargetTag = FGameplayTag();
							}
						}
						// 해당 종류에 안 쓰는 필드는 초기화 ( 잘못된 값이 CSV 에 안 써지도록 )
						if (EditRow.EffectType != EOutlierUpgradeEffectType::AbilityConfig) { EditRow.ConfigField = NAME_None; }
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
				SAssignNew(ConfigFieldCombo, SComboBox<FNamePtr>)
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
			// Name (자동생성 + 수정가능)
			+ SGridPanel::Slot(0, 6).Padding(2) [ SNew(STextBlock).Text(LOCTEXT("Name", "행 이름 (Name / 자동)")) ]
			+ SGridPanel::Slot(1, 6).Padding(2)
			[
				SNew(SEditableTextBox)
				.Text_Lambda([this]() { return FText::FromName(EditRow.Name); })
				.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type) { EditRow.Name = FName(*T.ToString()); bNameEditedManually = true; })
			]
			]
		]

		// ── 편집 패널 ( Node ) ──
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return bShowNodePanel ? EVisibility::Visible : EVisibility::Collapsed; })
			[
			SNew(SGridPanel)
			// RowName ( 현재 선택된 Node ) + 이름 변경
			+ SGridPanel::Slot(0, 0).Padding(2) [ SNew(STextBlock).Text(LOCTEXT("NodeRowNameLabel", "RowName (선택됨)")) ]
			+ SGridPanel::Slot(1, 0).Padding(2)
			[
				SNew(STextBlock).Text_Lambda([this]() { return NodeEditRow.RowName.IsNone() ? LOCTEXT("NoNodeSelected", "(새 Node)") : FText::FromName(NodeEditRow.RowName); })
			]
			+ SGridPanel::Slot(2, 0).Padding(8, 2, 2, 2)
			[
				SNew(SEditableTextBox)
				.MinDesiredWidth(160)
				.HintText(LOCTEXT("NewNodeName", "새 이름 입력 (변경/새 Node)..."))
				.Text_Lambda([this]() { return FText::FromString(NodeRenameInputText); })
				.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type) { NodeRenameInputText = T.ToString(); })
			]
			+ SGridPanel::Slot(3, 0).Padding(2)
			[
				SNew(SButton)
				.Text(LOCTEXT("RenameNode", "이름 변경"))
				.ToolTipText(LOCTEXT("RenameNodeTip", "선택된 Node 를 새 이름으로 바꾸고, 관련된 ParentId / Effect의 NodeRowName 을 함께 갱신합니다. ( 확인 필요 )"))
				.OnClicked(this, &SOutlierUpgradeEffectToolWidget::OnRenameNodeRow)
			]
			// Role
			+ SGridPanel::Slot(0, 1).Padding(2) [ SNew(STextBlock).Text(LOCTEXT("NodeRole", "Role")) ]
			+ SGridPanel::Slot(1, 1).Padding(2)
			[
				SNew(SComboBox<FStringPtr>)
				.OptionsSource(&RoleLabelOptions)
				.OnGenerateWidget_Lambda([](FStringPtr S) { return SNew(STextBlock).Text(FText::FromString(S.IsValid() ? *S : FString())); })
				.OnSelectionChanged_Lambda([this](FStringPtr S, ESelectInfo::Type) { if (S.IsValid()) { NodeEditRow.Role = RoleFromLabel(*S); } })
				[ SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(RoleLabel(NodeEditRow.Role)); }) ]
			]
			// TreeId
			+ SGridPanel::Slot(0, 2).Padding(2) [ SNew(STextBlock).Text(LOCTEXT("NodeTreeId", "TreeId")) ]
			+ SGridPanel::Slot(1, 2).Padding(2)
			[
				SNew(SEditableTextBox)
				.Text_Lambda([this]() { return FText::FromName(NodeEditRow.TreeId); })
				.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type) { NodeEditRow.TreeId = FName(*T.ToString()); })
			]
			// NodeId
			+ SGridPanel::Slot(0, 3).Padding(2) [ SNew(STextBlock).Text(LOCTEXT("NodeNodeId", "NodeId")) ]
			+ SGridPanel::Slot(1, 3).Padding(2)
			[
				SNew(SEditableTextBox)
				.Text_Lambda([this]() { return FText::FromName(NodeEditRow.NodeId); })
				.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type) { NodeEditRow.NodeId = FName(*T.ToString()); })
			]
			// ParentId
			+ SGridPanel::Slot(0, 4).Padding(2) [ SNew(STextBlock).Text(LOCTEXT("NodeParentId", "ParentId")) ]
			+ SGridPanel::Slot(1, 4).Padding(2)
			[
				MakeNameCombo(ParentIdOptions,
					[this](FName N) { NodeEditRow.ParentId = N; },
					[this]() { return ParentIdDisplayText(NodeEditRow.ParentId); },
					[]() { return true; },
					ParentIdCombo)
			]
			// AbilityTag
			+ SGridPanel::Slot(0, 5).Padding(2) [ SNew(STextBlock).Text(LOCTEXT("NodeAbilityTag", "AbilityTag")) ]
			+ SGridPanel::Slot(1, 5).Padding(2)
			[
				MakeNameCombo(NodeAbilityTagOptions,
					[this](FName N) { NodeEditRow.AbilityTag = FGameplayTag::RequestGameplayTag(N, false); },
					[this]() { return NodeEditRow.AbilityTag.IsValid() ? FText::FromName(NodeEditRow.AbilityTag.GetTagName()) : LOCTEXT("Pick", "선택..."); },
					[]() { return true; },
					NodeAbilityTagCombo)
			]
			// Cost
			+ SGridPanel::Slot(0, 6).Padding(2) [ SNew(STextBlock).Text(LOCTEXT("NodeCost", "Cost")) ]
			+ SGridPanel::Slot(1, 6).Padding(2)
			[
				SNew(SSpinBox<int32>)
				.MinValue(0)
				.Value_Lambda([this]() { return NodeEditRow.Cost; })
				.OnValueChanged_Lambda([this](int32 V) { NodeEditRow.Cost = V; })
			]
			// DisplayName
			+ SGridPanel::Slot(0, 7).Padding(2) [ SNew(STextBlock).Text(LOCTEXT("NodeDisplayName", "DisplayName")) ]
			+ SGridPanel::Slot(1, 7).Padding(2)
			[
				SNew(SEditableTextBox)
				.Text_Lambda([this]() { return NodeEditRow.DisplayName; })
				.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type) { NodeEditRow.DisplayName = T; })
			]
			// Desc
			+ SGridPanel::Slot(0, 8).Padding(2) [ SNew(STextBlock).Text(LOCTEXT("NodeDesc", "Desc")) ]
			+ SGridPanel::Slot(1, 8).Padding(2)
			[
				SNew(SBox).HeightOverride(60)
				[
					SNew(SMultiLineEditableTextBox)
					.AutoWrapText(true)
					.Text_Lambda([this]() { return NodeEditRow.Desc; })
					.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type) { NodeEditRow.Desc = T; })
				]
			]
			]
		]

		// ── 버튼 ──
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SNew(SHorizontalBox)
			// Effect 버튼
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[
				SNew(SButton)
				.Text(LOCTEXT("AddUpdate", "추가/수정"))
				.Visibility_Lambda([this]() { return bShowNodePanel ? EVisibility::Collapsed : EVisibility::Visible; })
				.OnClicked(this, &SOutlierUpgradeEffectToolWidget::OnAddRow)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[
				SNew(SButton)
				.Text(LOCTEXT("Delete", "삭제"))
				.Visibility_Lambda([this]() { return bShowNodePanel ? EVisibility::Collapsed : EVisibility::Visible; })
				.OnClicked(this, &SOutlierUpgradeEffectToolWidget::OnDeleteRow)
			]
			// Node 버튼
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[
				SNew(SButton)
				.Text(LOCTEXT("NodeAddUpdate", "필드 추가/수정"))
				.ToolTipText(LOCTEXT("NodeAddUpdateTip", "이름 입력칸이 비어있으면 선택된 Node 의 필드를 수정, 채워져 있으면 그 이름으로 새 Node 를 추가합니다. ( 리네임 아님 )"))
				.Visibility_Lambda([this]() { return bShowNodePanel ? EVisibility::Visible : EVisibility::Collapsed; })
				.OnClicked(this, &SOutlierUpgradeEffectToolWidget::OnAddOrUpdateNodeRow)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[
				SNew(SButton)
				.Text(LOCTEXT("NodeDelete", "삭제"))
				.Visibility_Lambda([this]() { return bShowNodePanel ? EVisibility::Visible : EVisibility::Collapsed; })
				.OnClicked(this, &SOutlierUpgradeEffectToolWidget::OnDeleteNodeRow)
			]
			// 공통 저장 ( Effect + 선택된 Node 테이블 둘 다 저장 + Reimport )
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
			Rows.Add(New);
		}
		Rows.Sort([](const FRowPtr& A, const FRowPtr& B) { return A->Name.Compare(B->Name) < 0; });
	}
	if (RowListView.IsValid())
	{
		RowListView->RequestListRefresh();
	}

	// 편집 중이던 Effect 행이 리로드된 데이터에도 여전히 있으면 최신값으로 다시 채운다.
	// ( 저장 직후 편집 패널이 저장 전 값을 계속 보여주지 않도록 )
	if (!EditRow.Name.IsNone())
	{
		for (const FRowPtr& R : Rows)
		{
			if (R.IsValid() && R->Name == EditRow.Name)
			{
				EditRow = *R;
				break;
			}
		}
	}

	SetStatus(FText::Format(LOCTEXT("Loaded", "{0}개 행 로드됨."), FText::AsNumber(Rows.Num())));
}

void SOutlierUpgradeEffectToolWidget::RefreshTargetTagOptions()
{
	FString Prefix = TargetTagPrefix(EditRow.EffectType);
	if (EditRow.EffectType == EOutlierUpgradeEffectType::FunctionOverride)
	{
		Prefix = FunctionOverrideTargetTagPrefix(GetSelectedNodeAbilityTag());
	}
	GatherTagsByPrefix(Prefix, TargetTagOptions);
}

void SOutlierUpgradeEffectToolWidget::ReloadRowsFromNodeTable()
{
	NodeRows.Reset();
	if (UDataTable* Table = NodeTable.Get())
	{
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			const FOutlierUpgradeNodeRow* Src = reinterpret_cast<const FOutlierUpgradeNodeRow*>(Pair.Value);
			if (!Src)
			{
				continue;
			}
			FNodeRowPtr New = MakeShared<FOutlierUpgradeNodeToolRow>();
			New->RowName = Pair.Key;
			New->Role = Src->Role;
			New->TreeId = Src->TreeId;
			New->NodeId = Src->NodeId;
			New->ParentId = Src->ParentId;
			New->AbilityTag = Src->AbilityTag;
			New->Cost = Src->Cost;
			New->DisplayName = Src->DisplayName;
			New->Desc = Src->Desc;
			NodeRows.Add(New);
		}
		NodeRows.Sort([](const FNodeRowPtr& A, const FNodeRowPtr& B) { return A->RowName.Compare(B->RowName) < 0; });
	}
	RefreshNodeRowNameOptions();
	RefreshParentIdOptions();

	// NodeListView 는 &NodeRows 를 그대로 들고 있으므로, 내용이 바뀐 뒤엔 명시적으로 새로고침해야
	// 리스트가 저장/Reimport 이전의 캐시된 화면을 계속 보여주지 않는다.
	if (NodeListView.IsValid())
	{
		NodeListView->RequestListRefresh();
	}

	// 편집 중이던 Node 가 리로드된 데이터에도 여전히 있으면 최신값으로 다시 채우고,
	// 없어졌으면( 리네임/삭제 등 ) 편집 필드를 비운다.
	if (!NodeEditRow.RowName.IsNone())
	{
		bool bStillExists = false;
		for (const FNodeRowPtr& N : NodeRows)
		{
			if (N.IsValid() && N->RowName == NodeEditRow.RowName)
			{
				NodeEditRow = *N;
				bStillExists = true;
				break;
			}
		}
		if (!bStillExists)
		{
			NodeEditRow = FOutlierUpgradeNodeToolRow();
		}
	}
}

// EditRow.NodeRowName 이 가리키는 Node 를 in-memory NodeRows 에서 찾는다.
// ( DataTable 을 직접 안 읽는 이유: 저장 전 리네임/편집 중인 값도 즉시 반영되어야 하므로 )
FGameplayTag SOutlierUpgradeEffectToolWidget::GetSelectedNodeAbilityTag() const
{
	for (const FNodeRowPtr& N : NodeRows)
	{
		if (N.IsValid() && N->RowName == EditRow.NodeRowName)
		{
			return N->AbilityTag;
		}
	}
	return FGameplayTag();
}

EOutlierUpgradeRole SOutlierUpgradeEffectToolWidget::GetSelectedNodeRole() const
{
	for (const FNodeRowPtr& N : NodeRows)
	{
		if (N.IsValid() && N->RowName == EditRow.NodeRowName)
		{
			return N->Role;
		}
	}
	return EOutlierUpgradeRole::None;
}

void SOutlierUpgradeEffectToolWidget::RefreshConfigFieldOptions()
{
	GatherConfigFieldNames(ConfigFieldOptions, GetSelectedNodeRole());

	// 새 목록에 없는 값이면( 예: Shooter 필드로 세팅된 채 Partner Node 로 바꾼 경우 ) 비워서
	// 존재하지 않는 필드 이름이 CSV 에 남지 않도록 한다.
	const bool bStillValid = ConfigFieldOptions.ContainsByPredicate(
		[this](const FNamePtr& N) { return N.IsValid() && *N == EditRow.ConfigField; });
	if (!EditRow.ConfigField.IsNone() && !bStillValid)
	{
		EditRow.ConfigField = NAME_None;
	}
}

void SOutlierUpgradeEffectToolWidget::RefreshNodeRowNameOptions()
{
	NodeRowNameOptions.Reset();
	TArray<FName> Names;
	for (const FNodeRowPtr& N : NodeRows)
	{
		if (N.IsValid())
		{
			Names.Add(N->RowName);
		}
	}
	Names.Sort(FNameLexicalLess());
	for (const FName& N : Names)
	{
		NodeRowNameOptions.Add(MakeShared<FName>(N));
	}
	if (NodeRowNameCombo.IsValid())
	{
		NodeRowNameCombo->RefreshOptions();
	}
}

void SOutlierUpgradeEffectToolWidget::RefreshParentIdOptions()
{
	ParentIdOptions.Reset();
	ParentIdOptions.Add(MakeShared<FName>(NAME_None)); // "(없음 / 루트)"

	TArray<FName> Names;
	for (const FNodeRowPtr& N : NodeRows)
	{
		// 자기 자신은 스스로의 부모가 될 수 없다.
		if (N.IsValid() && N->RowName != NodeEditRow.RowName)
		{
			Names.Add(N->RowName);
		}
	}
	Names.Sort(FNameLexicalLess());
	for (const FName& N : Names)
	{
		ParentIdOptions.Add(MakeShared<FName>(N));
	}
	if (ParentIdCombo.IsValid())
	{
		ParentIdCombo->RefreshOptions();
	}
}

void SOutlierUpgradeEffectToolWidget::RefreshNodeAbilityTagOptions()
{
	GatherTagsByPrefix(TEXT("Ability"), NodeAbilityTagOptions);
	if (NodeAbilityTagCombo.IsValid())
	{
		NodeAbilityTagCombo->RefreshOptions();
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

	// ConfigField 도 이 행이 물고 있는 Node 의 Role 기준으로 다시 계산한다.
	// 저장된 값이 그 Role 의 실제 struct 필드와 안 맞으면( 예: 예전에 CSV 를 손으로 잘못 쓴 경우 )
	// 여기서 자동으로 비워지고, 아래 ConfigField 콤보엔 "선택..." 으로 뜬다 - 그게 그 값이 원래부터
	// 게임에서 안 먹히던 깨진 값이었다는 신호다.
	RefreshConfigFieldOptions();
	if (ConfigFieldCombo.IsValid()) ConfigFieldCombo->RefreshOptions();
}

TSharedRef<ITableRow> SOutlierUpgradeEffectToolWidget::GenerateNodeRow(FNodeRowPtr Row, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SNodeTableRow, OwnerTable).Item(Row);
}

void SOutlierUpgradeEffectToolWidget::OnNodeRowSelected(FNodeRowPtr Row, ESelectInfo::Type SelectInfo)
{
	if (Row.IsValid())
	{
		PullSelectedNodeToEdit(*Row);
	}
}

void SOutlierUpgradeEffectToolWidget::PullSelectedNodeToEdit(const FOutlierUpgradeNodeToolRow& Row)
{
	NodeEditRow = Row;
	NodeRenameInputText.Empty();
	RefreshParentIdOptions(); // 자기 자신을 부모 후보에서 빼야 하므로 선택이 바뀔 때마다 다시 계산
}

void SOutlierUpgradeEffectToolWidget::SelectNodeInListByRowName(FName InRowName)
{
	if (InRowName.IsNone())
	{
		return;
	}
	for (const FNodeRowPtr& N : NodeRows)
	{
		if (N.IsValid() && N->RowName == InRowName)
		{
			PullSelectedNodeToEdit(*N);
			if (NodeListView.IsValid())
			{
				NodeListView->SetSelection(N);
				NodeListView->RequestScrollIntoView(N);
			}
			break;
		}
	}
}

FName SOutlierUpgradeEffectToolWidget::BuildAutoName() const
{
	FString NodePart = EditRow.NodeRowName.ToString();
	NodePart.RemoveFromStart(TEXT("Shooter_"));
	NodePart.RemoveFromStart(TEXT("Partner_"));

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
	// 종류에 안 쓰는 필드는 확실히 비워서 기록 ( 방어 )
	if (EditRow.EffectType != EOutlierUpgradeEffectType::AbilityConfig) { EditRow.ConfigField = NAME_None; }

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

// Node 패널에서 선택된 Node ( NodeEditRow ) 를 새 이름으로 바꾸고, 그 Node 를 참조하는 다른 Node 행의 ParentId,
// Effect 행의 NodeRowName 을 전부 같은 이름으로 치환한다. ( 전부 in-memory, 저장은 별도 )
FReply SOutlierUpgradeEffectToolWidget::OnRenameNodeRow()
{
	const FName OldName = NodeEditRow.RowName;
	const FString NewNameStr = NodeRenameInputText.TrimStartAndEnd();

	if (OldName.IsNone())
	{
		SetStatus(LOCTEXT("RenameNeedOld", "이름을 변경할 NodeRowName 을 먼저 선택하세요."));
		return FReply::Handled();
	}
	if (NewNameStr.IsEmpty())
	{
		SetStatus(LOCTEXT("RenameNeedNew", "새 이름을 입력하세요."));
		return FReply::Handled();
	}
	// Effect DataTable 이 로드되어 있지 않으면 Rows 가 비어있어서 "영향받는 Effect 0개"로 잘못 계산되고,
	// 실제로 그 Effect 들을 참조하는 NodeRowName 은 CSV에도 절대 반영되지 못한다. 그러니 미리 막는다.
	if (!EffectTable.IsValid())
	{
		SetStatus(LOCTEXT("RenameNeedEffectTable", "Effect DataTable 도 먼저 선택하세요. 그래야 연결된 Effect 행의 NodeRowName 도 함께 안전하게 바뀝니다."));
		return FReply::Handled();
	}
	const FName NewName(*NewNameStr);
	if (NewName == OldName)
	{
		SetStatus(LOCTEXT("RenameSame", "기존 이름과 같습니다."));
		return FReply::Handled();
	}
	for (const FNodeRowPtr& N : NodeRows)
	{
		if (N.IsValid() && N->RowName == NewName)
		{
			SetStatus(FText::Format(LOCTEXT("RenameDup", "이미 존재하는 Node 이름입니다: {0}"), FText::FromName(NewName)));
			return FReply::Handled();
		}
	}

	// 영향 범위 계산 ( 확인 팝업용 )
	int32 AffectedParents = 0;
	for (const FNodeRowPtr& N : NodeRows)
	{
		if (N.IsValid() && N->ParentId == OldName) { ++AffectedParents; }
	}
	int32 AffectedEffects = 0;
	for (const FRowPtr& R : Rows)
	{
		if (R.IsValid() && R->NodeRowName == OldName) { ++AffectedEffects; }
	}

	const FText Confirm = FText::Format(
		LOCTEXT("RenameConfirm", "'{0}' -> '{1}' 로 변경합니다.\nNode {2}개의 ParentId, Effect {3}개의 NodeRowName 이 함께 바뀝니다.\n계속할까요?"),
		FText::FromName(OldName), FText::FromName(NewName), FText::AsNumber(AffectedParents), FText::AsNumber(AffectedEffects));

	if (FMessageDialog::Open(EAppMsgType::YesNo, Confirm) != EAppReturnType::Yes)
	{
		SetStatus(LOCTEXT("RenameCancelled", "이름 변경을 취소했습니다."));
		return FReply::Handled();
	}

	// ── 적용 ( 메모리 상 ) ──
	for (FNodeRowPtr& N : NodeRows)
	{
		if (!N.IsValid()) { continue; }
		if (N->RowName == OldName) { N->RowName = NewName; }
		if (N->ParentId == OldName) { N->ParentId = NewName; }
	}
	for (FRowPtr& R : Rows)
	{
		if (R.IsValid() && R->NodeRowName == OldName) { R->NodeRowName = NewName; }
	}
	// Effect 패널에서 편집 중이던 값도 이 Node 를 물고 있었다면 함께 갱신.
	if (EditRow.NodeRowName == OldName) { EditRow.NodeRowName = NewName; }
	NodeEditRow.RowName = NewName;

	NodeRows.Sort([](const FNodeRowPtr& A, const FNodeRowPtr& B) { return A->RowName.Compare(B->RowName) < 0; });
	Rows.Sort([](const FRowPtr& A, const FRowPtr& B) { return A->Name.Compare(B->Name) < 0; });

	RefreshNodeRowNameOptions();
	RefreshParentIdOptions();
	if (RowListView.IsValid()) { RowListView->RequestListRefresh(); }
	if (NodeListView.IsValid()) { NodeListView->RequestListRefresh(); }
	NodeRenameInputText.Empty();

	SetStatus(FText::Format(
		LOCTEXT("RenameApplied", "'{0}' -> '{1}' 반영됨 (저장 전). Node {2}개, Effect {3}개 함께 변경됨."),
		FText::FromName(OldName), FText::FromName(NewName), FText::AsNumber(AffectedParents), FText::AsNumber(AffectedEffects)));
	return FReply::Handled();
}

// NodeEditRow.RowName 이 기존에 있으면 그 필드만 갱신, 없으면 새 Node 로 추가한다.
// ( 이름이 바뀌어도 ParentId/NodeRowName 을 자동으로 따라가지 않는다 - 그건 OnRenameNodeRow 의 역할 )
FReply SOutlierUpgradeEffectToolWidget::OnAddOrUpdateNodeRow()
{
	const FString TypedName = NodeRenameInputText.TrimStartAndEnd();
	const FName TargetRowName = TypedName.IsEmpty() ? NodeEditRow.RowName : FName(*TypedName);

	if (TargetRowName.IsNone())
	{
		SetStatus(LOCTEXT("NodeNeedName", "Node 이름을 입력하거나 기존 Node 를 먼저 선택하세요."));
		return FReply::Handled();
	}

	FNodeRowPtr Target;
	for (const FNodeRowPtr& N : NodeRows)
	{
		if (N.IsValid() && N->RowName == TargetRowName) { Target = N; break; }
	}

	const bool bIsNew = !Target.IsValid();
	if (bIsNew)
	{
		Target = MakeShared<FOutlierUpgradeNodeToolRow>();
		NodeRows.Add(Target);
	}

	*Target = NodeEditRow;
	Target->RowName = TargetRowName;

	NodeRows.Sort([](const FNodeRowPtr& A, const FNodeRowPtr& B) { return A->RowName.Compare(B->RowName) < 0; });
	RefreshNodeRowNameOptions();
	RefreshParentIdOptions();
	if (NodeListView.IsValid())
	{
		NodeListView->RequestListRefresh();
		NodeListView->SetSelection(Target);
	}
	NodeEditRow = *Target;
	NodeRenameInputText.Empty();

	SetStatus(FText::Format(
		bIsNew ? LOCTEXT("NodeAdded", "'{0}' Node 추가됨 (저장 전).") : LOCTEXT("NodeUpdated", "'{0}' Node 필드 수정됨 (저장 전)."),
		FText::FromName(TargetRowName)));
	return FReply::Handled();
}

FReply SOutlierUpgradeEffectToolWidget::OnDeleteNodeRow()
{
	const FName TargetName = NodeEditRow.RowName;
	if (TargetName.IsNone())
	{
		SetStatus(LOCTEXT("NodeDeleteNeedSelect", "삭제할 Node 를 먼저 선택하세요."));
		return FReply::Handled();
	}
	if (!EffectTable.IsValid())
	{
		SetStatus(LOCTEXT("NodeDeleteNeedEffectTable", "Effect DataTable 도 먼저 선택하세요. 그래야 이 Node 를 참조하는 Effect 가 있는지 정확히 확인할 수 있습니다."));
		return FReply::Handled();
	}

	// 이 Node 를 참조하는 Effect / 다른 Node 의 ParentId 가 있으면 경고만 하고, 실제 삭제는 그대로 진행한다.
	// ( 참조가 끊어진 채 남으므로, 필요하면 그 Effect/ParentId 는 사용자가 직접 정리해야 한다. )
	int32 AffectedParents = 0;
	for (const FNodeRowPtr& N : NodeRows)
	{
		if (N.IsValid() && N->ParentId == TargetName) { ++AffectedParents; }
	}
	int32 AffectedEffects = 0;
	for (const FRowPtr& R : Rows)
	{
		if (R.IsValid() && R->NodeRowName == TargetName) { ++AffectedEffects; }
	}
	if (AffectedParents > 0 || AffectedEffects > 0)
	{
		const FText Warn = FText::Format(
			LOCTEXT("NodeDeleteWarn", "'{0}' 을(를) 참조하는 Node {1}개(ParentId), Effect {2}개(NodeRowName)가 있습니다.\n삭제하면 참조가 끊어진 채로 남습니다. 계속할까요?"),
			FText::FromName(TargetName), FText::AsNumber(AffectedParents), FText::AsNumber(AffectedEffects));
		if (FMessageDialog::Open(EAppMsgType::YesNo, Warn) != EAppReturnType::Yes)
		{
			SetStatus(LOCTEXT("NodeDeleteCancelled", "삭제를 취소했습니다."));
			return FReply::Handled();
		}
	}

	NodeRows.RemoveAll([TargetName](const FNodeRowPtr& N) { return N.IsValid() && N->RowName == TargetName; });
	NodeEditRow = FOutlierUpgradeNodeToolRow();
	NodeRenameInputText.Empty();
	RefreshNodeRowNameOptions();
	RefreshParentIdOptions();
	if (NodeListView.IsValid()) { NodeListView->RequestListRefresh(); }
	SetStatus(FText::Format(LOCTEXT("NodeDeleted", "'{0}' Node 삭제됨 (저장 전)."), FText::FromName(TargetName)));
	return FReply::Handled();
}

FReply SOutlierUpgradeEffectToolWidget::OnSwitchToEffectPanel()
{
	bShowNodePanel = false;
	return FReply::Handled();
}

FReply SOutlierUpgradeEffectToolWidget::OnSwitchToNodePanel()
{
	bShowNodePanel = true;
	// Effect 쪽에서 지금 물고 있는 Node 가 있으면 그걸 바로 보여준다 ("bind 한 node도 나오게").
	SelectNodeInListByRowName(EditRow.NodeRowName);

	if (!NodeTable.IsValid())
	{
		SetStatus(LOCTEXT("NodePanelNoTable", "Node 패널로 전환됨. 위쪽에서 Node DataTable 을 먼저 선택하세요."));
	}
	else if (NodeRows.Num() == 0)
	{
		SetStatus(LOCTEXT("NodePanelEmpty", "Node 패널로 전환됨. 선택된 Node DataTable 에 행이 없습니다."));
	}
	else
	{
		SetStatus(FText::Format(LOCTEXT("NodePanelReady", "Node 패널로 전환됨. {0}개 Node 행."), FText::AsNumber(NodeRows.Num())));
	}
	return FReply::Handled();
}

bool SOutlierUpgradeEffectToolWidget::SaveEffectTableCsv(FString& OutSavedFileName)
{
	UDataTable* Table = EffectTable.Get();
	if (!Table)
	{
		SetStatus(LOCTEXT("NoTable", "Effect DataTable 을 먼저 선택하세요."));
		return false;
	}

	FString CsvPath;
	if (Table->AssetImportData)
	{
		CsvPath = Table->AssetImportData->GetFirstFilename();
	}
	if (CsvPath.IsEmpty() || !FPaths::FileExists(CsvPath))
	{
		SetStatus(LOCTEXT("NoCsv", "Effect DataTable 의 소스 CSV 경로를 찾지 못했습니다."));
		return false;
	}

	// CSV 문자열 빌드
	FString Csv = TEXT("Name,NodeRowName,EffectType,TargetTag,ConfigField,Op,Magnitude\r\n");
	for (const FRowPtr& R : Rows)
	{
		Csv += FString::Printf(TEXT("%s,%s,%s,%s,%s,%s,%s\r\n"),
			*R->Name.ToString(),
			*R->NodeRowName.ToString(),
			*EffectTypeEnumString(R->EffectType),
			*WrapTag(R->TargetTag),
			*NameOrEmpty(R->ConfigField),
			*OpEnumString(R->Op),
			*FString::SanitizeFloat(R->Magnitude));
	}

	if (!WriteUtf8CsvFile(CsvPath, Csv))
	{
		SetStatus(FText::Format(LOCTEXT("WriteFail", "CSV 쓰기 실패: {0}"), FText::FromString(CsvPath)));
		return false;
	}

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
	OutSavedFileName = FPaths::GetCleanFilename(CsvPath);
	return true;
}

bool SOutlierUpgradeEffectToolWidget::SaveNodeTableCsv(FString& OutSavedFileName)
{
	UDataTable* Table = NodeTable.Get();
	if (!Table)
	{
		SetStatus(LOCTEXT("NoNodeTable", "Node DataTable 을 먼저 선택하세요."));
		return false;
	}

	FString CsvPath;
	if (Table->AssetImportData)
	{
		CsvPath = Table->AssetImportData->GetFirstFilename();
	}
	if (CsvPath.IsEmpty() || !FPaths::FileExists(CsvPath))
	{
		SetStatus(LOCTEXT("NoNodeCsv", "Node DataTable 의 소스 CSV 경로를 찾지 못했습니다."));
		return false;
	}

	// CSV 문자열 빌드 ( DT_ShooterUpgradeNode.csv 등의 컬럼 순서와 동일하게 )
	FString Csv = TEXT("Name,Role,TreeId,NodeId,ParentId,AbilityTag,Cost,DisplayName,Desc\r\n");
	for (const FNodeRowPtr& N : NodeRows)
	{
		if (!N.IsValid()) { continue; }
		Csv += FString::Printf(TEXT("%s,%s,%s,%s,%s,%s,%d,%s,%s\r\n"),
			*N->RowName.ToString(),
			*RoleEnumString(N->Role),
			*NameOrEmpty(N->TreeId),
			*NameOrEmpty(N->NodeId),
			*NameOrEmpty(N->ParentId),
			*WrapTag(N->AbilityTag),
			N->Cost,
			*CsvField(N->DisplayName.ToString()),
			*CsvField(N->Desc.ToString()));
	}

	if (!WriteUtf8CsvFile(CsvPath, Csv))
	{
		SetStatus(FText::Format(LOCTEXT("WriteFail", "CSV 쓰기 실패: {0}"), FText::FromString(CsvPath)));
		return false;
	}

	FReimportManager::Instance()->Reimport(
		Table,
		/*bAskForNewFileIfMissing*/ false,
		/*bShowNotification*/ true,
		CsvPath,
		/*SpecifiedReimportHandler*/ nullptr,
		/*SourceFileIndex*/ INDEX_NONE,
		/*bForceNewFile*/ false,
		/*bAutomated*/ true);

	ReloadRowsFromNodeTable();
	OutSavedFileName = FPaths::GetCleanFilename(CsvPath);
	return true;
}

FReply SOutlierUpgradeEffectToolWidget::OnSaveAndReimport()
{
	// Effect 는 항상 저장 대상 ( 필수 ).
	FString EffectFileName;
	if (!SaveEffectTableCsv(EffectFileName))
	{
		return FReply::Handled(); // 실패 메시지는 SaveEffectTableCsv 안에서 이미 세팅됨
	}

	// Node 는 선택되어 있을 때만 같이 저장 ( 리네임으로 ParentId/NodeRowName 이 동기화된 것도 여기서 반영 ).
	FString NodeFileName;
	if (NodeTable.IsValid())
	{
		if (!SaveNodeTableCsv(NodeFileName))
		{
			SetStatus(FText::Format(LOCTEXT("SavedEffectNodeFail", "Effect 는 저장했지만 Node 저장 실패 - {0}"), StatusText));
			return FReply::Handled();
		}
	}

	if (!NodeFileName.IsEmpty())
	{
		SetStatus(FText::Format(LOCTEXT("SavedBoth", "저장 + Reimport 완료 ({0}, {1})."),
			FText::FromString(EffectFileName), FText::FromString(NodeFileName)));
	}
	else
	{
		SetStatus(FText::Format(LOCTEXT("Saved", "저장 + Reimport 완료 ({0})."), FText::FromString(EffectFileName)));
	}
	return FReply::Handled();
}

void SOutlierUpgradeEffectToolWidget::SetStatus(const FText& InText)
{
	StatusText = InText;
}

#undef LOCTEXT_NAMESPACE
