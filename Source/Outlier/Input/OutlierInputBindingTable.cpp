#include "Input/OutlierInputBindingTable.h"

#include "EnhancedActionKeyMapping.h"
#include "InputAction.h"
#include "Input/OutlierInputBindingSettings.h"
#include "InputMappingContext.h"
#include "PlayerMappableKeySettings.h"
#include "UObject/UnrealType.h"
#include "GameFramework/Character.h"
#include "OutlierPlayerState.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "Misc/DataValidation.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#endif

namespace
{
	FString MakeInputBindingRowKey(
		const UInputMappingContext* MappingContext,
		const UInputAction* InputAction,
		const FEnhancedActionKeyMapping& Mapping)
	{
		const FString ContextPath = MappingContext
			? MappingContext->GetPathName()
			: FString();
		const FString ActionPath = InputAction
			? InputAction->GetPathName()
			: FString();
		return FString::Printf(
			TEXT("%s|%s|%s"),
			*ContextPath,
			*ActionPath,
			*Mapping.Key.ToString());
	}

	FString MakeInputBindingRowKey(const FOutlierInputBindingTableRow& Row)
	{
		const FString ContextPath = Row.MappingContext.ToSoftObjectPath().ToString();
		const FString ActionPath = Row.InputAction
			? Row.InputAction->GetPathName()
			: FString();
		return FString::Printf(
			TEXT("%s|%s|%s"),
			*ContextPath,
			*ActionPath,
			*Row.DefaultKey.ToString());
	}

	const UPlayerMappableKeySettings* GetMappingOwnedMappableSettings(
		const FEnhancedActionKeyMapping& Mapping)
	{
		FObjectPropertyBase* SettingsProperty = CastField<FObjectPropertyBase>(
			FEnhancedActionKeyMapping::StaticStruct()->FindPropertyByName(
				TEXT("PlayerMappableKeySettings")));
		if (!SettingsProperty)
		{
			return nullptr;
		}

		const void* SettingsAddress =
			SettingsProperty->ContainerPtrToValuePtr<void>(&Mapping);
		return Cast<UPlayerMappableKeySettings>(
			SettingsProperty->GetObjectPropertyValue(SettingsAddress));
	}

	FName ResolveMappingName(const FEnhancedActionKeyMapping& Mapping)
	{
		const UPlayerMappableKeySettings* MappingOwnedSettings =
			GetMappingOwnedMappableSettings(Mapping);
		return MappingOwnedSettings
			? MappingOwnedSettings->Name
			: NAME_None;
	}

	FText ResolveDisplayName(
		const FEnhancedActionKeyMapping& Mapping,
		const UInputAction* InputAction)
	{
		const UPlayerMappableKeySettings* MappingOwnedSettings =
			GetMappingOwnedMappableSettings(Mapping);
		if (MappingOwnedSettings && !MappingOwnedSettings->DisplayName.IsEmpty())
		{
			return MappingOwnedSettings->DisplayName;
		}

		return InputAction
			? FText::FromString(InputAction->GetName())
			: FText::GetEmpty();
	}

	FText ResolveCategoryName(
		const FEnhancedActionKeyMapping& Mapping,
		const UInputMappingContext* MappingContext)
	{
		const UPlayerMappableKeySettings* MappingOwnedSettings =
			GetMappingOwnedMappableSettings(Mapping);
		if (MappingOwnedSettings && !MappingOwnedSettings->DisplayCategory.IsEmpty())
		{
			return MappingOwnedSettings->DisplayCategory;
		}

		return MappingContext
			? FText::FromString(MappingContext->GetName())
			: FText::GetEmpty();
	}

	FName ResolveConflictGroup(
		const UInputMappingContext* MappingContext,
		const UOutlierInputBindingSettings* InputBindingSettings)
	{
		if (!MappingContext || !InputBindingSettings)
		{
			return TEXT("Gameplay");
		}

		for (const FOutlierInputMappingContextConflictRule& ConflictRule
			: InputBindingSettings->MappingContextConflictRules)
		{
			if (ConflictRule.MappingContext.ToSoftObjectPath()
				== FSoftObjectPath(MappingContext))
			{
				return ConflictRule.ConflictGroup.IsNone()
					? TEXT("Gameplay")
					: ConflictRule.ConflictGroup;
			}
		}

		return TEXT("Gameplay");
	}

	void SortInputBindingRows(TArray<FOutlierInputBindingTableRow>& InRows)
	{
		InRows.Sort(
			[](const FOutlierInputBindingTableRow& Left,
				const FOutlierInputBindingTableRow& Right)
			{
				if (Left.MappingContextOrder != Right.MappingContextOrder)
				{
					return Left.MappingContextOrder < Right.MappingContextOrder;
				}

				if (Left.SortOrder != Right.SortOrder)
				{
					return Left.SortOrder < Right.SortOrder;
				}

				if (Left.SourceMappingIndex != Right.SourceMappingIndex)
				{
					return Left.SourceMappingIndex < Right.SourceMappingIndex;
				}

				return Left.DisplayName.ToString() < Right.DisplayName.ToString();
			});
	}
}

int32 FOutlierInputBindingTableRow::GetEffectiveDisplayScopes() const
{
	if (DisplayScopes != 0) return DisplayScopes;
	const UOutlierInputBindingSettings* Settings = GetDefault<UOutlierInputBindingSettings>();
	for (const FOutlierInputMappingContextDisplayRule& Rule : Settings->MappingContextDisplayRules)
	{
		if (!MappingContext.IsNull() && Rule.MappingContext.ToSoftObjectPath() == MappingContext.ToSoftObjectPath())
		{
			return Rule.DisplayScopes;
		}
	}
	return static_cast<int32>(EOutlierInputSettingScope::Common);
}

bool FOutlierInputBindingTableRow::IsVisibleForRole(EOutlierPlayerRole Role) const
{
	return UOutlierInputBindingTable::IsScopeVisibleForRole(GetEffectiveDisplayScopes(), Role);
}

bool UOutlierInputBindingTable::IsScopeVisibleForRole(int32 Scopes, EOutlierPlayerRole Role)
{
	const EOutlierInputSettingScope RoleScope = Role == EOutlierPlayerRole::Shooter
		? EOutlierInputSettingScope::Shooter : Role == EOutlierPlayerRole::Partner
		? EOutlierInputSettingScope::Partner : EOutlierInputSettingScope::BeforeRoleSelection;
	return (Scopes & static_cast<int32>(EOutlierInputSettingScope::Common | RoleScope)) != 0;
}

EOutlierInputBindingValidation FOutlierInputBindingTableRow::ValidateReplacementKey(FKey NewKey) const
{
	const bool bWheelDirection = NewKey == EKeys::MouseScrollUp || NewKey == EKeys::MouseScrollDown;
	if (!NewKey.IsValid() || NewKey == EKeys::AnyKey || NewKey.IsGamepadKey()
		|| !NewKey.IsBindableInBlueprints() || !DefaultKey.IsValid())
	{
		return EOutlierInputBindingValidation::InputRequired;
	}

	if (DefaultKey.IsAnalog())
	{
		// A wheel pulse is not a substitute for mouse movement, even though both can be Axis1D.
		const bool bMatchingAxis = (DefaultKey.IsAxis1D() && NewKey.IsAxis1D())
			|| (DefaultKey.IsAxis2D() && NewKey.IsAxis2D())
			|| (DefaultKey.IsAxis3D() && NewKey.IsAxis3D());
		return bMatchingAxis && NewKey != EKeys::MouseWheelAxis
			? EOutlierInputBindingValidation::Valid
			: EOutlierInputBindingValidation::AxisInputRequired;
	}

	if (NewKey.IsAnalog() || (bWheelDirection && !bAllowMouseWheel))
	{
		return EOutlierInputBindingValidation::InputRequired;
	}

	return EOutlierInputBindingValidation::Valid;
}

TArray<FOutlierInputBindingTableRow> UOutlierInputBindingTable::GetAllRowsSorted() const
{
	TArray<FOutlierInputBindingTableRow> VisibleRows;
	for (const FOutlierInputBindingTableRow& Row : KeyboardRows)
	{
		if (Row.bRebindable)
		{
			VisibleRows.Add(Row);
		}
	}

	for (const FOutlierInputBindingTableRow& Row : MouseRows)
	{
		if (Row.bRebindable)
		{
			VisibleRows.Add(Row);
		}
	}

	SortInputBindingRows(VisibleRows);

	return VisibleRows;
}

namespace
{
	bool IsSameBinding(const FOutlierInputBindingTableRow& Left, const FOutlierInputBindingTableRow& Right)
	{
		return Left.MappingContext == Right.MappingContext
			&& Left.MappingName == Right.MappingName && Left.Slot == Right.Slot;
	}

	bool SharesProfileSlot(const FOutlierInputBindingTableRow& Left, const FOutlierInputBindingTableRow& Right)
	{
		return Left.MappingName == Right.MappingName && Left.Slot == Right.Slot;
	}

	bool ReferencesBinding(const FOutlierLinkedInputMapping& Link, const FOutlierInputBindingTableRow& Row)
	{
		return Link.MappingContext == Row.MappingContext && Link.MappingName == Row.MappingName && Link.Slot == Row.Slot;
	}

	bool CollectLinkedRows(const TArray<FOutlierInputBindingTableRow>& AllRows,
		const FOutlierInputBindingTableRow& Root, TArray<FOutlierInputBindingTableRow>& OutRows, FString& OutError)
	{
		OutRows.Reset();
		OutError.Reset();
		OutRows.Add(Root);
		for (const FOutlierLinkedInputMapping& Link : Root.LinkedMappings)
		{
			const FOutlierInputBindingTableRow* Target = AllRows.FindByPredicate(
				[&Link](const auto& Row) { return ReferencesBinding(Link, Row); });
			if (!Target || IsSameBinding(Root, *Target) || !Target->LinkedMappings.IsEmpty()
				|| Target->InputAction != Link.InputAction || Target->DefaultKey != Link.DefaultKey
				|| Target->DefaultKey != Root.DefaultKey || Link.Slot >= EPlayerMappableKeySlot::Unspecified
				|| OutRows.ContainsByPredicate([Target](const auto& Row) { return IsSameBinding(Row, *Target); }))
			{
				OutError = TEXT("연결 대상 누락, 중복, 중첩 연결 또는 기본 키/Action 불일치입니다.");
				return false;
			}
			OutRows.Add(*Target);
		}
		return true;
	}
}

bool UOutlierInputBindingTable::ResolveLinkedRows(const FOutlierInputBindingTableRow& Representative,
	TArray<FOutlierInputBindingTableRow>& OutRows, FString& OutError) const
{
	const TArray<FOutlierInputBindingTableRow> AllRows = GetAllRowsSorted();
	const FOutlierInputBindingTableRow* Root = AllRows.FindByPredicate(
		[&Representative](const auto& Row) { return IsSameBinding(Row, Representative); });
	OutRows.Reset();
	OutError.Reset();
	if (!Root || Root->MappingName.IsNone() || !Root->InputAction || !Root->DefaultKey.IsValid()
		|| Root->MappingContext.IsNull() || Root->Slot >= EPlayerMappableKeySlot::Unspecified)
	{
		OutError = TEXT("대표 매핑 또는 슬롯이 유효하지 않습니다.");
		return false;
	}
	if (!CollectLinkedRows(AllRows, *Root, OutRows, OutError)) return false;
	if (Root->LinkedMappings.IsEmpty()) return true;
	for (const auto& Member : OutRows)
	{
		const UInputMappingContext* Source = Member.MappingContext.LoadSynchronous();
		if (!Source || !Source->GetMappings().ContainsByPredicate([&Member](const auto& Mapping)
			{
				return Mapping.IsPlayerMappable() && Mapping.GetMappingName() == Member.MappingName
					&& Mapping.Action == Member.InputAction && Mapping.Key == Member.DefaultKey;
			}))
		{
			OutError = TEXT("원본 IMC의 매핑이 변경되었습니다. 테이블을 재생성하고 연결을 확인해 주세요.");
			return false;
		}
		int32 Matches = 0;
		for (const auto& Candidate : AllRows)
		{
			if (IsSameBinding(Member, Candidate)) ++Matches;
			if (SharesProfileSlot(Member, Candidate)
				&& !OutRows.ContainsByPredicate([&Candidate](const auto& Included) { return IsSameBinding(Included, Candidate); }))
			{
				OutError = TEXT("같은 사용자 슬롯을 공유하는 다른 IMC 매핑도 연결 목록에 포함해야 합니다.");
				return false;
			}
		}
		if (Matches != 1)
		{
			OutError = TEXT("IMC/MappingName/슬롯으로 대상을 하나로 식별할 수 없습니다.");
			return false;
		}
	}

	// 다른 그룹이 같은 저장 슬롯을 소유하면 양쪽 그룹을 모두 무효화한다.
	// 실패한 그룹도 소유권 검사에 포함하여 검증 순서에 따라 숨기는 행이 달라지지 않게 한다.
	for (const auto& Other : AllRows)
	{
		if (Other.LinkedMappings.IsEmpty() || IsSameBinding(*Root, Other)) continue;
		for (const auto& Member : OutRows)
		{
			const bool bOtherOwnsMember = SharesProfileSlot(Other, Member)
				|| Other.LinkedMappings.ContainsByPredicate([&Member](const auto& Link)
				{
					return Link.MappingName == Member.MappingName && Link.Slot == Member.Slot;
				});
			if (bOtherOwnsMember)
			{
				OutError = TEXT("다른 연결 그룹이 같은 매핑 슬롯을 소유합니다.");
				return false;
			}
		}
	}
	return true;
}

TArray<FOutlierInputBindingTableRow> UOutlierInputBindingTable::GetVisibleRowsSorted() const
{
	const TArray<FOutlierInputBindingTableRow> AllRows = GetAllRowsSorted();
	TArray<FOutlierInputBindingTableRow> HiddenRows;
	for (const auto& Row : AllRows)
	{
		if (Row.LinkedMappings.IsEmpty()) continue;
		TArray<FOutlierInputBindingTableRow> Members;
		FString Error;
		if (!ResolveLinkedRows(Row, Members, Error)) continue;
		for (int32 Index = 1; Index < Members.Num(); ++Index) HiddenRows.Add(Members[Index]);
	}
	TArray<FOutlierInputBindingTableRow> VisibleRows = AllRows;
	VisibleRows.RemoveAll([&HiddenRows](const auto& Row)
	{
		return HiddenRows.ContainsByPredicate([&Row](const auto& Hidden) { return IsSameBinding(Row, Hidden); });
	});
	return VisibleRows;
}

#if WITH_EDITOR
void UOutlierInputBindingTable::LinkGameAndUIBindings()
{
	// 재생성 중간 상태가 실행 중인 사용자 프로필에 반영되지 않도록 PIE에서는 적용하지 않는다.
	if (GEngine)
	{
		for (const auto& WorldContext : GEngine->GetWorldContexts())
		{
			if (WorldContext.WorldType == EWorldType::PIE)
			{
				UE_LOG(LogTemp, Warning, TEXT("Stop PIE before applying the game/UI binding preset."));
				return;
			}
		}
	}
	const FSoftObjectPath GameContext(TEXT("/Game/Input/Contexts/IMC_FirstPerson.IMC_FirstPerson"));
	const FSoftObjectPath UIContextPath(TEXT("/Game/Input/Contexts/IMC_Widget.IMC_Widget"));
	UInputMappingContext* UIContext = Cast<UInputMappingContext>(UIContextPath.TryLoad());
	if (!UIContext) return;

	struct FBindingPair
	{
		FName GameName;
		FName UIActionName;
		FName UIName;
		FKey Key;
		int32 UIIndex = INDEX_NONE;
	};
	FBindingPair Pairs[] = {
		{ TEXT("IA_Move_W"), TEXT("IA_WidgetUp"), TEXT("IA_WidgetUp_W"), EKeys::W },
		{ TEXT("IA_Move_A"), TEXT("IA_WidgetLeft"), TEXT("IA_WidgetLeft_A"), EKeys::A },
		{ TEXT("IA_Move_S"), TEXT("IA_WidgetDown"), TEXT("IA_WidgetDown_S"), EKeys::S },
		{ TEXT("IA_Move_D"), TEXT("IA_WidgetRight"), TEXT("IA_WidgetRight_D"), EKeys::D },
		{ TEXT("IA_Interaction"), TEXT("IA_WidgetConfirm"), TEXT("IA_WidgetConfirm"), EKeys::F },
		{ TEXT("IA_IngameSetting"), TEXT("IA_Escape"), TEXT("IA_Escape"), EKeys::Escape }
	};

	// 1. 실제 사용 IMC의 정확한 Action/기본 키 쌍을 모두 찾은 뒤 수정한다.
	// 같은 UI Action의 방향키 매핑은 대상에 포함하지 않는다.
	for (auto& Pair : Pairs)
	{
		const auto* Game = KeyboardRows.FindByPredicate([&](const auto& Row)
		{
			return Row.MappingContext.ToSoftObjectPath() == GameContext && Row.MappingName == Pair.GameName
				&& Row.DefaultKey == Pair.Key && Row.Slot == EPlayerMappableKeySlot::First && Row.bRebindable;
		});
		int32 Matches = 0;
		for (int32 Index = 0; Index < UIContext->GetMappings().Num(); ++Index)
		{
			const auto& Mapping = UIContext->GetMappings()[Index];
			if (Mapping.Action && Mapping.Action->GetPathName() == FString::Printf(
				TEXT("/Game/Input/Actions/FirstPerson/%s.%s"), *Pair.UIActionName.ToString(), *Pair.UIActionName.ToString())
				&& Mapping.Key == Pair.Key)
			{
				Pair.UIIndex = Index;
				++Matches;
			}
		}
		if (!Game || Matches != 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("Game/UI preset: expected one mapping pair for %s."), *Pair.GameName.ToString());
			return;
		}
	}
	FObjectPropertyBase* SettingsProperty = FindFProperty<FObjectPropertyBase>(
		FEnhancedActionKeyMapping::StaticStruct(), TEXT("PlayerMappableKeySettings"));
	FProperty* BehaviorProperty = FEnhancedActionKeyMapping::StaticStruct()->FindPropertyByName(TEXT("SettingBehavior"));
	FEnumProperty* EnumProperty = CastField<FEnumProperty>(BehaviorProperty);
	FByteProperty* ByteProperty = CastField<FByteProperty>(BehaviorProperty);
	if (!SettingsProperty || (!EnumProperty && !ByteProperty)) return;

	const auto PreviousMappings = UIContext->GetMappings();
	const auto PreviousKeyboardRows = KeyboardRows;
	const auto PreviousMouseRows = MouseRows;
	Modify();
	UIContext->Modify();

	// 2. UI의 여섯 매핑만 재설정 대상으로 등록한다. 이미 부여된 MappingName은 보존한다.
	for (auto& Pair : Pairs)
	{
		auto& Mapping = UIContext->GetMapping(Pair.UIIndex);
		const auto* Existing = GetMappingOwnedMappableSettings(Mapping);
		if (Existing && !Existing->Name.IsNone()) Pair.UIName = Existing->Name;
		else
		{
			auto* Settings = NewObject<UPlayerMappableKeySettings>(UIContext, NAME_None, RF_Transactional);
			Settings->Name = Pair.UIName;
			Settings->DisplayName = FText::FromName(Pair.UIActionName);
			SettingsProperty->SetObjectPropertyValue_InContainer(&Mapping, Settings);
		}
		if (EnumProperty)
		{
			EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(
				EnumProperty->ContainerPtrToValuePtr<void>(&Mapping),
				static_cast<int64>(EPlayerMappableKeySettingBehaviors::OverrideSettings));
		}
		else ByteProperty->SetPropertyValue_InContainer(&Mapping,
			static_cast<uint8>(EPlayerMappableKeySettingBehaviors::OverrideSettings));
	}
	RebuildRowsFromConfiguredIMCs();

	// 3. 재생성된 원본 행을 연결하고 전부 검증한다. 한 쌍이라도 실패하면 전체를 복구한다.
	FString Error;
	for (const auto& Pair : Pairs)
	{
		auto* Game = KeyboardRows.FindByPredicate([&](const auto& Row)
		{
			return Row.MappingContext.ToSoftObjectPath() == GameContext && Row.MappingName == Pair.GameName
				&& Row.Slot == EPlayerMappableKeySlot::First;
		});
		const auto* UI = KeyboardRows.FindByPredicate([&](const auto& Row)
		{
			return Row.MappingContext.ToSoftObjectPath() == UIContextPath && Row.MappingName == Pair.UIName
				&& Row.DefaultKey == Pair.Key && Row.Slot == EPlayerMappableKeySlot::First;
		});
		if (!Game || !UI)
		{
			Error = TEXT("재생성된 게임/UI 행을 찾지 못했습니다. IMC 스캔 설정을 확인해 주세요.");
			break;
		}
		if (!Game->LinkedMappings.ContainsByPredicate([UI](const auto& Link) { return ReferencesBinding(Link, *UI); }))
		{
			FOutlierLinkedInputMapping Link;
			Link.MappingContext = UI->MappingContext;
			Link.MappingName = UI->MappingName;
			Link.Slot = UI->Slot;
			Link.InputAction = UI->InputAction;
			Link.DefaultKey = UI->DefaultKey;
			Game->LinkedMappings.Add(Link);
		}
		TArray<FOutlierInputBindingTableRow> Members;
		if (!ResolveLinkedRows(*Game, Members, Error)) break;
	}
	if (!Error.IsEmpty())
	{
		for (int32 Index = 0; Index < PreviousMappings.Num(); ++Index) UIContext->GetMapping(Index) = PreviousMappings[Index];
		KeyboardRows = PreviousKeyboardRows;
		MouseRows = PreviousMouseRows;
		UE_LOG(LogTemp, Warning, TEXT("Game/UI preset rolled back: %s"), *Error);
		OnRowsChanged.Broadcast();
		return;
	}
	UIContext->MarkPackageDirty();
	MarkPackageDirty();
	OnRowsChanged.Broadcast();
	UE_LOG(LogTemp, Display, TEXT("Game/UI bindings linked: WASD, F, Escape. Save IMC_Widget and the binding table."));
}

EDataValidationResult UOutlierInputBindingTable::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult ParentResult = Super::IsDataValid(Context);
	bool bValid = ParentResult != EDataValidationResult::Invalid;
	for (const auto& Row : GetAllRowsSorted())
	{
		if (Row.LinkedMappings.IsEmpty()) continue;
		TArray<FOutlierInputBindingTableRow> Members;
		FString Error;
		if (!ResolveLinkedRows(Row, Members, Error))
		{
			Context.AddError(FText::FromString(FString::Printf(TEXT("%s: %s"), *Row.MappingName.ToString(), *Error)));
			bValid = false;
		}
	}
	return bValid ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}

void UOutlierInputBindingTable::RebuildRowsFromConfiguredIMCs()
{
	TArray<UInputMappingContext*> MappingContexts;

	const UOutlierInputBindingSettings* InputBindingSettings =
		GetDefault<UOutlierInputBindingSettings>();
	if (InputBindingSettings)
	{
		for (const TSoftObjectPtr<UInputMappingContext>& ContextPtr
			: InputBindingSettings->InputMappingContextsToScan)
		{
			if (UInputMappingContext* MappingContext = ContextPtr.LoadSynchronous())
			{
				MappingContexts.AddUnique(MappingContext);
			}
		}

		const FString DirectoryPath = InputBindingSettings->ContentInputDirectory.Path;
		if (!DirectoryPath.IsEmpty() && FPackageName::IsValidLongPackageName(DirectoryPath))
		{
			FAssetRegistryModule& AssetRegistryModule =
				FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

			TArray<FAssetData> ContextAssets;
			AssetRegistry.GetAssetsByPath(
				FName(*DirectoryPath),
				ContextAssets,
				true);

			for (const FAssetData& ContextAsset : ContextAssets)
			{
				if (UInputMappingContext* MappingContext =
					Cast<UInputMappingContext>(ContextAsset.GetAsset()))
				{
					MappingContexts.AddUnique(MappingContext);
				}
			}
		}
	}

	MappingContexts.Sort(
		[](const UInputMappingContext& Left, const UInputMappingContext& Right)
		{
			return Left.GetPathName() < Right.GetPathName();
		});

	TMap<FString, FOutlierInputBindingTableRow> ExistingRowsByKey;
	for (const FOutlierInputBindingTableRow& ExistingRow : KeyboardRows)
	{
		ExistingRowsByKey.Add(MakeInputBindingRowKey(ExistingRow), ExistingRow);
	}
	for (const FOutlierInputBindingTableRow& ExistingRow : MouseRows)
	{
		ExistingRowsByKey.Add(MakeInputBindingRowKey(ExistingRow), ExistingRow);
	}

	TArray<FOutlierInputBindingTableRow> RebuiltKeyboardRows;
	TArray<FOutlierInputBindingTableRow> RebuiltMouseRows;
	for (int32 ContextIndex = 0; ContextIndex < MappingContexts.Num(); ++ContextIndex)
	{
		UInputMappingContext* MappingContext = MappingContexts[ContextIndex];
		if (!MappingContext)
		{
			continue;
		}

		const TArray<FEnhancedActionKeyMapping>& Mappings =
			MappingContext->GetMappings();
		for (int32 MappingIndex = 0; MappingIndex < Mappings.Num(); ++MappingIndex)
		{
			const FEnhancedActionKeyMapping& Mapping = Mappings[MappingIndex];
			const UInputAction* InputAction = Mapping.Action.Get();
			if (!InputAction || !Mapping.Key.IsValid())
			{
				continue;
			}

			// Gamepad mappings have no home in this table (Keyboard/Mouse only).
			if (Mapping.Key.IsGamepadKey())
			{
				continue;
			}

			// A mapping is only pulled in once it's been made mappable/rebindable
			// on the IMC side (i.e. the Outlier Input Mappable Tool's "Use" was
			// applied to it, giving it a Name there).
			const FName MappingName = ResolveMappingName(Mapping);
			if (MappingName.IsNone())
			{
				continue;
			}

			FOutlierInputBindingTableRow NewRow;
			NewRow.InputAction = const_cast<UInputAction*>(InputAction);
			NewRow.DisplayName = ResolveDisplayName(Mapping, InputAction);
			NewRow.CategoryName = ResolveCategoryName(Mapping, MappingContext);
			NewRow.DefaultKey = Mapping.Key;
			NewRow.MappingName = MappingName;
			NewRow.MappingContext = MappingContext;
			NewRow.MappingContextName = FText::FromString(MappingContext->GetName());
			NewRow.ConflictGroup =
				ResolveConflictGroup(MappingContext, InputBindingSettings);
			NewRow.SortOrder = ContextIndex * 1000 + MappingIndex;
			NewRow.MappingContextOrder = ContextIndex;
			NewRow.SourceMappingIndex = MappingIndex;

			const FString RowKey =
				MakeInputBindingRowKey(MappingContext, InputAction, Mapping);
			if (const FOutlierInputBindingTableRow* ExistingRow =
				ExistingRowsByKey.Find(RowKey))
			{
				NewRow.SortOrder = ExistingRow->SortOrder;
				NewRow.bRebindable = ExistingRow->bRebindable;
				NewRow.bAllowMouseWheel = ExistingRow->bAllowMouseWheel;
				NewRow.DisplayScopes = ExistingRow->DisplayScopes;
				NewRow.LinkedMappings = ExistingRow->LinkedMappings;
				NewRow.Slot = ExistingRow->Slot;
			}

			if (Mapping.Key.IsMouseButton())
			{
				RebuiltMouseRows.Add(MoveTemp(NewRow));
			}
			else
			{
				RebuiltKeyboardRows.Add(MoveTemp(NewRow));
			}
		}
	}

	KeyboardRows = MoveTemp(RebuiltKeyboardRows);
	MouseRows = MoveTemp(RebuiltMouseRows);
	SortInputBindingRows(KeyboardRows);
	SortInputBindingRows(MouseRows);
	Modify();
	MarkPackageDirty();
	OnRowsChanged.Broadcast();
}

void UOutlierInputBindingTable::SortRowsByMappingContext()
{
	Modify();
	SortInputBindingRows(KeyboardRows);
	SortInputBindingRows(MouseRows);
	MarkPackageDirty();
	OnRowsChanged.Broadcast();
}

void UOutlierInputBindingTable::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	OnRowsChanged.Broadcast();
}
#endif
