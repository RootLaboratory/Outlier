// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "FirstPerson/FirstPersonInputConfig.h"
#include "DroneInputConfig.generated.h"

class UInputAction;
/**
 * 
 */
UCLASS(BlueprintType, Blueprintable)
class OUTLIER_API UDroneInputConfig : public UFirstPersonInputConfig
{
	GENERATED_BODY()
	
public:
	/** FreeMove Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* FreeMoveAction;

	/** VerticalMove Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* VerticalMoveAction;
};
