// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "VisualEventSubsystem.generated.h"


/**
 * 
 */

class UProjectionMarkDefinition;

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
	void SpawnMarkFromHit(UProjectionMarkDefinition* Definition, const FHitResult& HitResult);

	UFUNCTION(BlueprintCallable, Category = "Visual Event")
	void SpawnFXAtLocation(UProjectionMarkDefinition* Definition, FVector Location, FRotator Rotation);

	UFUNCTION(BlueprintCallable, Category = "Visual Event")
	void PlaySoundAtLocation(UProjectionMarkDefinition* Definition, FVector Location);


};
