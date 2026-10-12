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

	/** A subtitle whose voice cannot play stays for (non-whitespace characters x this), clamped below. */
	UPROPERTY(Config, EditAnywhere, Category = "Subtitle Fallback", meta = (ClampMin = "0.01"))
	float FallbackSecondsPerCharacter = 0.15f;

	UPROPERTY(Config, EditAnywhere, Category = "Subtitle Fallback", meta = (ClampMin = "0.1"))
	float FallbackMinSeconds = 2.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Subtitle Fallback", meta = (ClampMin = "0.1"))
	float FallbackMaxSeconds = 8.0f;
};
