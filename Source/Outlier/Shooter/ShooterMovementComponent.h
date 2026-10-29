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
	friend class UShooterCharacterMovementComponent;

protected:
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	uint8 bWantsToSprint : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	uint8 bWantsToCrouch : 1 = false;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	uint8 bIsSprinting : 1 = false;

	UPROPERTY(ReplicatedUsing = OnRep_IsSliding, VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	uint8 bIsSliding : 1 = false;

	UFUNCTION()
	void OnRep_IsSliding();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	uint8 bIsSlidingCanceled : 1 = false;

	// 기존 Blueprint 조회를 보존하는 표시용 사본이다. 계산/갱신은 CMC에서만 한다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Slide")
	FVector SlideDirection = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Slide")
	float SlideStartSpeed = 0.0f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Slide")
	float SlideElapsedTime = 0.0f;

	float BaseMaxWalkSpeedCrouched = -1.0f;

	float GetStealthSpeedMultiplier() const;

#if !UE_BUILD_SHIPPING
	// 실행 측마다 증가하는 ID다. 서버/클라이언트 사이의 공통 요청 번호로 사용하지 않는다.
	uint32 SlideDiagnosticId = 0;
	void TraceSlideDiagnostic(const TCHAR* Event) const;
	float GetSlideDiagnosticDuration() const;
#endif

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
