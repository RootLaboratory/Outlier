// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Drone/DroneInputConfig.h"
#include "PartnerInputConfig.generated.h"

class UInputAction;
/**
 * 
 */
UCLASS(BlueprintType, Blueprintable)
class OUTLIER_API UPartnerInputConfig : public UDroneInputConfig
{
	GENERATED_BODY()
	
public:
	/** AreaOfEffect Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* AreaOfEffectAction;

	/** CameraAssist Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* CameraAssistAction;

	/** Hacking Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* HackingAction;

	/** Scan Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* ScanAction;

	/** Shield Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* ShieldAction;

	/** SyncMove Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* SyncMoveAction;

	/** Accelerate Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* AccelerateAction;
};
