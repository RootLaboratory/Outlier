#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "OutlierEventSettings.generated.h"

class UOutlierEventDataAsset;

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Outlier Events"))
class OUTLIER_API UOutlierEventSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, Category = "Role Data Assets")
	TSoftObjectPtr<UOutlierEventDataAsset> ShooterDataAsset;

	UPROPERTY(Config, EditAnywhere, Category = "Role Data Assets")
	TSoftObjectPtr<UOutlierEventDataAsset> PartnerDataAsset;
};
