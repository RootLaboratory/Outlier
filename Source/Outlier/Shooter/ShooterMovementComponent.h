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
	friend class FShooterGroundedMovementTest;

protected:
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	uint8 bWantsToSprint : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	uint8 bWantsToCrouch : 1 = false;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	uint8 bIsSprinting : 1 = false;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	uint8 bIsSliding : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	uint8 bIsSlidingCanceled : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Slide")
	FVector SlideDirection = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Slide")
	float SlideStartSpeed = 0.0f;
	float CurrentSlideSpeed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Slide")
	float SlideElapsedTime = 0.0f;

	double SlideStartTime = 0.0;
	float SavedGroundFriction = 0.0f;
	float SavedBrakingDecelerationWalking = 0.0f;
	float SavedBrakingFrictionFactor = 0.0f;
	float SavedMaxAcceleration = 0.0f;
	float SavedMaxWalkSpeedCrouched = 0.0f;

	FTimerHandle SlideUpdateTimerHandle;
	FTimerHandle SlideTimerHandle;

public:
	UShooterMovementComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void HandleSprintPressed();
	void HandleSprintReleased();
	void HandleCrouchPressed();
	void HandleCrouchReleased();
	void RequestCrouchOrSlide();
	void RequestUncrouch();
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
	void SuspendSprintInternal();
	void ClearInputIntent();
	bool CanStartSlide() const;
	bool CanSprint() const;

	bool WantsToSprint() const { return bWantsToSprint; }
	bool WantsToCrouch() const { return bWantsToCrouch; }
	bool IsSprinting() const { return bIsSprinting; }
	bool IsSliding() const { return bIsSliding; }
	bool IsSlidingCanceled() const { return bIsSlidingCanceled; }
};
