// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TrailEffectDefinition.generated.h"

class UFXSystemAsset;


// 추후에 분리시킬 예정
// Trail이나 Projectile 형태의 이펙트 및 나이아가라 Wrapping 

/**
 * 
 */
UCLASS(Blueprintable)
class VISUALEVENT_API UTrailEffectDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FX")
	TObjectPtr<UFXSystemAsset> FXAsset;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FX")
	float LifeSpan = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FX")
	FVector Scale = FVector(1.f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FX")
	FRotator RotationOffset = FRotator::ZeroRotator;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Beam")
	FName StartParameterName = TEXT("Start");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Beam")
	FName EndParameterName = TEXT("End");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Beam")
	FVector StartOffset = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Beam")
	FVector EndOffset = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile")
	FName AttachSocketName = NAME_None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile")
	FVector RelativeLocation = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile")
	FRotator RelativeRotation = FRotator::ZeroRotator;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile")
	uint8 bAttachToSource : 1 = true;


	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Trail")
	float Speed = 300.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Trail")
	FName DirectionParameterName = TEXT("User.Direction");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Trail")
	FName SpeedParameterName = TEXT("User.Speed");
};
