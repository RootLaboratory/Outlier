// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FirstPersonPlayerController.generated.h"

class UInputMappingContext;

/**
 * 
 */
UCLASS()
class OUTLIER_API AFirstPersonPlayerController : public APlayerController
{
	GENERATED_BODY()
	
public:
	AFirstPersonPlayerController();

protected:

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category = "Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	virtual void BeginPlay() override;

	// Input Mapping Context Setup
	virtual void SetupInputComponent() override;

};
