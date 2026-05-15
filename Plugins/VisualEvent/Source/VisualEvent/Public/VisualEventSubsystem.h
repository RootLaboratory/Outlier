// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "VisualEventType.h"
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


	UFUNCTION(BlueprintCallable, Category = "Set Event")
	void FeaturesEffect(FVector Location, FRotator Rotation, FVisualEventSet& EffectSet);

	UFUNCTION(BlueprintCallable, Category = "Visual Event")

	void SpawnMuzzleEffect(const UTrailEffectDefinition* Def, const FVector& Location, const FRotator& Rotation);

	//일단 Trail으로 받지만 따로 분리할 것임.
	UFUNCTION(BlueprintCallable, Category = "Visual Event")
	void SpawnEffectAtLocation(const UTrailEffectDefinition* Def, const FVector& Location, const FRotator& Rotation);



};
