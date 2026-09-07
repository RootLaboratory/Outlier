#include "Input/OutlierInputBindingTable.h"

#include "EnhancedActionKeyMapping.h"
#include "InputAction.h"
#include "Input/OutlierInputBindingSettings.h"
#include "InputMappingContext.h"
#include "PlayerMappableKeySettings.h"
#include "UObject/UnrealType.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
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

TArray<FOutlierInputBindingTableRow> UOutlierInputBindingTable::GetVisibleRowsSorted() const
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

#if WITH_EDITOR
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
}

void UOutlierInputBindingTable::SortRowsByMappingContext()
{
	Modify();
	SortInputBindingRows(KeyboardRows);
	SortInputBindingRows(MouseRows);
	MarkPackageDirty();
}
#endif
