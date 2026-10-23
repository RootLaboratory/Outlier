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

UCLASS()
class OUTLIER_API UShooterCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

	friend class FShooterSavedMove;
	friend class FShooterCoyoteTimeTest;
	friend struct FShooterMoveResponse;

public:
	UShooterCharacterMovementComponent();

	bool HasCoyoteOpportunity() const;
	bool IsCoyoteJumpAttempt() const { return bCoyoteJumpAttempt; }
	void ResetCoyoteTime();
	void PrepareJumpInput(float DeltaTime);
	void FinishJumpInput();

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
	bool bFallingFirstJumpAttempt = false;
	bool bCoyoteJumpAttempt = false;
	bool bJumpSucceeded = false;
	bool bStartingNaturalFall = false;
	bool bExternalMovementPending = false;
	bool bHasCorrectedJumpInput = false;
	bool bCorrectedPressedJump = false;
};
