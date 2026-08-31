#include "Upgrade/OutlierUpgradeSetData.h"

#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#endif

TArray<FName> UOutlierUpgradeSetData::GetApplyEffectKeyOptions()
{
	TArray<FName> Keys;

#if WITH_EDITOR
	IAssetRegistry& AssetRegistry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssetsByClass(UOutlierUpgradeSetData::StaticClass()->GetClassPathName(), Assets);

	for (const FAssetData& AssetData : Assets)
	{
		const UOutlierUpgradeSetData* SetData = Cast<UOutlierUpgradeSetData>(AssetData.GetAsset());
		if (!SetData)
		{
			continue;
		}

		for (const TPair<FName, TSubclassOf<UGameplayEffect>>& Pair : SetData->ApplyEffectClasses)
		{
			if (!Pair.Key.IsNone())
			{
				Keys.AddUnique(Pair.Key);
			}
		}
	}

	Keys.Sort(FNameLexicalLess());
#endif

	return Keys;
}

void UOutlierUpgradeSetData::RefreshUnlockedNodeTextureBindings()
{
	TMap<FName, TObjectPtr<UTexture2D>> ExistingTextures;
	for (const FUpgradeNodeTextureBinding& Binding : UnlockedNodeTextures)
	{
		if (!Binding.NodeRowName.IsNone() && Binding.Texture)
		{
			ExistingTextures.Add(Binding.NodeRowName, Binding.Texture);
		}
	}

	UnlockedNodeTextures.Reset();

	if (!UpgradeDataTable)
	{
		return;
	}

	TArray<FName> RowNames = UpgradeDataTable->GetRowNames();
	RowNames.Sort(FNameLexicalLess());

	for (const FName& RowName : RowNames)
	{
		const FOutlierUpgradeNodeRow* Row = UpgradeDataTable->FindRow<FOutlierUpgradeNodeRow>(
			RowName,
			TEXT("OutlierUpgradeSetDataTextureBindings"),
			false);
		if (!Row)
		{
			continue;
		}

		if (UpgradeRole != EOutlierUpgradeRole::None && Row->Role != UpgradeRole)
		{
			continue;
		}

		FUpgradeNodeTextureBinding& Binding = UnlockedNodeTextures.AddDefaulted_GetRef();
		Binding.NodeRowName = RowName;

		if (const TObjectPtr<UTexture2D>* ExistingTexture = ExistingTextures.Find(RowName))
		{
			Binding.Texture = *ExistingTexture;
		}
	}
}

#if WITH_EDITOR
void UOutlierUpgradeSetData::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UOutlierUpgradeSetData, UpgradeDataTable)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UOutlierUpgradeSetData, UpgradeRole))
	{
		RefreshUnlockedNodeTextureBindings();
	}
}
#endif
