// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Shooter/ShooterCharacterComponentBase.h"
#include "Shooter/ShooterCharacter.h"
#include "ShooterMovementComponent.generated.h"

UCLASS(ClassGroup=(Shooter), meta=(BlueprintSpawnableComponent))
class OUTLIER_API UShooterMovementComponent : public UShooterCharacterComponentBase
{
	GENERATED_BODY()

public:
	UShooterMovementComponent();

	void TryStartSprint();
	void TryStopSprint();
	void TryStartCrouchOrSlide();
	void TryStopCrouch();
	void TrySlide();
	void StopSlide(ESlideEndReason EndReason);
	void HandleSlideWallHit(const FHitResult& Hit);
	void DoJumpStart();
	void DoJumpEnd();

	// Slide
	void StartSlideMovement();
	void UpdateSlideMovement();
	void FinishSlideMovement();

	void RefreshMovementState();
	void SetMovementStateImmediate(EMovementState NewState);
	void StopSprintInternal();
	bool CanStartSlide() const;
};
