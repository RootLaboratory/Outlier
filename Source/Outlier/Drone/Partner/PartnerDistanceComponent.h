// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Drone/Partner/PartnerCharacterComponentBase.h"
#include "PartnerDistanceComponent.generated.h"

/**
 * 
 */

class ULocalPlayerUISubSystem;

UCLASS()
class OUTLIER_API UPartnerDistanceComponent : public UPartnerCharacterComponentBase
{
	GENERATED_BODY()

public:
	UPartnerDistanceComponent();

	// Each transition has its own cooldown. Suppressed sounds are never queued.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boundary|Audio", meta = (ClampMin = "0.0", Units = "s"))
	float ConnectionAudioCooldown = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boundary|Audio", meta = (ClampMin = "0.0", Units = "s"))
	float DisconnectionAudioCooldown = 3.0f;

	void UpdateBoundaryState();
	void RefreshActivationFromPlayerState();

	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction
	) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	float CalculateDistance() const;
	void CacheUISubsystem();
	void PlayBoundaryTransitionAudio(bool bOutside);

	FTimerHandle ConnectionAudioCooldownTimer;
	FTimerHandle DisconnectionAudioCooldownTimer;

private:
	UPROPERTY(Transient)
	TObjectPtr<ULocalPlayerUISubSystem> CachedSubsystem;
};
