#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "OutlierUIAudioSettings.generated.h"

/**
 * Audio tags used by native UI input handling.
 *
 * Values are configured in DefaultGame.ini and must reference tags registered by
 * Config/Tags/AudioTags.ini.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Outlier UI Audio"))
class OUTLIER_API UOutlierUIAudioSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, Category = "Common", meta = (Categories = "Audio.Type"))
	FGameplayTag UITypeTag;

	UPROPERTY(Config, EditAnywhere, Category = "Widget", meta = (Categories = "Audio.Context"))
	FGameplayTag WidgetEscape;
};
