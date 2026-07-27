// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FirstPersonInputConfig.generated.h"

class UInputAction;

UCLASS(BlueprintType, Blueprintable)
class OUTLIER_API UFirstPersonInputConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Move Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

	/** Attack Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> AttackAction;

	/** Interaction Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> InteractionAction;

	/** Camera Toggle Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> CamToggleAction;

	UPROPERTY(EditAnywhere, Category = "Debug")
	TObjectPtr<UInputAction> DebugArenaReload;
};
