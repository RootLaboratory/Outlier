// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "VisualEventSubsystem.generated.h"


/**
 * 
 */

// 특정 동작에 부가적으로 생성되는 이펙트 및 Decal을 생성하는 클래스;


class UProjectionMarkDefinition;
class UTrailEffectDefinition;
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


	UFUNCTION(BlueprintCallable, Category = "Visual Event")
	void SpawnMarkFromHit(UProjectionMarkDefinition* Definition, const FHitResult& HitResult);

	UFUNCTION(BlueprintCallable, Category = "Visual Event")
	void PlaySoundAtLocation(UProjectionMarkDefinition* Definition, FVector Location);


};
