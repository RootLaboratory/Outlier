#pragma once
// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_SpawnBulletCasing.generated.h"

class UNiagaraSystem;

/**
 *
 */
UCLASS()
class OUTLIER_API UAnimNotify_SpawnBulletCasing : public UAnimNotify
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bullet Casing")
	TObjectPtr<UNiagaraSystem> BulletCasingSystem = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bullet Casing")
	FName SocketName = TEXT("BulletCasing");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bullet Casing")
	uint8 bFirstPerson : 1 = false;

	virtual void Notify(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference
	) override;
};
