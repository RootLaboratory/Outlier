#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/CharacterMovementReplication.h"
#include "ShooterCharacterMovementComponent.generated.h"

struct FShooterCoyoteState
{
	float RemainingTime = 0.0f;
	bool bAvailable = false;
};

// 서버 보정과 이동 재생이 같은 점프 이력에서 시작하도록 함께 보관한다.
struct FShooterJumpSnapshot
{
	FShooterCoyoteState Coyote;
	int32 JumpCount = 0;
	float HoldTime = 0.0f;
	float ForceTime = 0.0f;
	bool bWasJumping = false;
	bool bPressedJump = false;
	bool bExternalMovementPending = false;
};

// 순번은 이동 timestamp 리셋과 독립적이다. 최신 요청을 반복 전송해 패킷 손실에도 의도를 보존한다.
enum class EShooterMovementInput : uint8
{
	SprintPressed,
	SprintReleased,
	CrouchPressed,
	CrouchReleased,
	SlideJumpCancel
};

struct FShooterMovementInput
{
	// 서버가 수명주기 경계에서 갱신한다. 이전 조종/위치의 지연 패킷은 새 Slide를 시작할 수 없다.
	uint32 Epoch = 0;
	uint32 CrouchRequest = 0;
	uint32 JumpCancelRequest = 0;
	bool bSprint = false;
	bool bCrouch = false;
	bool bSprintAtCrouch = false;
	bool operator==(const FShooterMovementInput& Other) const;
	void Serialize(FArchive& Ar);
};

// 정보                         | 저장/복원 책임
// 요청 순번, 눌림/홀드 입력      | SavedMove -> 이동 패킷 -> 해당 이동의 입력 판정
// 처리 완료 순번, 승인된 상태   | 서버 보정 -> 최초 재생 기준; 이후에는 직전 재생 결과를 유지
// 방향/진입 속도/경과/감속/종료 | 아래 Snapshot; 실제 Velocity/위치는 엔진 보정이 담당
// 실제 앉기 자세               | Snapshot으로 캡슐 복원; 홀드 의도와 구분
// 마찰/제동/가속 임시 설정      | 로컬에서 한 번 보관/복구하며 패킷에 싣지 않음
struct FShooterSlideSnapshot
{
	FShooterMovementInput AppliedInput;
	uint32 ProcessedCrouchRequest = 0;
	uint32 ProcessedJumpCancelRequest = 0;
	uint32 ActiveRequest = 0;
	FVector Direction = FVector::ZeroVector;
	float StartSpeed = 0.0f;
	float CurrentSpeed = 0.0f;
	float Elapsed = 0.0f;
	float Duration = 0.0f;
	uint8 EndReason = 0;
	bool bActive = false;
	bool bSprintHeld = false;
	bool bCrouchHeld = false;
	bool bCrouched = false;
	void Serialize(FArchive& Ar);
};

struct FShooterMoveResponse : FCharacterMoveResponseDataContainer
{
	FShooterJumpSnapshot JumpSnapshot;
	FShooterSlideSnapshot SlideSnapshot;

	virtual void ServerFillResponseData(const UCharacterMovementComponent& Movement,
		const FClientAdjustment& PendingAdjustment) override;
	virtual bool Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* PackageMap) override;
};

/** 기존 packed 이동에 입력과 연출 식별자를 싣는다. 진입 속도/방향은 서버가 계산한다. */
struct FShooterAudioMoveData : FCharacterNetworkMoveData
{
	uint32 AudioMoveId = 0;
	uint32 AudioAirborneId = 0;
	FShooterMovementInput MovementInput;
	uint32 PredictedSlideRequest = 0;
	bool bPredictedSliding = false;
	bool bPredictedSprintHeld = false;
	bool bPredictedCrouchHeld = false;
	bool bPredictedCrouched = false;
	uint8 PredictedEndReason = 0;
	virtual void ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType) override;
	virtual bool Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* PackageMap,
		ENetworkMoveType MoveType) override;
};

struct FShooterAudioMoveDataContainer : FCharacterNetworkMoveDataContainer
{
	FShooterAudioMoveDataContainer();
	FShooterAudioMoveData Moves[3];
};

class UShooterMovementComponent;
class UOutlierArenaSubsystem;

UCLASS()
class OUTLIER_API UShooterCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

	friend class FShooterSavedMove;
	friend class FShooterCoyoteTimeTest;
	friend class FOutlierMovementAudioPredictionTest;
	friend class FShooterGroundedMovementTest;
	friend class UShooterMovementComponent;
	friend struct FShooterMoveResponse;
	friend class FShooterSlidePredictionTest;
	friend class FShooterSlideLifecycleTest;
	friend class FShooterSlideTerrainTest;

public:
	UShooterCharacterMovementComponent();

	bool IsSlideSimulating() const { return bSlideSimulating; }
	float GetSlideElapsedTime() const { return SlideElapsedTime; }
	float GetCurrentSlideSpeed() const { return CurrentSlideSpeed; }
	FVector GetSlideDirection() const { return SlideDirection; }
	bool QueueMovementInput(EShooterMovementInput Input);
	void ApplyMovementInput();
	bool IsReplayingMovement() const;
	void ResetSlideForLifecycle();

#if !UE_BUILD_SHIPPING
	static bool IsSlideDiagnosticEnabled();
	void LogSlideDiagnostic(const TCHAR* Event, float MoveDeltaSeconds = 0.0f, float MoveTimeStamp = -1.0f);
#endif

	bool HasCoyoteOpportunity() const;
	bool IsCoyoteJumpAttempt() const { return bCoyoteJumpAttempt; }
	void ResetCoyoteTime();
	void PrepareJumpInput(float DeltaTime);
	void FinishJumpInput();
	/** 점프는 저장 이동, 착지는 공중 체류 회차를 사용해 서로 다른 착지 프레임도 같은 이벤트로 묶는다. */
	uint64 GetMovementAudioEventId(bool bLanding) const;

	virtual bool DoJump(bool bReplayingMoves, float DeltaTime) override;
	virtual void PerformMovement(float DeltaSeconds) override;
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;
	virtual void OnTeleported() override;
	virtual void Launch(FVector const& LaunchVelocity) override;
	virtual void AddImpulse(FVector Impulse, bool bVelocityChange = false) override;
	virtual void AddForce(FVector Force) override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	// 코요테/횟수 보정 응답이 누락되는 구형 이동 RPC 경로로 전환하지 않는다.
	virtual bool ShouldUsePackedMovementRPCs() const override { return true; }
	virtual void ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& MoveResponse) override;
	virtual bool ClientUpdatePositionAfterServerUpdate() override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual void MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags,
		const FVector& NewAccel) override;
	virtual void ServerMoveHandleClientError(float ClientTimeStamp, float DeltaTime, const FVector& Accel,
		const FVector& RelativeClientLocation, UPrimitiveComponent* ClientMovementBase,
		FName ClientBaseBoneName, uint8 ClientMovementMode) override;

protected:
	virtual void BeginPlay() override;
	virtual void PhysWalking(float DeltaTime, int32 Iterations) override;
	virtual void PhysNavWalking(float DeltaTime, int32 Iterations) override;
	virtual void HandleImpact(const FHitResult& Hit, float TimeSlice, const FVector& MoveDelta) override;

	virtual void StartFalling(int32 Iterations, float RemainingTime, float TimeTick,
		const FVector& Delta, const FVector& SubLocation) override;

private:
	bool BeginSlideSimulation(UShooterMovementComponent* Movement);
	void EndSlideSimulation();
	void PhysSlide(float DeltaTime, int32 Iterations, EMovementMode GroundMode);
	float EvaluateSlideSpeed(float ElapsedTime) const;
	void FinishSimulatedSlide();
	void PublishSlideState();
	UShooterMovementComponent* GetShooterMovement() const;
	void SaveSlidePhysics();
	FShooterSlideSnapshot CaptureSlideSnapshot() const;
	void RestoreSlideSnapshot(const FShooterSlideSnapshot& Snapshot);
	void ReconcileSlidePresentation();
	FShooterMovementInput LiveMovementInput;
	FShooterMovementInput MoveInput;
	FShooterMovementInput AppliedMovementInput;
	uint32 MovementEpoch = 0;
	uint32 NextMovementRequest = 0;
	uint32 ProcessedCrouchRequest = 0;
	uint32 ProcessedJumpCancelRequest = 0;
	uint32 ActiveSlideRequest = 0;
	uint32 PresentedSlideRequest = 0;
	uint32 PresentedSlideEpoch = 0;
	uint8 SlideEndReason = 0;
	bool bApplyingMovementInput = false;
	bool bUsingMovementInput = false;
	bool bMovementInputTransition = false;
	bool bSlidePresented = false;
	bool bRestoringMovementState = false;
	FShooterSlideSnapshot PendingServerSlide;
	// 재생 도중 정리돼도 엔진이 현재 SavedMove의 PostUpdate를 마칠 때까지 객체 수명은 유지한다.
	TArray<FSavedMovePtr> LifecycleReplayMoves;
#if WITH_DEV_AUTOMATION_TESTS
	// 에셋 재생 여부와 구분해 시작/종료 연출 요청 자체의 중복을 검증한다.
	uint32 SlidePresentationStarts = 0;
	uint32 SlidePresentationStops = 0;
#endif

	// CMC만 시간/속도/임시 물리 설정을 소유한다. ActorComponent의 복제 bool은 기존 연출 연결용이다.
	TWeakObjectPtr<UShooterMovementComponent> SlideMovement;
	FVector SlideDirection = FVector::ZeroVector;
	float SlideStartSpeed = 0.0f;
	float CurrentSlideSpeed = 0.0f;
	float SlideElapsedTime = 0.0f;
	float SlideSimulationDuration = 0.0f;
	float SavedGroundFriction = 0.0f;
	float SavedBrakingDecelerationWalking = 0.0f;
	float SavedBrakingFrictionFactor = 0.0f;
	float SavedMaxAcceleration = 0.0f;
	float SavedMaxWalkSpeedCrouched = 0.0f;
	bool bSlideSimulating = false;
	// 동기 종료/충돌 콜백에서 새 Slide가 시작됐는지 구분한다. 네트워크 요청 ID가 아니다.
	uint32 SlideSimulationSerial = 0;
#if !UE_BUILD_SHIPPING
	double SlideDiagnosticStartTime = 0.0;
	bool ShouldTraceSlideMovement() const;
	TWeakObjectPtr<UShooterMovementComponent> DiagnosticMovement;
	TWeakObjectPtr<UOutlierArenaSubsystem> DiagnosticArena;
	float DiagnosticCaptureUntil = -1.0f;
	float DiagnosticReplayTimeStamp = -1.0f;
#endif

	FShooterJumpSnapshot CaptureJumpSnapshot() const;
	void RestoreJumpSnapshot(const FShooterJumpSnapshot& Snapshot);

	FShooterCoyoteState CoyoteState;
	FShooterJumpSnapshot BeforeJumpInput;
	FShooterJumpSnapshot PendingServerSnapshot;
	FShooterMoveResponse MoveResponse;
	FShooterAudioMoveDataContainer AudioMoveData;
	uint32 NextAudioMoveId = 0;
	uint32 AudioMoveId = 0;
	uint32 AudioAirborneId = 0;
	bool bUsingNetworkAudioIds = false;
	bool bFallingFirstJumpAttempt = false;
	bool bCoyoteJumpAttempt = false;
	bool bJumpSucceeded = false;
	bool bStartingNaturalFall = false;
	bool bExternalMovementPending = false;
	bool bHasCorrectedJumpInput = false;
	bool bCorrectedPressedJump = false;
};
