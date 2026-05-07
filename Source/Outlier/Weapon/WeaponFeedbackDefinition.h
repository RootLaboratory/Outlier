// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WeaponFeedbackDefinition.generated.h"

class UProjectionMarkDefinition;
class UTrailEffectDefinition;
class USoundDefinition;

/**
 * 
 */
UCLASS(BlueprintType)
class OUTLIER_API UWeaponFeedbackDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Feedback)
	TObjectPtr<UProjectionMarkDefinition> WeaponDecal;					// 탄흔

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Feedback)
	TObjectPtr<UTrailEffectDefinition> WeaponMuzzle;					// 총구 이펙트

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Feedback)
	TObjectPtr<UTrailEffectDefinition> WeaponTrail;						// 탄 트레일

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Feedback)
	TObjectPtr<USoundDefinition> GunSound;
};
