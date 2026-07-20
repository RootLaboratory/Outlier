#include "Drone/Partner/HackInfoSubsystem.h"

#include "Drone/Partner/HackInfoSettings.h"
#include "Engine/DataTable.h"

bool UHackInfoSubsystem::TryGetHackInfo(const FGameplayTag& HackInfoTag, FHackInfoRow& OutInfo) const
{
	if (!HackInfoTag.IsValid())
	{
		return false;
	}

	const UHackInfoSettings* Settings = GetDefault<UHackInfoSettings>();
	if (!Settings)
	{
		return false;
	}

	UDataTable* HackInfoTable = Settings->HackInfoTable.LoadSynchronous();
	if (!HackInfoTable)
	{
		return false;
	}

	const FHackInfoRow* Row = HackInfoTable->FindRow<FHackInfoRow>(
		HackInfoTag.GetTagName(),
		TEXT("TryGetHackInfo")
	);

	if (!Row)
	{
		return false;
	}

	OutInfo = *Row;
	return true;
}
