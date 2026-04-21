// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "VisualEventSubsystem.generated.h"


/**
 * 
 */

class UProjectionMarkDefinition;
class UTrailEffectDefinition;
class USoundDefinition;

UCLASS()
class VISUALEVENT_API UVisualEventSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
	
public:

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

public:


	UFUNCTION(BlueprintCallable, Category = "Visual Event")
	void SpawnMarkAtLocation(UProjectionMarkDefinition* Def, FVector Location, FRotator Rotation); 

	UFUNCTION(BlueprintCallable, Category = "Visual Event")
	void SpawnBeamTrail(const UTrailEffectDefinition* Def, const FVector& Start, const FVector& End);


	UFUNCTION(BlueprintCallable, Category = "Visual Event")
	void SpawnProjectileTrail(const UTrailEffectDefinition* Def, USceneComponent* AttachTarget);

	UFUNCTION(BlueprintCallable, Category = "Sound Event")
	void PlaySoundAtLocation(USoundDefinition* SoundDefinition, FVector Location);
};
