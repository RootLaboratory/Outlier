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

// 발동 시점의 설정과 서버 시각을 함께 복제한다. 클라이언트 수신 시 감쇠를 다시 시작하지 않는다.
USTRUCT()
struct FPartnerEvasiveBurstState
{
	GENERATED_BODY()

	UPROPERTY()
	double StartedAt = 0.0;
	UPROPERTY()
	float Multiplier = 1.0f;
	UPROPERTY()
	float Duration = 0.0f;

	float GetMultiplier(double Now) const;
};

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
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	void HandleBoostStateChanged(bool bBoosting);

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
	// cm/s. 0이면 기존 MoveSpeed/BoostSpeed를 유지하고, 양수이면 CameraAssist에만 전용 최대 속도를 적용한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Partner|AutoFollow|Speed", meta = (ClampMin = "0", Units = "cm/s"))
	float CameraAssistMaxSpeed = 0.0f;
	UPROPERTY(EditAnywhere, Category = "Partner|AutoFollow|Speed")
	FFlightSpeedProfile CameraAssistSpeedProfile;
	UPROPERTY(EditAnywhere, Category = "Partner|AutoFollow|Speed")
	FFlightSpeedProfile SyncMoveSpeedProfile;

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
	// 최종 목적지는 유지하고, 막힌 장거리만 이 반경의 지역 경유지로 나누어 탐색한다.
	UPROPERTY(EditAnywhere, Category = "Partner|AutoFollow|Path", meta = (ClampMin = "500"))
	float FollowTravelHorizon = 2000.0f;
	UPROPERTY(EditAnywhere, Category = "Partner|AutoFollow|Path", meta = (ClampMin = "100"))
	float FollowPrefetchDistance = 1000.0f;
	UPROPERTY(EditAnywhere, Category = "Partner|AutoFollow|Path", meta = (ClampMin = "1"))
	int32 FollowMaxSearchNodes = 512;
	UPROPERTY(EditAnywhere, Category = "Partner|AutoFollow|Path", meta = (ClampMin = "1"))
	int32 FollowMaxCollisionChecks = 2048;
	UPROPERTY(EditAnywhere, Category = "Partner|AutoFollow|Path", meta = (ClampMin = "0.1"))
	float FollowMaxSearchMilliseconds = 4.0f;
	// 위 세 한도는 요청 전체의 누적 예산이다. 아래 한도만 프레임별 작업량을 제한한다.
	UPROPERTY(EditAnywhere, Category = "Partner|AutoFollow|Path", meta = (ClampMin = "128"))
	int32 FollowSearchSliceChecks = 256;
	UPROPERTY(EditAnywhere, Category = "Partner|AutoFollow|Path", meta = (ClampMin = "0.1"))
	float FollowSearchSliceMilliseconds = 1.0f;
	UPROPERTY(EditAnywhere, Category = "Partner|AutoFollow|Path", meta = (ClampMin = "0.1"))
	float FollowSearchTimeout = 2.0f;
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
	UPROPERTY(ReplicatedUsing = OnRep_EvasiveBurst)
	FPartnerEvasiveBurstState EvasiveBurst;
	UFUNCTION()
	void OnRep_EvasiveBurst();
	double GetBurstServerTime() const;
	float GetEffectiveBoostSpeed() const;
	void RefreshBoostSpeed(bool bForce = false);

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
	void AdvanceFollowPath(double Now, const FVector& Goal);
	void CompleteFollowPath(double Now);
	bool ShouldPrefetchFollowPath(const FVector& Current) const;
	EFlightPathStatus CheckFollowSegment(const FVector& Goal) const;
	float GetFollowTravelHorizon() const;
	void RememberFollowRoute(const FVector& Start);
	bool TryJoinFollowPath(TArray<FVector>& Points) const;
	void FollowStoredPath(float DeltaTime);
	bool HasReachedFollowWaypoint(const FVector& Current) const;
	void UpdateFollowProgress(double Now, const FVector& Current);
	void FailFollowPath(double Now, EFlightPathStatus Status, bool bKeepCurrentPath = false);
	void ClearFollowPath();
	void PublishFollowPath(bool bStopped);
	void StopFollowMovement();
	const FFlightSpeedProfile& GetFollowSpeedProfile() const;
	float GetFollowDesiredSpeed(bool bIncludeBurst = true) const;
	FFlightSpeedProfileState FollowSpeedState;
	bool bOwnsFollowSpeed = false;

	UFUNCTION(Server, Reliable)
	void ServerSetAutoFollowInput(EPartnerMoveMode Mode, bool bHeld, uint32 InputSerial);
	UFUNCTION(Client, Reliable)
	void ClientReceiveFollowPath(EPartnerMoveMode Mode, uint32 InputSerial, const TArray<FVector>& Points, bool bStopped);

	FPartnerFollowInputState CameraAssistInput;
	FPartnerFollowInputState SyncMoveInput;
	EPartnerMoveMode FollowMode;
	FPartnerFollowRetryState FollowRetry;
	EFlightPathStatus FollowLastFailure = EFlightPathStatus::Success;
	TArray<FVector> FollowPath;
	TSharedPtr<FFlightPathSearch> FollowSearch;
	double FollowSearchStartedAt = 0.0;
	int32 FollowWaypoint = 0;
	FVector FollowTargetSnapshot = FVector::ZeroVector;
	FVector FollowHoldLocation = FVector::ZeroVector;
	FVector FollowProgressLocation = FVector::ZeroVector;
	int32 FollowProgressWaypoint = INDEX_NONE;
	double FollowLastProgressAt = 0.0;
	double FollowNextPathCheckAt = 0.0;
	double FollowNextRepathAt = 0.0;
	double FollowNextDiagnosticAt = 0.0;
	bool bFollowWasActive = false;
	bool bFollowHolding = false;
	bool bFollowPartialRoute = false;
	bool bFollowSearchContinuation = false;
	int32 FollowRouteSegments = 0;
	double FollowBestGoalDistance = TNumericLimits<double>::Max();
	TArray<FVector> FollowRouteHistory;
	friend class FPartnerAssistNavigationTest;
	friend class FPartnerFollowLifecycleTest;

	UPROPERTY(Transient)
	TObjectPtr<UCapsuleComponent> MovementCapsule;
	UPROPERTY(Transient)
	TObjectPtr<UCharacterMovementComponent> CharacterMovement;

	UPROPERTY()
	TObjectPtr<APartnerCharacter> PartnerCharacter;

	UPROPERTY()
	TObjectPtr<AShooterCharacter> ShooterCharacter;
};
