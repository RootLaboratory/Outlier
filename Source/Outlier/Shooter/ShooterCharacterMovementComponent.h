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

struct FShooterMoveResponse : FCharacterMoveResponseDataContainer
{
	FShooterJumpSnapshot JumpSnapshot;

	virtual void ServerFillResponseData(const UCharacterMovementComponent& Movement,
		const FClientAdjustment& PendingAdjustment) override;
	virtual bool Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* PackageMap) override;
};

/** 기존 packed 이동에 연출 식별자만 싣는다. 이동 성공/착지 판정은 서버가 그대로 수행한다. */
struct FShooterAudioMoveData : FCharacterNetworkMoveData
{
	uint32 AudioMoveId = 0;
	uint32 AudioAirborneId = 0;
	virtual void ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType) override;
	virtual bool Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* PackageMap,
		ENetworkMoveType MoveType) override;
};

struct FShooterAudioMoveDataContainer : FCharacterNetworkMoveDataContainer
{
	FShooterAudioMoveDataContainer();
	FShooterAudioMoveData Moves[3];
};

UCLASS()
class OUTLIER_API UShooterCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

	friend class FShooterSavedMove;
	friend class FShooterCoyoteTimeTest;
	friend class FOutlierMovementAudioPredictionTest;
	friend struct FShooterMoveResponse;

public:
	UShooterCharacterMovementComponent();

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
	virtual void StartFalling(int32 Iterations, float RemainingTime, float TimeTick,
		const FVector& Delta, const FVector& SubLocation) override;

private:
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
