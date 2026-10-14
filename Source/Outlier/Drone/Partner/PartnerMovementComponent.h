#pragma once

#include "CoreMinimal.h"
#include "Drone/FlightMovementComponentBase.h"
#include "Drone/FlightPathfinding.h"
#include "PartnerMovementComponent.generated.h"

class APartnerCharacter;
class AShooterCharacter;
class UCapsuleComponent;
class UCharacterMovementComponent;
enum class EPartnerMoveMode : uint8;

// 종료와 버튼 해제를 구분한다. 실패해도 Held는 유지하므로 Triggered로 자동 재개되지 않는다.
struct FPartnerFollowInputState
{
	uint32 Serial = 0;
	bool bHeld = false;
	bool bActive = false;

	bool Press()
	{
		if (bHeld)
		{
			return false;
		}
		if (++Serial == 0)
		{
			++Serial;
		}
		bHeld = bActive = true;
		return true;
	}
	bool AcceptPress(uint32 NewSerial)
	{
		if (bHeld || NewSerial == 0 || static_cast<int32>(NewSerial - Serial) <= 0)
		{
			return false;
		}
		Serial = NewSerial;
		bHeld = bActive = true;
		return true;
	}
	bool Release(uint32 ReleasedSerial)
	{
		if (!bHeld || Serial != ReleasedSerial)
		{
			return false;
		}
		bHeld = bActive = false;
		return true;
	}
	bool AcceptsPath(uint32 PathSerial) const
	{
		return bHeld && bActive && Serial == PathSerial;
	}
	void Stop()
	{
		bActive = false;
	}
};

// 예산 부족도 제한적으로 재시도한다. 이것을 물리적인 도달 불가능으로 단정하지 않는다.
struct FPartnerFollowRetryState
{
	int32 Failures = 0;
	double StartedAt = -1.0;
	double NextAttemptAt = 0.0;

	void Reset()
	{
		*this = FPartnerFollowRetryState();
	}
	void Fail(double Now, double Interval)
	{
		if (StartedAt < 0.0)
		{
			StartedAt = Now;
		}
		++Failures;
		NextAttemptAt = Now + FMath::Max(Interval, 0.05);
	}
	bool IsExhausted(double Now, int32 MaxAttempts, double Timeout) const
	{
		return Failures >= FMath::Max(MaxAttempts, 1)
			|| (StartedAt >= 0.0 && Now - StartedAt >= FMath::Max(Timeout, 0.05));
	}
};

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class OUTLIER_API UPartnerMovementComponent : public UFlightMovementComponentBase
{
	GENERATED_BODY()

public:
	UPartnerMovementComponent();

	void RefreshCharacterRefsFromPlayerState();
	void RefreshMovementState();
	void ApplyCameraAssist();
	void StopAutoFollow();
	void ReleaseCameraAssistInput();
	void ResetAutoFollowForOwnerChange();
	void SetSyncMove(bool SyncMove);
	void SetFreeMove(bool FreeMove);
	virtual void SetMoveInput(const FVector2D& MoveInput) override;
	virtual void SetVerticalInput(float Axis) override;
	void OnMoveModeChanged(EPartnerMoveMode NewMode);
	void OnReplicatedMoveMode();
	void ApplyPartnerFlightSettings();

protected:
	virtual void BeginPlay() override;
	virtual ACharacter* GetFlightOwnerCharacter() const override;
	virtual USceneComponent* GetFlightVisualTiltRoot() const override;
	virtual USceneComponent* GetFlightViewModelRoot() const override;
	virtual bool CanRunInputMovement() const override;
	virtual bool ShouldUpdateMovementFeel() const override;
	virtual EFlightInputMode GetFlightInputMode() const override;
	virtual void OnAfterInputMovement(float DeltaTime) override;

	// 기존 BP 저장 속성 호환용이다. 공용 경로 탐색에서는 이전 8방향 회피 가중치를 사용하지 않는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Partner|Move", meta = (DeprecatedProperty,
		DeprecationMessage = "AutoFollow now uses capsule-validated flight paths."))
	float DirectionWeight = 500.0f;

	UPROPERTY(EditAnywhere, Category = "Partner|AutoFollow|Path", meta = (ClampMin = "25"))
	float FollowPathCellSize = 100.0f;
	UPROPERTY(EditAnywhere, Category = "Partner|AutoFollow|Path", meta = (ClampMin = "0"))
	float FollowPathClearance = 5.0f;
	UPROPERTY(EditAnywhere, Category = "Partner|AutoFollow|Path", meta = (ClampMin = "100"))
	float FollowPathMargin = 800.0f;
	UPROPERTY(EditAnywhere, Category = "Partner|AutoFollow|Path", meta = (ClampMin = "0"))
	float FollowGoalSearchRadius = 150.0f;
	UPROPERTY(EditAnywhere, Category = "Partner|AutoFollow|Path", meta = (ClampMin = "1"))
	int32 FollowMaxSearchNodes = 512;
	UPROPERTY(EditAnywhere, Category = "Partner|AutoFollow|Path", meta = (ClampMin = "1"))
	int32 FollowMaxCollisionChecks = 2048;
	UPROPERTY(EditAnywhere, Category = "Partner|AutoFollow|Path", meta = (ClampMin = "0.1"))
	float FollowMaxSearchMilliseconds = 4.0f;
	UPROPERTY(EditAnywhere, Category = "Partner|AutoFollow|Path", meta = (ClampMin = "0.05"))
	float FollowRepathInterval = 0.25f;
	UPROPERTY(EditAnywhere, Category = "Partner|AutoFollow|Path", meta = (ClampMin = "10"))
	float FollowTargetUpdateDistance = 100.0f;
	UPROPERTY(EditAnywhere, Category = "Partner|AutoFollow|Path", meta = (ClampMin = "1"))
	int32 FollowMaxRetryAttempts = 3;
	UPROPERTY(EditAnywhere, Category = "Partner|AutoFollow|Path", meta = (ClampMin = "0.1"))
	float FollowRetryTimeout = 2.0f;
	UPROPERTY(EditAnywhere, Category = "Partner|AutoFollow|Path", meta = (ClampMin = "0.1"))
	float FollowNoProgressTimeout = 1.0f;

private:
	bool IsAutoFollowMoveMode() const;
	void EnterSyncMove();
	void UpdateAutoFollow(float DeltaTime);
	void StartAutoFollow(EPartnerMoveMode Mode);
	void ReleaseAutoFollow(EPartnerMoveMode Mode);
	FPartnerFollowInputState& GetFollowInput(EPartnerMoveMode Mode);
	FVector GetAutoFollowTarget() const;
	float GetFollowAcceptanceRadius() const;
	FFlightPathRequest MakeFollowPathRequest(const FVector& Goal) const;
	void UpdateFollowPath(double Now, const FVector& Goal);
	void FindFollowPath(double Now, const FVector& Goal);
	void FollowStoredPath(float DeltaTime);
	void FailFollowPath(double Now, EFlightPathStatus Status);
	void ClearFollowPath();
	void PublishFollowPath(bool bStopped);
	void StopFollowMovement();

	UFUNCTION(Server, Reliable)
	void ServerSetAutoFollowInput(EPartnerMoveMode Mode, bool bHeld, uint32 InputSerial);
	UFUNCTION(Client, Reliable)
	void ClientReceiveFollowPath(EPartnerMoveMode Mode, uint32 InputSerial, const TArray<FVector>& Points, bool bStopped);

	FPartnerFollowInputState CameraAssistInput;
	FPartnerFollowInputState SyncMoveInput;
	EPartnerMoveMode FollowMode;
	FPartnerFollowRetryState FollowRetry;
	TArray<FVector> FollowPath;
	int32 FollowWaypoint = 0;
	FVector FollowTargetSnapshot = FVector::ZeroVector;
	FVector FollowHoldLocation = FVector::ZeroVector;
	FVector FollowProgressLocation = FVector::ZeroVector;
	double FollowLastProgressAt = 0.0;
	double FollowNextPathCheckAt = 0.0;
	double FollowNextRepathAt = 0.0;
	bool bFollowWasActive = false;
	bool bFollowHolding = false;
	friend class FPartnerAssistNavigationTest;

	UPROPERTY(Transient)
	TObjectPtr<UCapsuleComponent> MovementCapsule;
	UPROPERTY(Transient)
	TObjectPtr<UCharacterMovementComponent> CharacterMovement;

	UPROPERTY()
	TObjectPtr<APartnerCharacter> PartnerCharacter;

	UPROPERTY()
	TObjectPtr<AShooterCharacter> ShooterCharacter;
};
