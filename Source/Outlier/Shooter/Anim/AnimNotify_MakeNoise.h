// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_MakeNoise.generated.h"

/**
 * 
 */
UCLASS()
class OUTLIER_API UAnimNotify_MakeNoise : public UAnimNotify
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noise",
		meta = (ClampMin = "0.0"))
	float Loudness = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noise",
		meta = (ClampMin = "0.0"))
	float MaxRange = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noise")
	FName NoiseTag = TEXT("Footstep");

	virtual void Notify(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference
	) override;
};
