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

	void UpdateBoundaryState();

	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction
	) override;

protected:
	virtual void BeginPlay() override;

private:
	float CalculateDistance() const;
	void CacheUISubsystem();

private:
	UPROPERTY(Transient)
	TObjectPtr<ULocalPlayerUISubSystem> CachedSubsystem;
};
