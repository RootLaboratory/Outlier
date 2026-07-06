// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_PlayWeaponAnimation.generated.h"

/**
 * 
 */
UCLASS()
class OUTLIER_API UAnimNotify_PlayWeaponAnimation : public UAnimNotify
{
	GENERATED_BODY()
	
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<UAnimationAsset> WeaponAnimation = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	uint8 bFirstPerson : 1 = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	uint8 bLooping : 1 = false;

	virtual void Notify(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference
	) override;
};
