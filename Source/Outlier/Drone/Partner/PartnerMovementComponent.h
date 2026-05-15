// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Drone/Partner/PartnerCharacterComponentBase.h"
#include "PartnerMovementComponent.generated.h"

enum class EPartnerMoveMode : uint8;

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class OUTLIER_API UPartnerMovementComponent : public UPartnerCharacterComponentBase
{
	GENERATED_BODY()

public:	
	void RefreshMovementState();
	void ApplyCameraAssist();
	void StopCameraAssist();
	void SetSyncMove(bool SyncMove);
	void SetFreeMove(bool FreeMove);
	void SetMoveInput(const FVector2D& MoveInput);
	void SetVerticalInput(const float Axis);
	void OnMoveModeChanged(EPartnerMoveMode NewMode);

	void RefreshTickEnabled();

	void UpdateBoundaryState();

	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction
	) override;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float DirectionWeight = 500.0f;

private:
	uint8 bSyncMove		: 1 = false;
	uint8 bFreeMove		: 1 = false;
	uint8 bCameraAssist : 1 = false;
	uint8 bMovementFeelInitialized : 1 = false;
	FVector2D CurrentMoveInput = FVector2D::ZeroVector;
	float VerticalInput     = 0.0f;
	FVector LastLocation = FVector::ZeroVector;
	FVector SmoothedVelocity = FVector::ZeroVector;

private:
	void UpdateNormalMove(float DeltaTime);
	void UpdateFreeMove(float DeltaTime);
	void UpdateSyncMove(float DeltaTime);
	void UpdateVerticalMove(float DeltaTime);
	void UpdateCameraAssist(float DeltaTime);
	void UpdateMovementFeel(float DeltaTime);
	void EnterSyncMove();

	void MoveTowardTargetWithAvoidance(
		const FVector& TargetLocation,
		float DeltaTime,
		float InterpSpeed
	);

	FVector FindSimpleAvoidanceTarget(
		const FVector& CurrentLocation,
		const FVector& TargetLocation,
		const FHitResult& Hit
	);

	bool IsPathClear(const FVector& From, const FVector& To) const;
	float GetMovementMassFactor() const;
};
