// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ProjectionMarkDefinition.generated.h"

/**
 * 
 */

//BP에서 조절 가능한 형태로 Material 데이터를 Wrapping; 

UCLASS(Blueprintable)
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
