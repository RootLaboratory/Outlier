#include "Drone/Partner/PartnerMovementComponent.h"

#include "Components/CapsuleComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "OutlierPlayerState.h"
#include "Shooter/ShooterCharacter.h"

DEFINE_LOG_CATEGORY_STATIC(LogPartnerFollow, Log, All);

UPartnerMovementComponent::UPartnerMovementComponent()
	: FollowMode(EPartnerMoveMode::Normal)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetIsReplicatedByDefault(true);
}

void UPartnerMovementComponent::BeginPlay()
{
	RefreshCharacterRefsFromPlayerState();
	ApplyPartnerFlightSettings();

	Super::BeginPlay();

	RefreshMovementState();
}

void UPartnerMovementComponent::RefreshCharacterRefsFromPlayerState()
{
	AShooterCharacter* PreviousShooter = ShooterCharacter;
	PartnerCharacter = Cast<APartnerCharacter>(GetOwner());
	if (!PartnerCharacter)
	{
		ShooterCharacter = nullptr;
		return;
	}

	AOutlierPlayerState* PS = PartnerCharacter->GetPlayerState<AOutlierPlayerState>();
	ShooterCharacter = PS ? PS->GetShooterCharacter() : nullptr;
	MovementCapsule = PartnerCharacter->GetCapsuleComponent();
	CharacterMovement = PartnerCharacter->GetCharacterMovement();
	if (PreviousShooter != ShooterCharacter && bFollowWasActive)
	{
		// 다른 Shooter의 경로가 남지 않도록 종료한다. 새 대상은 다시 누른 입력으로 시작한다.
		StopAutoFollow();
	}
}

void UPartnerMovementComponent::RefreshMovementState()
{
	if (!PartnerCharacter || !PartnerCharacter->HasAuthority())
	{
		return;
	}

	const EDroneMovementState NewState = IsAutoFollowMoveMode()
		? EDroneMovementState::Follow
		: EDroneMovementState::Fly;
	PartnerCharacter->SetMovementState(NewState);
}

void UPartnerMovementComponent::ApplyCameraAssist()
{
	StartAutoFollow(EPartnerMoveMode::CameraAssist);
}

FPartnerFollowInputState& UPartnerMovementComponent::GetFollowInput(EPartnerMoveMode Mode)
{
	return Mode == EPartnerMoveMode::CameraAssist ? CameraAssistInput : SyncMoveInput;
}

void UPartnerMovementComponent::StartAutoFollow(EPartnerMoveMode Mode)
{
	FPartnerFollowInputState& Input = GetFollowInput(Mode);
	if (!PartnerCharacter || !PartnerCharacter->CanAcceptInput() || !Input.Press())
	{
		return;
	}

	if (PartnerCharacter->HasAuthority())
	{
		PartnerCharacter->ApplyMoveMode(Mode);
	}
	else
	{
		// 입력은 즉시 예측하지만 경로 선정은 서버만 수행한다.
		PartnerCharacter->ApplyMoveMode(Mode);
		ServerSetAutoFollowInput(Mode, true, Input.Serial);
	}
}

void UPartnerMovementComponent::StopAutoFollow()
{
	const bool bWasActive = bFollowWasActive;
	GetFollowInput(FollowMode).Stop();
	if (bFollowWasActive || !FollowPath.IsEmpty())
	{
		ClearFollowPath();
	}
	if (IsAutoFollowMoveMode())
	{
		PartnerCharacter->ApplyMoveMode(EPartnerMoveMode::Normal);
	}
	if (!bWasActive && PartnerCharacter && PartnerCharacter->HasAuthority())
	{
		PublishFollowPath(true);
	}
}

void UPartnerMovementComponent::ReleaseCameraAssistInput()
{
	ReleaseAutoFollow(EPartnerMoveMode::CameraAssist);
}

void UPartnerMovementComponent::ReleaseAutoFollow(EPartnerMoveMode Mode)
{
	FPartnerFollowInputState& Input = GetFollowInput(Mode);
	const uint32 InputSerial = Input.Serial;
	if (!Input.Release(InputSerial))
	{
		return;
	}
	if (PartnerCharacter && PartnerCharacter->MoveMode == Mode)
	{
		StopAutoFollow();
	}
	if (PartnerCharacter && !PartnerCharacter->HasAuthority() && PartnerCharacter->IsLocallyControlled())
	{
		ServerSetAutoFollowInput(Mode, false, InputSerial);
	}
}

void UPartnerMovementComponent::ServerSetAutoFollowInput_Implementation(EPartnerMoveMode Mode, bool bHeld, uint32 InputSerial)
{
	if (!PartnerCharacter || (Mode != EPartnerMoveMode::CameraAssist && Mode != EPartnerMoveMode::SyncMove))
	{
		return;
	}
	FPartnerFollowInputState& Input = GetFollowInput(Mode);
	if (!bHeld)
	{
		if (Input.Release(InputSerial) && PartnerCharacter->MoveMode == Mode)
		{
			StopAutoFollow();
		}
		return;
	}
	if (!Input.AcceptPress(InputSerial))
	{
		return;
	}
	if (!PartnerCharacter->CanAcceptInput())
	{
		Input.Stop();
		ClientReceiveFollowPath(Mode, InputSerial, TArray<FVector>(), true);
		return;
	}
	PartnerCharacter->ApplyMoveMode(Mode);
}

void UPartnerMovementComponent::ResetAutoFollowForOwnerChange()
{
	StopAutoFollow();
	CameraAssistInput = FPartnerFollowInputState();
	SyncMoveInput = FPartnerFollowInputState();
	FollowRetry.Reset();
}

void UPartnerMovementComponent::SetSyncMove(bool SyncMove)
{
	if (SyncMove)
	{
		StartAutoFollow(EPartnerMoveMode::SyncMove);
	}
	else
	{
		ReleaseAutoFollow(EPartnerMoveMode::SyncMove);
	}
}

void UPartnerMovementComponent::SetFreeMove(bool FreeMove)
{
	if (!PartnerCharacter || IsAutoFollowMoveMode())
	{
		return;
	}

	const EPartnerMoveMode TargetMode = FreeMove
		? EPartnerMoveMode::FreeMove
		: EPartnerMoveMode::Normal;

	if (PartnerCharacter->MoveMode == TargetMode)
	{
		return;
	}

	PartnerCharacter->SetMoveMode(TargetMode);
}

void UPartnerMovementComponent::SetMoveInput(const FVector2D& MoveInput)
{
	Super::SetMoveInput(IsAutoFollowMoveMode() ? FVector2D::ZeroVector : MoveInput);
}

void UPartnerMovementComponent::SetVerticalInput(float Axis)
{
	Super::SetVerticalInput(IsAutoFollowMoveMode() ? 0.0f : Axis);
}

void UPartnerMovementComponent::OnMoveModeChanged(EPartnerMoveMode NewMode)
{
	ResetMovementFeel();
	if (bFollowWasActive && NewMode != FollowMode)
	{
		bFollowWasActive = false;
		GetFollowInput(FollowMode).Stop();
		ClearFollowPath();
		if (PartnerCharacter && PartnerCharacter->HasAuthority())
		{
			PublishFollowPath(true);
		}
	}
	FollowMode = NewMode;
	if (IsAutoFollowMoveMode() && !bFollowWasActive)
	{
		ClearFollowPath();
		FollowRetry.Reset();
		FollowNextRepathAt = 0.0;
		FollowNextPathCheckAt = 0.0;
		bFollowWasActive = true;
	}

	if (NewMode == EPartnerMoveMode::SyncMove && PartnerCharacter &&
		(PartnerCharacter->HasAuthority() || PartnerCharacter->IsLocallyControlled()))
	{
		EnterSyncMove();
	}

	if (IsAutoFollowMoveMode())
	{
		ClearFlightInput();
	}

	RefreshMovementState();
	RefreshTickEnabled();
}

void UPartnerMovementComponent::OnReplicatedMoveMode()
{
	if (!PartnerCharacter)
	{
		return;
	}
	if (PartnerCharacter->IsLocallyControlled())
	{
		// MoveMode 복제에는 입력 식별자가 없다. 소유자의 새 입력은 이전 Normal 복제로 취소하지 않는다.
		// 서버 종료는 Mode/Serial이 포함된 경로 RPC로 적용하며, 종료된 입력의 늦은 모드 복제도 무시한다.
		if (bFollowWasActive && GetFollowInput(FollowMode).bActive)
		{
			PartnerCharacter->MoveMode = FollowMode;
		}
		else if (IsAutoFollowMoveMode() && !GetFollowInput(PartnerCharacter->MoveMode).bActive)
		{
			PartnerCharacter->MoveMode = EPartnerMoveMode::Normal;
		}
	}
	OnMoveModeChanged(PartnerCharacter->MoveMode);
}

void UPartnerMovementComponent::ApplyPartnerFlightSettings()
{
	if (!PartnerCharacter)
	{
		return;
	}

	SetMoveSpeed(PartnerCharacter->MoveSpeed);
	SetBoostSpeed(PartnerCharacter->BoostSpeed);
	SetVerticalSpeed(PartnerCharacter->VerticalSpeed);
	SetAcceleration(PartnerCharacter->Acceleration);
	SetDeceleration(PartnerCharacter->Deceleration);
	SetRotationLagAmount(PartnerCharacter->RotationLagAmount);
	SetRotationLagRecoverSpeed(PartnerCharacter->RotationLagRecoverSpeed);
	SetCameraPitchOnMove(PartnerCharacter->CameraPitchOnMove);
	SetCameraRollOnTurn(PartnerCharacter->CameraRollOnTurn);
	SetCameraRollInterpSpeed(PartnerCharacter->CameraRollInterpSpeed);
	SetMeshInertialTiltScale(PartnerCharacter->MeshInertialTiltScale);
	SetCameraInertialTiltScale(PartnerCharacter->CameraInertialTiltScale);
	SetViewModelInertialTiltScale(PartnerCharacter->ViewModelInertialTiltScale);
	SetInertialTiltReboundRatio(PartnerCharacter->InertialTiltReboundRatio);
	SetInertialTiltReboundMaxScale(PartnerCharacter->InertialTiltReboundMaxScale);
	SetInertialTiltReboundInterpMultiplier(PartnerCharacter->InertialTiltReboundInterpMultiplier);
	SetInertialTiltRecoverInterpMultiplier(PartnerCharacter->InertialTiltRecoverInterpMultiplier);
	SetAccelerating(PartnerCharacter->bIsAccelerate);
}

ACharacter* UPartnerMovementComponent::GetFlightOwnerCharacter() const
{
	return PartnerCharacter;
}

USceneComponent* UPartnerMovementComponent::GetFlightVisualTiltRoot() const
{
	return PartnerCharacter ? PartnerCharacter->GetThirdPersonTiltRoot() : nullptr;
}

USceneComponent* UPartnerMovementComponent::GetFlightViewModelRoot() const
{
	return PartnerCharacter ? PartnerCharacter->GetFirstPersonViewModelRoot() : nullptr;
}

bool UPartnerMovementComponent::CanRunInputMovement() const
{
	return PartnerCharacter &&
		!PartnerCharacter->IsSuitTransitionBlocked() &&
		!IsAutoFollowMoveMode() &&
		(PartnerCharacter->HasAuthority() || PartnerCharacter->IsLocallyControlled());
}

bool UPartnerMovementComponent::ShouldUpdateMovementFeel() const
{
	if (!PartnerCharacter || PartnerCharacter->GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}

	return PartnerCharacter->IsLocallyControlled() || PartnerCharacter->WasRecentlyRendered();
}

EFlightInputMode UPartnerMovementComponent::GetFlightInputMode() const
{
	return PartnerCharacter && PartnerCharacter->MoveMode == EPartnerMoveMode::Normal
		? EFlightInputMode::Horizontal
		: EFlightInputMode::Free;
}

void UPartnerMovementComponent::OnAfterInputMovement(float DeltaTime)
{
	if (!PartnerCharacter)
	{
		return;
	}
	if (!PartnerCharacter->CanAcceptInput())
	{
		if (bFollowWasActive)
		{
			StopAutoFollow();
		}
		return;
	}

	const bool bCanRunServerMovement = PartnerCharacter->HasAuthority();
	const bool bCanRunPredictedFollowMovement =
		PartnerCharacter->IsLocallyControlled() && IsAutoFollowMoveMode();

	if (!bCanRunServerMovement && !bCanRunPredictedFollowMovement)
	{
		return;
	}

	switch (PartnerCharacter->MoveMode)
	{
	case EPartnerMoveMode::CameraAssist:
	case EPartnerMoveMode::SyncMove:
		UpdateAutoFollow(DeltaTime);
		break;
	default:
		break;
	}
}

bool UPartnerMovementComponent::IsAutoFollowMoveMode() const
{
	return PartnerCharacter &&
		(
			PartnerCharacter->MoveMode == EPartnerMoveMode::CameraAssist ||
			PartnerCharacter->MoveMode == EPartnerMoveMode::SyncMove
		);
}

void UPartnerMovementComponent::EnterSyncMove()
{
	if (!PartnerCharacter || !ShooterCharacter)
	{
		return;
	}

	PartnerCharacter->SyncLocalOffset = PartnerCharacter->GetActorLocation() - ShooterCharacter->GetActorLocation();
}

void UPartnerMovementComponent::UpdateAutoFollow(float DeltaTime)
{
	if (!IsValid(ShooterCharacter) || !MovementCapsule || !CharacterMovement)
	{
		if (PartnerCharacter->HasAuthority())
		{
			StopAutoFollow();
		}
		return;
	}
	// 서버는 제한된 주기로 선정/검증하고, 소유 클라이언트는 전달받은 경로만 추종한다.
	if (PartnerCharacter->HasAuthority())
	{
		UpdateFollowPath(GetWorld()->GetTimeSeconds(), GetAutoFollowTarget());
	}
	if (IsAutoFollowMoveMode())
	{
		FollowStoredPath(DeltaTime);
	}
}

FVector UPartnerMovementComponent::GetAutoFollowTarget() const
{
	const FVector CapsuleOffset = MovementCapsule->GetComponentLocation() - PartnerCharacter->GetActorLocation();
	if (FollowMode == EPartnerMoveMode::SyncMove)
	{
		return ShooterCharacter->GetActorLocation() + PartnerCharacter->SyncLocalOffset + CapsuleOffset;
	}
	const FVector ShooterLocation = ShooterCharacter->GetActorLocation();
	const FVector Right = ShooterCharacter->GetActorRightVector();
	const FVector Up = FVector::UpVector;
	const FVector Forward = ShooterCharacter->GetActorForwardVector();
	const FVector LocalOffset = PartnerCharacter->AssistTargetLocalOffset;
	return
		ShooterLocation +
		-Forward * LocalOffset.X +
		Right * LocalOffset.Y +
		Up * LocalOffset.Z + CapsuleOffset;
}

FFlightPathRequest UPartnerMovementComponent::MakeFollowPathRequest(const FVector& Goal) const
{
	FFlightPathRequest Request;
	Request.Start = MovementCapsule->GetComponentLocation();
	Request.Goal = Goal;
	Request.SearchBounds = FBox(Request.Start.ComponentMin(Goal), Request.Start.ComponentMax(Goal))
		.ExpandBy(FMath::Max(FollowPathMargin, 100.0f));
	Request.CellSize = FMath::Max(FollowPathCellSize, 25.0f);
	Request.Clearance = FMath::Max(FollowPathClearance, 0.0f);
	Request.GoalSearchRadius = FMath::Max(FollowGoalSearchRadius, 0.0f);
	Request.MaxSearchNodes = FMath::Max(FollowMaxSearchNodes, 1);
	Request.MaxCollisionChecks = FMath::Max(FollowMaxCollisionChecks, 1);
	Request.MaxSearchMilliseconds = FMath::Max(FollowMaxSearchMilliseconds, 0.1f);
	// Partner는 RoomTag와 무관하다. 실제 캡슐 충돌과 차단막은 공용 탐색에서 그대로 검사한다.
	Request.bRequireRoom = false;
	return Request;
}

float UPartnerMovementComponent::GetFollowAcceptanceRadius() const
{
	return FollowMode == EPartnerMoveMode::CameraAssist
		? FMath::Max(PartnerCharacter->AssistMinDistance, 10.0f)
		: 25.0f;
}

void UPartnerMovementComponent::UpdateFollowPath(double Now, const FVector& Goal)
{
	if (FollowRetry.IsExhausted(Now, FollowMaxRetryAttempts, FollowRetryTimeout))
	{
		UE_LOG(LogPartnerFollow, Warning, TEXT("[Follow] Stop Partner=%s Attempts=%d Reason=RetryLimit ReleaseRequired=1"),
			*GetNameSafe(PartnerCharacter), FollowRetry.Failures);
		StopAutoFollow();
		return;
	}

	const FVector Current = MovementCapsule->GetComponentLocation();
	if (!FollowPath.IsEmpty() && FVector::DistSquared(Current, FollowProgressLocation) >= FMath::Square(25.0f))
	{
		FollowProgressLocation = Current;
		FollowLastProgressAt = Now;
		FollowRetry.Reset();
	}

	// 이동 중 막힘/정체는 기존 경로를 버리고 재시도한다. 벽을 통과하는 직선 대체 이동은 하지 않는다.
	if (FollowPath.IsValidIndex(FollowWaypoint) && Now >= FollowNextPathCheckAt)
	{
		FollowNextPathCheckAt = Now + FMath::Max(FollowRepathInterval, 0.05f);
		FFlightPathRequest Segment = MakeFollowPathRequest(FollowPath[FollowWaypoint]);
		Segment.GoalSearchRadius = 0.0f;
		const EFlightPathStatus Status = FlightPathfinding::CheckSegment(*GetWorld(), *MovementCapsule, Segment);
		if (Status != EFlightPathStatus::Success)
		{
			FailFollowPath(Now, Status);
			return;
		}
		if (Now - FollowLastProgressAt >= FMath::Max(FollowNoProgressTimeout, 0.1f))
		{
			UE_LOG(LogPartnerFollow, Warning, TEXT("[Follow] Stalled Partner=%s Mode=%s Seconds=%.2f"),
				*GetNameSafe(PartnerCharacter), *UEnum::GetValueAsString(FollowMode), Now - FollowLastProgressAt);
			FailFollowPath(Now, EFlightPathStatus::BlockedSegment);
			return;
		}
	}

	const bool bTargetMoved = FVector::DistSquared(Goal, FollowTargetSnapshot)
		>= FMath::Square(FMath::Max(FollowTargetUpdateDistance, 10.0f));
	if (bFollowHolding && !bTargetMoved && FVector::DistSquared(Current, FollowHoldLocation)
		<= FMath::Square(GetFollowAcceptanceRadius()))
	{
		return;
	}
	if (Now >= FollowNextRepathAt && Now >= FollowRetry.NextAttemptAt && (FollowPath.IsEmpty() || bTargetMoved))
	{
		// 도착해서 대기 중이면 작은 타깃 움직임마다 같은 경로를 다시 만들지 않는다.
		if (FollowPath.IsEmpty() && FVector::DistSquared(Current, Goal)
			<= FMath::Square(GetFollowAcceptanceRadius()))
		{
			FollowRetry.Reset();
			return;
		}
		FindFollowPath(Now, Goal);
	}
}

void UPartnerMovementComponent::FindFollowPath(double Now, const FVector& Goal)
{
	FollowNextRepathAt = Now + FMath::Max(FollowRepathInterval, 0.05f);
	const FFlightPathResult Result = FlightPathfinding::FindPath(*GetWorld(), *MovementCapsule, MakeFollowPathRequest(Goal));
	UE_LOG(LogPartnerFollow, Log, TEXT("[Follow] Path Partner=%s Mode=%s Status=%s Points=%d Checks=%d Nodes=%d Ms=%.3f GoalAdjusted=%d"),
		*GetNameSafe(PartnerCharacter), *UEnum::GetValueAsString(FollowMode), *UEnum::GetValueAsString(Result.Status), Result.Waypoints.Num(),
		Result.CollisionChecks, Result.SearchNodes, Result.ElapsedMilliseconds, Result.bGoalAdjusted);
	if (Result.Status != EFlightPathStatus::Success || Result.Waypoints.IsEmpty())
	{
		FailFollowPath(Now, Result.Status);
		return;
	}
	const bool bWasWaitingForPath = FollowPath.IsEmpty();
	FollowPath = Result.Waypoints;
	FollowWaypoint = FollowPath.Num() > 1 ? 1 : 0;
	FollowTargetSnapshot = Goal;
	// 경로 성공만으로 실패 이력을 지우지 않는다. 실제 25cm 이동 후에만 정상 진행으로 취급한다.
	if (bWasWaitingForPath)
	{
		FollowProgressLocation = MovementCapsule->GetComponentLocation();
		FollowLastProgressAt = Now;
	}
	bFollowHolding = false;
	PublishFollowPath(false);
}

void UPartnerMovementComponent::FailFollowPath(double Now, EFlightPathStatus Status)
{
	FollowRetry.Fail(Now, FollowRepathInterval);
	ClearFollowPath();
	PublishFollowPath(false);
	UE_LOG(LogPartnerFollow, Warning, TEXT("[Follow] Retry Partner=%s Status=%s Attempt=%d/%d"),
		*GetNameSafe(PartnerCharacter), *UEnum::GetValueAsString(Status), FollowRetry.Failures, FollowMaxRetryAttempts);
}

void UPartnerMovementComponent::FollowStoredPath(float DeltaTime)
{
	if (!FollowPath.IsValidIndex(FollowWaypoint) || DeltaTime <= KINDA_SMALL_NUMBER)
	{
		StopFollowMovement();
		return;
	}
	const FVector Current = MovementCapsule->GetComponentLocation();
	const FVector ToWaypoint = FollowPath[FollowWaypoint] - Current;
	const bool bFinalWaypoint = FollowWaypoint == FollowPath.Num() - 1;
	const float Acceptance = bFinalWaypoint ? GetFollowAcceptanceRadius() : 20.0f;
	if (ToWaypoint.SizeSquared() <= FMath::Square(Acceptance))
	{
		if (!bFinalWaypoint && PartnerCharacter->HasAuthority())
		{
			// 허용 반경으로 모서리를 일찍 잘라가는 경우에도 실제 위치에서 다음 구간을 다시 확인한다.
			FFlightPathRequest Segment = MakeFollowPathRequest(FollowPath[FollowWaypoint + 1]);
			Segment.GoalSearchRadius = 0.0f;
			if (FlightPathfinding::CheckSegment(*GetWorld(), *MovementCapsule, Segment) != EFlightPathStatus::Success)
			{
				FailFollowPath(GetWorld()->GetTimeSeconds(), EFlightPathStatus::BlockedSegment);
				return;
			}
		}
		StopFollowMovement();
		++FollowWaypoint;
		if (bFinalWaypoint)
		{
			FollowHoldLocation = FollowPath.Last();
			UE_LOG(LogPartnerFollow, Log, TEXT("[Follow] Arrived Partner=%s"), *GetNameSafe(PartnerCharacter));
			ClearFollowPath();
			bFollowHolding = true;
			FollowRetry.Reset();
			if (PartnerCharacter->HasAuthority())
			{
				PublishFollowPath(false);
			}
		}
		return;
	}
	const bool bCameraAssist = FollowMode == EPartnerMoveMode::CameraAssist;
	const float MaxDistance = FMath::Max(bCameraAssist ? PartnerCharacter->AssistMaxDistance
		: PartnerCharacter->SyncMoveDistance, Acceptance + 1.0f);
	const float DistanceScale = FMath::Clamp(ToWaypoint.Size() / MaxDistance, 0.15f, 1.0f);
	const float Strength = bCameraAssist ? FMath::Clamp(PartnerCharacter->AssistStrength / 100.0f, 0.0f, 1.0f) : 1.0f;
	const float InterpSpeed = bCameraAssist ? PartnerCharacter->AssistInterpSpeed : PartnerCharacter->SyncMoveInterpSpeed;
	const float InterpScale = FMath::Clamp(InterpSpeed / 8.0f, 0.1f, 1.0f);
	PartnerCharacter->AddMovementInput(ToWaypoint.GetSafeNormal(), DistanceScale * Strength * InterpScale);
}

void UPartnerMovementComponent::StopFollowMovement()
{
	if (PartnerCharacter)
	{
		PartnerCharacter->ConsumeMovementInputVector();
	}
	if (CharacterMovement)
	{
		CharacterMovement->StopMovementImmediately();
	}
}

void UPartnerMovementComponent::ClearFollowPath()
{
	FollowPath.Reset();
	FollowWaypoint = 0;
	bFollowHolding = false;
	StopFollowMovement();
}

void UPartnerMovementComponent::PublishFollowPath(bool bStopped)
{
	if (PartnerCharacter->HasAuthority() && !PartnerCharacter->IsLocallyControlled()
		&& PartnerCharacter->GetNetConnection())
	{
		// 매 Tick 위치를 보내지 않는다. 선정/취소 시에만 경로를 전달하고 위치 복제는 CharacterMovement가 담당한다.
		ClientReceiveFollowPath(FollowMode, GetFollowInput(FollowMode).Serial, FollowPath, bStopped);
	}
}

void UPartnerMovementComponent::ClientReceiveFollowPath_Implementation(
	EPartnerMoveMode Mode, uint32 InputSerial, const TArray<FVector>& Points, bool bStopped)
{
	// 이전 입력의 늦은 결과가 새 추종이나 수동 조작을 덮어쓰지 못하게 한다.
	if (!PartnerCharacter || PartnerCharacter->MoveMode != Mode || !GetFollowInput(Mode).AcceptsPath(InputSerial))
	{
		return;
	}
	ClearFollowPath();
	if (bStopped)
	{
		StopAutoFollow();
		return;
	}
	if (!PartnerCharacter->CanAcceptInput())
	{
		return;
	}
	FollowPath = Points;
	FollowWaypoint = FollowPath.Num() > 1 ? 1 : 0;
}
