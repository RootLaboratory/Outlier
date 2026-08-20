#include "Upgrade/OutlierUpgradeSetData.h"

#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"

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
