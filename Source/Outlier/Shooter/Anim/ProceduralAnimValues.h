// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WeaponValues.h"
#include "RecoilValues.h"
#include "ProceduralAnimValues.generated.h"

/**
 * 
 */
UCLASS(BlueprintType)
class OUTLIER_API UProceduralAnimValues : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FWeaponValues WeaponValues;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FRecoilValues RecoilValues;
};
