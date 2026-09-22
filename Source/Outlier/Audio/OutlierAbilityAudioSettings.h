#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "OutlierAbilityAudioSettings.generated.h"

/**
 * Audio tags used by native gameplay abilities.
 *
 * Values are configured in DefaultGame.ini and must reference tags registered by
 * Config/Tags/AudioTags.ini. Gameplay code intentionally does not construct tags from strings.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Outlier Ability Audio"))
class OUTLIER_API UOutlierAbilityAudioSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Common", meta = (Categories = "Audio.Type"))
	FGameplayTag PlayerTypeTag;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Shooter", meta = (Categories = "Audio.Context"))
	FGameplayTag ShooterQuantumLeap;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Shooter", meta = (Categories = "Audio.Context"))
	FGameplayTag ShooterReflectionOn;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Shooter", meta = (Categories = "Audio.Context"))
	FGameplayTag ShooterReflectionLoop;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Shooter", meta = (Categories = "Audio.Context"))
	FGameplayTag ShooterReflectionOff;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Shooter", meta = (Categories = "Audio.Context"))
	FGameplayTag ShooterOverchargeOn;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Shooter", meta = (Categories = "Audio.Context"))
	FGameplayTag ShooterOverchargeLoop;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Shooter", meta = (Categories = "Audio.Context"))
	FGameplayTag ShooterOverchargeOff;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Shooter", meta = (Categories = "Audio.Context"))
	FGameplayTag ShooterStealthOn;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Shooter", meta = (Categories = "Audio.Context"))
	FGameplayTag ShooterStealthOff;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Partner", meta = (Categories = "Audio.Context"))
	FGameplayTag PartnerEMPCharge;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Partner", meta = (Categories = "Audio.Context"))
	FGameplayTag PartnerEMPBurst;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Partner", meta = (Categories = "Audio.Context"))
	FGameplayTag PartnerEMPFail;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Partner", meta = (Categories = "Audio.Context"))
	FGameplayTag PartnerShield;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Partner", meta = (Categories = "Audio.Context"))
	FGameplayTag PartnerHackTryLoop;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Partner", meta = (Categories = "Audio.Context"))
	FGameplayTag PartnerHackSuccess;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Partner", meta = (Categories = "Audio.Context"))
	FGameplayTag PartnerHackFail;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Partner", meta = (Categories = "Audio.Context"))
	FGameplayTag PartnerScan;
};
