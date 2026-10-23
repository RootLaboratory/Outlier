#include "Shooter/ShooterCharacterMovementComponent.h"

#include "Shooter/ShooterCharacter.h"

DEFINE_LOG_CATEGORY_STATIC(LogShooterCoyote, Log, All);

class FShooterSavedMove : public FSavedMove_Character
{
	using Super = FSavedMove_Character;

public:
	FShooterJumpSnapshot BeforeJump;
	bool bStartedFalling = false;

	virtual void Clear() override
	{
		Super::Clear();
		BeforeJump = {};
		bStartedFalling = false;
	}

	virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, const FVector& NewAccel,
		FNetworkPredictionData_Client_Character& ClientData) override
	{
		Super::SetMoveFor(Character, InDeltaTime, NewAccel, ClientData);
		const auto* Movement = CastChecked<UShooterCharacterMovementComponent>(Character->GetCharacterMovement());
		// 소유 클라이언트에서는 CheckJumpInput 다음에 저장된다. 소비 후 상태를 저장하면 재생 때 첫 점프가 사라진다.
		BeforeJump = Movement->BeforeJumpInput;
		JumpCurrentCount = BeforeJump.JumpCount;
		bWasJumping = BeforeJump.bWasJumping;
		JumpKeyHoldTime = BeforeJump.HoldTime;
		JumpForceTimeRemaining = BeforeJump.ForceTime;
		bStartedFalling = Movement->IsFalling();
		bForceNoCombine |= BeforeJump.Coyote.bAvailable || bStartedFalling || bPressedJump || JumpCurrentCount != 0;
	}

	virtual void PostUpdate(ACharacter* Character, EPostUpdateMode Mode) override
	{
		Super::PostUpdate(Character, Mode);
		// 바닥 이탈을 포함한 이동도 결합하지 않아 서버에서 지지 상실 시각을 다시 판정할 수 있게 한다.
		bForceNoCombine |= Character->GetCharacterMovement()->IsFalling();
	}

	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override
	{
		const auto* NewShooterMove = static_cast<const FShooterSavedMove*>(NewMove.Get());
		return !BeforeJump.Coyote.bAvailable && !NewShooterMove->BeforeJump.Coyote.bAvailable
			&& !bStartedFalling && !NewShooterMove->bStartedFalling
			&& Super::CanCombineWith(NewMove, Character, MaxDelta);
	}

	virtual void PrepMoveFor(ACharacter* Character) override
	{
		auto* Movement = CastChecked<UShooterCharacterMovementComponent>(Character->GetCharacterMovement());
		// 첫 재생은 서버 보정 상태, 이후 재생은 직전 이동의 결과를 잇는다.
		// 원래 예측했던 횟수/유예를 매번 복원하면 서버가 거부한 기회가 다시 살아난다.
		const FShooterJumpSnapshot ReplayStart = Movement->CaptureJumpSnapshot();
		Super::PrepMoveFor(Character);
		Movement->RestoreJumpSnapshot(ReplayStart);
	}
};

class FShooterClientPredictionData : public FNetworkPredictionData_Client_Character
{
public:
	explicit FShooterClientPredictionData(const UCharacterMovementComponent& Movement)
		: FNetworkPredictionData_Client_Character(Movement) {}

	virtual FSavedMovePtr AllocateNewMove() override
	{
		return FSavedMovePtr(new FShooterSavedMove());
	}
};

UShooterCharacterMovementComponent::UShooterCharacterMovementComponent()
{
	SetMoveResponseDataContainer(MoveResponse);
}

bool UShooterCharacterMovementComponent::HasCoyoteOpportunity() const
{
	const auto* Shooter = Cast<AShooterCharacter>(CharacterOwner);
	return Shooter && Shooter->GetController() && !Shooter->IsDead() && !Shooter->IsSuitTransitionBlocked()
		&& Shooter->CoyoteTime > 0.0f && IsFalling() && Shooter->JumpCurrentCount == 0
		&& CoyoteState.bAvailable && CoyoteState.RemainingTime >= 0.0f;
}

void UShooterCharacterMovementComponent::ResetCoyoteTime()
{
	CoyoteState = {};
}

FShooterJumpSnapshot UShooterCharacterMovementComponent::CaptureJumpSnapshot() const
{
	FShooterJumpSnapshot Snapshot;
	Snapshot.Coyote = CoyoteState;
	Snapshot.bExternalMovementPending = bExternalMovementPending;
	if (CharacterOwner)
	{
		Snapshot.JumpCount = CharacterOwner->JumpCurrentCount;
		Snapshot.HoldTime = CharacterOwner->JumpKeyHoldTime;
		Snapshot.ForceTime = CharacterOwner->JumpForceTimeRemaining;
		Snapshot.bWasJumping = CharacterOwner->bWasJumping;
		Snapshot.bPressedJump = CharacterOwner->bPressedJump;
	}
	return Snapshot;
}

void UShooterCharacterMovementComponent::RestoreJumpSnapshot(const FShooterJumpSnapshot& Snapshot)
{
	CoyoteState = Snapshot.Coyote;
	bExternalMovementPending = Snapshot.bExternalMovementPending;
	if (CharacterOwner)
	{
		CharacterOwner->JumpCurrentCount = Snapshot.JumpCount;
		CharacterOwner->JumpCurrentCountPreJump = Snapshot.JumpCount;
		CharacterOwner->JumpKeyHoldTime = Snapshot.HoldTime;
		CharacterOwner->JumpForceTimeRemaining = Snapshot.ForceTime;
		CharacterOwner->bWasJumping = Snapshot.bWasJumping;
		CharacterOwner->bPressedJump = Snapshot.bPressedJump;
	}
}

void UShooterCharacterMovementComponent::PrepareJumpInput(float DeltaTime)
{
	(void)DeltaTime;
	BeforeJumpInput = CaptureJumpSnapshot();
	bFallingFirstJumpAttempt = CharacterOwner && CharacterOwner->bPressedJump
		&& !CharacterOwner->bWasJumping && IsFalling() && CharacterOwner->JumpCurrentCount == 0;
	bCoyoteJumpAttempt = bFallingFirstJumpAttempt && HasCoyoteOpportunity();
	bJumpSucceeded = false;
}

void UShooterCharacterMovementComponent::FinishJumpInput()
{
	// 엔진의 낙하 선증가는 실패 때도 남는다. 거절된 첫 시도에서는 횟수와 유예를 소비하지 않는다.
	if (bFallingFirstJumpAttempt && !bJumpSucceeded && CharacterOwner)
	{
		CharacterOwner->JumpCurrentCount = BeforeJumpInput.JumpCount;
		CharacterOwner->JumpCurrentCountPreJump = BeforeJumpInput.JumpCount;
	}
	bFallingFirstJumpAttempt = false;
	bCoyoteJumpAttempt = false;
}

bool UShooterCharacterMovementComponent::DoJump(bool bReplayingMoves, float DeltaTime)
{
	if (bFallingFirstJumpAttempt && !bCoyoteJumpAttempt)
	{
		return false;
	}

	const int32 PreviousCount = CharacterOwner ? CharacterOwner->JumpCurrentCount : 0;
	const float CoyoteRemainingTime = CoyoteState.RemainingTime;
	if (bCoyoteJumpAttempt)
	{
		// CheckJumpInput의 낙하 보정 0→1만 되돌린다. 성공 후 엔진이 직접 0→1과 OnJumped를 처리한다.
		CharacterOwner->JumpCurrentCount = 0;
	}
	const bool bDidJump = Super::DoJump(bReplayingMoves, DeltaTime);
	if (bDidJump)
	{
		bJumpSucceeded = true;
		// 보정으로 저장된 이동을 재생할 때는 같은 점프의 진단 로그를 반복하지 않는다.
		if (bCoyoteJumpAttempt && !bReplayingMoves)
		{
			UE_LOG(LogShooterCoyote, Log,
				TEXT("[CoyoteJump] Success Owner=%s Side=%s Remaining=%.1fms"),
				*GetNameSafe(CharacterOwner), CharacterOwner->HasAuthority() ? TEXT("Server") : TEXT("ClientPrediction"),
				CoyoteRemainingTime * 1000.0f);
		}
		ResetCoyoteTime();
	}
	else if (bCoyoteJumpAttempt)
	{
		CharacterOwner->JumpCurrentCount = PreviousCount;
	}
	return bDidJump;
}

void UShooterCharacterMovementComponent::PerformMovement(float DeltaSeconds)
{
	// 입력은 이동 시작 시각에서 판정한다. 이미 열린 기회만 이번 이동의 시뮬레이션 시간만큼 진행한다.
	if (CoyoteState.bAvailable)
	{
		CoyoteState.RemainingTime -= FMath::Max(DeltaSeconds, 0.0f);
		if (CoyoteState.RemainingTime < 0.0f)
		{
			ResetCoyoteTime();
		}
	}
	Super::PerformMovement(DeltaSeconds);
	// 외부 힘이 적용된 이동에서 발판을 벗어났다면 그 공중 체류 전체를 제외한다.
	if (IsMovingOnGround() && PendingImpulseToApply.IsZero() && PendingForceToApply.IsZero()
		&& PendingLaunchVelocity.IsZero())
	{
		bExternalMovementPending = false;
	}
}

void UShooterCharacterMovementComponent::StartFalling(int32 Iterations, float RemainingTime, float TimeTick,
	const FVector& Delta, const FVector& SubLocation)
{
	const auto* Shooter = Cast<AShooterCharacter>(CharacterOwner);
	const bool bCanStart = IsMovingOnGround() && Shooter && Shooter->GetController()
		&& !Shooter->IsDead() && !Shooter->IsSuitTransitionBlocked() && Shooter->JumpCurrentCount == 0
		&& !bExternalMovementPending;
	if (bCanStart && Shooter->CoyoteTime > 0.0f)
	{
		// 엔진 StartFalling과 같은 거리 보간으로 이번 이동에서 실제 낙하에 쓰일 시간을 구한다.
		const float DesiredDistance = Delta.Size();
		const float ActualDistance = ProjectToGravityFloor(UpdatedComponent->GetComponentLocation() - SubLocation).Size();
		const float FallingTime = DesiredDistance < UE_KINDA_SMALL_NUMBER ? 0.0f
			: RemainingTime + TimeTick * (1.0f - FMath::Min(1.0f, ActualDistance / DesiredDistance));
		CoyoteState.RemainingTime = Shooter->CoyoteTime - FallingTime;
		CoyoteState.bAvailable = CoyoteState.RemainingTime >= 0.0f;
	}
	TGuardValue<bool> NaturalFallGuard(bStartingNaturalFall, bCanStart);
	Super::StartFalling(Iterations, RemainingTime, TimeTick, Delta, SubLocation);
	if (!IsFalling())
	{
		ResetCoyoteTime();
	}
}

void UShooterCharacterMovementComponent::OnMovementModeChanged(EMovementMode PreviousMode, uint8 PreviousCustomMode)
{
	if (!bStartingNaturalFall || MovementMode != MOVE_Falling)
	{
		ResetCoyoteTime();
	}
	if (IsMovingOnGround() && PendingImpulseToApply.IsZero() && PendingForceToApply.IsZero()
		&& PendingLaunchVelocity.IsZero())
	{
		bExternalMovementPending = false;
	}
	Super::OnMovementModeChanged(PreviousMode, PreviousCustomMode);
}

void UShooterCharacterMovementComponent::OnTeleported()
{
	ResetCoyoteTime();
	Super::OnTeleported();
}

void UShooterCharacterMovementComponent::Launch(const FVector& LaunchVelocity)
{
	ResetCoyoteTime();
	bExternalMovementPending = true;
	Super::Launch(LaunchVelocity);
}

void UShooterCharacterMovementComponent::AddImpulse(FVector Impulse, bool bVelocityChange)
{
	if (!Impulse.IsNearlyZero())
	{
		ResetCoyoteTime();
		bExternalMovementPending = true;
	}
	Super::AddImpulse(Impulse, bVelocityChange);
}

void UShooterCharacterMovementComponent::AddForce(FVector Force)
{
	if (!Force.IsNearlyZero())
	{
		ResetCoyoteTime();
		bExternalMovementPending = true;
	}
	Super::AddForce(Force);
}

FNetworkPredictionData_Client* UShooterCharacterMovementComponent::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
	{
		auto* MutableThis = const_cast<UShooterCharacterMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FShooterClientPredictionData(*this);
	}
	return ClientPredictionData;
}

void UShooterCharacterMovementComponent::ServerMoveHandleClientError(float ClientTimeStamp, float DeltaTime,
	const FVector& Accel, const FVector& RelativeClientLocation, UPrimitiveComponent* ClientMovementBase,
	FName ClientBaseBoneName, uint8 ClientMovementMode)
{
	Super::ServerMoveHandleClientError(ClientTimeStamp, DeltaTime, Accel, RelativeClientLocation,
		ClientMovementBase, ClientBaseBoneName, ClientMovementMode);
	if (GetPredictionData_Server_Character()->PendingAdjustment.TimeStamp == ClientTimeStamp)
	{
		// 전송은 나중에 일어날 수 있으므로 보정 대상 이동이 끝난 시점의 상태를 보관한다.
		PendingServerSnapshot = CaptureJumpSnapshot();
	}
}

void FShooterMoveResponse::ServerFillResponseData(const UCharacterMovementComponent& Movement,
	const FClientAdjustment& PendingAdjustment)
{
	FCharacterMoveResponseDataContainer::ServerFillResponseData(Movement, PendingAdjustment);
	JumpSnapshot = CastChecked<UShooterCharacterMovementComponent>(&Movement)->PendingServerSnapshot;
}

bool FShooterMoveResponse::Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* PackageMap)
{
	if (!FCharacterMoveResponseDataContainer::Serialize(Movement, Ar, PackageMap))
	{
		return false;
	}
	if (IsCorrection())
	{
		Ar << JumpSnapshot.Coyote.RemainingTime;
		Ar << JumpSnapshot.Coyote.bAvailable;
		Ar << JumpSnapshot.JumpCount;
		Ar << JumpSnapshot.HoldTime;
		Ar << JumpSnapshot.ForceTime;
		Ar << JumpSnapshot.bWasJumping;
		Ar << JumpSnapshot.bPressedJump;
		Ar << JumpSnapshot.bExternalMovementPending;
	}
	return !Ar.IsError();
}

void UShooterCharacterMovementComponent::ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& Response)
{
	Super::ClientHandleMoveResponse(Response);
	const auto* ClientData = GetPredictionData_Client_Character();
	if (Response.IsCorrection() && ClientData->bUpdatePosition && ClientData->LastAckedMove.IsValid()
		&& ClientData->LastAckedMove->TimeStamp == Response.ClientAdjustment.TimeStamp)
	{
		const bool bLocalPressedJump = CharacterOwner->bPressedJump;
		const auto& Snapshot = static_cast<const FShooterMoveResponse&>(Response).JumpSnapshot;
		RestoreJumpSnapshot(Snapshot);
		// 현재 로컬 입력은 보존하고, 이동 재생을 시작할 때만 서버의 입력 기준 상태로 바꾼다.
		CharacterOwner->bPressedJump = bLocalPressedJump;
		bCorrectedPressedJump = Snapshot.bPressedJump;
		bHasCorrectedJumpInput = true;
	}
}

bool UShooterCharacterMovementComponent::ClientUpdatePositionAfterServerUpdate()
{
	if (!bHasCorrectedJumpInput || !CharacterOwner)
	{
		return Super::ClientUpdatePositionAfterServerUpdate();
	}
	const bool bLocalPressedJump = CharacterOwner->bPressedJump;
	CharacterOwner->bPressedJump = bCorrectedPressedJump;
	bHasCorrectedJumpInput = false;
	const bool bReplayed = Super::ClientUpdatePositionAfterServerUpdate();
	CharacterOwner->bPressedJump = bLocalPressedJump;
	return bReplayed;
}

void UShooterCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	const bool bWasPressed = CharacterOwner && CharacterOwner->bPressedJump;
	Super::UpdateFromCompressedFlags(Flags);
	if (CharacterOwner && !CharacterOwner->HasAuthority() && CharacterOwner->bClientUpdating)
	{
		// 엔진은 서버에서만 입력 전환을 복원한다. 클라이언트 재생도 놓기/재입력에 맞춰 Hold를 초기화한다.
		if (!CharacterOwner->bPressedJump)
		{
			CharacterOwner->StopJumping();
		}
		else if (!bWasPressed)
		{
			CharacterOwner->Jump();
		}
	}
}
