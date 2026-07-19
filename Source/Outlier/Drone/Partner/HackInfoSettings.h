#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "HackInfoSettings.generated.h"

class UDataTable;

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Hack Info Settings"))
class OUTLIER_API UHackInfoSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Hack|UI")
	TSoftObjectPtr<UDataTable> HackInfoTable;
};
