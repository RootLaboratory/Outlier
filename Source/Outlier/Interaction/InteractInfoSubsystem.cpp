#include "Interaction/InteractInfoSubsystem.h"

#include "Engine/DataTable.h"
#include "Interaction/InteractInfoSettings.h"

bool UInteractInfoSubsystem::TryGetInteractInfo(const FGameplayTag& InteractTag, FInteractInfoRow& OutInfo) const
{
	if (!InteractTag.IsValid())
	{
		return false;
	}

	const UInteractInfoSettings* Settings = GetDefault<UInteractInfoSettings>();
	if (!Settings)
	{
		return false;
	}

	UDataTable* InteractInfoTable = Settings->InteractInfoTable.LoadSynchronous();
	if (!InteractInfoTable)
	{
		return false;
	}

	const FInteractInfoRow* Row = InteractInfoTable->FindRow<FInteractInfoRow>(
		InteractTag.GetTagName(),
		TEXT("TryGetInteractInfo")
	);

	if (!Row)
	{
		return false;
	}

	OutInfo = *Row;
	return true;
}
