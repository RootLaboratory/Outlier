// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ShooterInputConfig.generated.h"

class UInputAction;

/**
 * 
 */
UCLASS(BlueprintType, Blueprintable)
class OUTLIER_API UShooterInputConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* JumpAction;

	/** Switch Weapon Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* SwitchWeapon1Action;

	/** Switch Weapon Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* SwitchWeapon2Action;

	/** Switch Weapon Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* SwitchWeapon3Action;

	/** Sprint Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* SprintAction;

	/** Crouch Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* CrouchAction;

	/** Lean Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* LeanAction;

	/** Slide Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* SlideAction;

	/** Interaction Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* InteractionAction;
	
	/** Suit Select Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* SuitSelectAction;

	/** Suit Use Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* SuitUseAction;

	/** Reload Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* ReloadAction;

	/** Aim Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* AimAction;

};
