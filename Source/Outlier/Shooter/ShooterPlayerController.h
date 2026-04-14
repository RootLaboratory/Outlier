// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "ShooterPlayerController.generated.h"

class AShooterCharacter;

/**
 * Basic player controller class for shooter gameplay.
 * Manages possession and respawn behavior.
 */
UCLASS(abstract)
class OUTLIER_API AShooterPlayerController : public AFirstPersonPlayerController
{
	GENERATED_BODY()

protected:
	/** Pawn class used when respawning the player. */
	UPROPERTY(EditAnywhere, Category = "Shooter|Respawn")
	TSubclassOf<AShooterCharacter> CharacterClass;

	/** Tag applied to the possessed player pawn. */
	UPROPERTY(EditAnywhere, Category = "Shooter|Player")
	FName PlayerPawnTag = FName("Player");

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;

	/** Pawn initialization */
	virtual void OnPossess(APawn* InPawn) override;

	/** Called if the possessed pawn is destroyed */
	UFUNCTION()
	void OnPawnDestroyed(AActor* DestroyedActor);

	// UI 관련
	// 총알
	// 피격 등등
};
