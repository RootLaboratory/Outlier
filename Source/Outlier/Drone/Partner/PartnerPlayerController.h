// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "PartnerPlayerController.generated.h"

class APartnerCharacter;
/**
 * 
 */
UCLASS()
class OUTLIER_API APartnerPlayerController : public AFirstPersonPlayerController
{
	GENERATED_BODY()

protected:
	/** Pawn class used when respawning the player. */
	UPROPERTY(EditAnywhere, Category = "Partner|Respawn")
	TSubclassOf<APartnerCharacter> CharacterClass;

	/** Tag applied to the possessed player pawn. */
	UPROPERTY(EditAnywhere, Category = "Partner|Player")
	FName PartnerPawnTag = FName("Partner");

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;

	/** Pawn initialization */
	virtual void OnPossess(APawn* InPawn) override;

	virtual void BindMainUI() override;

	virtual void BindPostProcessSubSystem() override;

	virtual void ReceivedPlayer() override;

	virtual void AcknowledgePossession(APawn* P) override;

public:
	APartnerPlayerController();
	
	
};
