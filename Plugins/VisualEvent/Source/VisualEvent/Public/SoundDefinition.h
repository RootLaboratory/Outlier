// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SoundDefinition.generated.h"

/**
 * 
 */

class USoundBase;
class USoundAttenuation;
class USoundConcurrency;

UCLASS(Blueprintable)
class VISUALEVENT_API USoundDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sound")
	TObjectPtr<USoundBase> Sound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sound")
	float VolumeMultiplier = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sound")
	float PitchMultiplier = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sound")
	float StartTime = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sound")
	TObjectPtr<USoundAttenuation> AttenuationSettings;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sound")
	TObjectPtr<USoundConcurrency> ConcurrencySettings;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Location")
	FVector LocationOffset = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attach")
	FName AttachSocketName = NAME_None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attach")
	FVector RelativeLocation = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attach")
	FRotator RelativeRotation = FRotator::ZeroRotator;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Policy")
	uint8 bAutoDestroy : 1 = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Policy")
	uint8 bStopWhenAttachedToDestroyed : 1 = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Policy")
	uint8 bIsUISound : 1 = false;
	
	
	
};
