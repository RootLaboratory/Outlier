// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ProjectionMarkDefinition.generated.h"

/**
 * 
 */
UCLASS()
class VISUALEVENT_API UProjectionMarkDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()
	
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	TObjectPtr<UMaterialInterface> DecalMaterial;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    FVector DecalSize = FVector(8.f, 8.f, 8.f);

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    float LifeSpan = 10.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    FRotator RotationOffset = FRotator::ZeroRotator;
};
