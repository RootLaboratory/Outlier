#include "Drone/Partner/PartnerMovementComponent.h"

#include "Components/CapsuleComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Net/UnrealNetwork.h"
#include "Upgrade/OutlierUpgradeComponent.h"
#include "OutlierPlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Shooter/ShooterCharacter.h"

DEFINE_LOG_CATEGORY_STATIC(LogPartnerFollow, Log, All);
static TAutoConsoleVariable<int32> CVarPartnerFollowDebug(TEXT("outlier.Partner.FollowDebug"), 0,
	TEXT("Auto follow diagnostics: 1=activation, references and one-second movement samples."));
static TAutoConsoleVariable<int32> CVarPartnerFollowPathDebug(TEXT("outlier.Partner.FollowPathDebug"), 0,
	TEXT("Draw server follow paths for 3 seconds: cyan=start, yellow=requested goal, magenta=resolved goal, red=failed direct route."));

UPartnerMovementComponent::UPartnerMovementComponent()
	: FollowMode(EPartnerMoveMode::Normal)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetIsReplicatedByDefault(true);
}

float FPartnerEvasiveBurstState::GetMultiplier(double Now) const
{
	if (!FMath::IsFinite(Now) || !FMath::IsFinite(StartedAt)
		|| !FMath::IsFinite(Duration) || Duration <= 0.0f
		|| !FMath::IsFinite(Multiplier) || Multiplier <= 1.0f)
	{
		return 1.0f;
	}
	const float Progress = static_cast<float>(FMath::Clamp((Now - StartedAt) / Duration, 0.0, 1.0));
	return FMath::Lerp(Multiplier, 1.0f, Progress);
}

void UPartnerMovementComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UPartnerMovementComponent, EvasiveBurst);
}

double UPartnerMovementComponent::GetBurstServerTime() const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	return GameState ? GameState->GetServerWorldTimeSeconds() : (World ? World->GetTimeSeconds() : 0.0);
}

void UPartnerMovementComponent::HandleBoostStateChanged(bool bBoosting)
{
	if (!PartnerCharacter || !PartnerCharacter->HasAuthority()) return;

	EvasiveBurst = FPartnerEvasiveBurstState();
	const UOutlierUpgradeComponent* Upgrade = PartnerCharacter->GetUpgradeComponent();
	const UOutlierAbilitySystemComponent* ASC = PartnerCharacter->GetOutlierAbilitySystemComponent();
	if (bBoosting && PartnerCharacter->CanAcceptInput() && Upgrade
		&& Upgrade->HasUpgradeTag(OutlierGameplayTags::Upgrade::Partner::Mobility::EvasiveBurst())
		&& ASC && ASC->IsPartnerAbilitiesConfigured())
	{
		const FOutlierPartnerAbilityConfig& Config = ASC->GetPartnerAbilityConfig();
		EvasiveBurst.StartedAt = GetBurstServerTime();
		EvasiveBurst.Multiplier = Config.EvasiveBurstMultiplier;
		EvasiveBurst.Duration = Config.EvasiveBurstDuration;
	}
	RefreshBoostSpeed(true);
	PartnerCharacter->ForceNetUpdate();
}

float UPartnerMovementComponent::GetEffectiveBoostSpeed() const
{
	if (!PartnerCharacter) return BoostSpeed;
	const float Weight = PartnerCharacter->bIsAccelerate && PartnerCharacter->CanAcceptInput()
		? EvasiveBurst.GetMultiplier(GetBurstServerTime()) : 1.0f;
	return PartnerCharacter->BoostSpeed * Weight;
}

void UPartnerMovementComponent::RefreshBoostSpeed(bool bForce)
{
	if (!PartnerCharacter) return;
	const float PreviousSpeed = BoostSpeed;
	SetBoostSpeed(GetEffectiveBoostSpeed());
	if (CharacterMovement && !IsAutoFollowMoveMode()
		&& (bForce || !FMath::IsNearlyEqual(PreviousSpeed, BoostSpeed)))
	{
		CharacterMovement->MaxFlySpeed = PartnerCharacter->bIsAccelerate ? BoostSpeed : PartnerCharacter->MoveSpeed;
	}
}

void UPartnerMovementComponent::OnRep_EvasiveBurst()
{
	if (!PartnerCharacter) RefreshCharacterRefsFromPlayerState();
	RefreshBoostSpeed(true);
}

void UPartnerMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	if (PartnerCharacter && PartnerCharacter->HasAuthority() && EvasiveBurst.Duration > 0.0f)
	{
		const UOutlierUpgradeComponent* Upgrade = PartnerCharacter->GetUpgradeComponent();
		if (!PartnerCharacter->CanAcceptInput() || !PartnerCharacter->bIsAccelerate || !Upgrade
			|| !Upgrade->HasUpgradeTag(OutlierGameplayTags::Upgrade::Partner::Mobility::EvasiveBurst()))
		{
			HandleBoostStateChanged(false);
		}
	}
	RefreshBoostSpeed();
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
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
	if (!PartnerCharacter || !PartnerCharacter->CanAcceptInput())
	{
		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		if (CVarPartnerFollowDebug.GetValueOnGameThread() > 0 && Now >= FollowNextDiagnosticAt)
		{
			FollowNextDiagnosticAt = Now + 1.0;
			UE_LOG(LogPartnerFollow, Log, TEXT("[Follow] StartRejected Partner=%s Mode=%s Reason=InputBlocked"),
				*GetNameSafe(PartnerCharacter), *UEnum::GetValueAsString(Mode));
		}
		return;
	}
	if (!Input.Press())
	{
		// Triggered는 매 프레임 호출되므로 실패 후 해제가 필요한 상황도 로그를 반복하지 않는다.
		return;
	}
	RefreshCharacterRefsFromPlayerState();
	UE_LOG(LogPartnerFollow, Log, TEXT("[Follow] Start Partner=%s Shooter=%s Mode=%s Authority=%d Serial=%u Strength=%.1f Acceptance=%.1f"),
		*GetNameSafe(PartnerCharacter), *GetNameSafe(ShooterCharacter), *UEnum::GetValueAsString(Mode),
		PartnerCharacter->HasAuthority(), Input.Serial, PartnerCharacter->AssistStrength, PartnerCharacter->AssistMinDistance);

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
	if (bFollowWasActive || !FollowPath.IsEmpty() || FollowSearch)
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
		UE_LOG(LogPartnerFollow, Log, TEXT("[Follow] ServerRejected Partner=%s Mode=%s Serial=%u Reason=StaleOrHeldInput"),
			*GetNameSafe(PartnerCharacter), *UEnum::GetValueAsString(Mode), InputSerial);
		return;
	}
	if (!PartnerCharacter->CanAcceptInput())
	{
		UE_LOG(LogPartnerFollow, Log, TEXT("[Follow] ServerRejected Partner=%s Mode=%s Serial=%u Reason=InputBlocked"),
			*GetNameSafe(PartnerCharacter), *UEnum::GetValueAsString(Mode), InputSerial);
		Input.Stop();
		ClientReceiveFollowPath(Mode, InputSerial, TArray<FVector>(), true);
		return;
	}
	RefreshCharacterRefsFromPlayerState();
	UE_LOG(LogPartnerFollow, Log, TEXT("[Follow] ServerStart Partner=%s Shooter=%s Mode=%s Serial=%u"),
		*GetNameSafe(PartnerCharacter), *GetNameSafe(ShooterCharacter), *UEnum::GetValueAsString(Mode), InputSerial);
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
		FollowLastFailure = EFlightPathStatus::Success;
		FollowNextPathCheckAt = 0.0;
		FollowNextDiagnosticAt = 0.0;
		bFollowWasActive = true;
		FollowRouteHistory.Reset();
		FollowRouteSegments = 0;
		FollowBestGoalDistance = TNumericLimits<double>::Max();
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
	RefreshBoostSpeed(true);
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
			UE_LOG(LogPartnerFollow, Log, TEXT("[Follow] Stop Partner=%s Reason=InputBlocked"), *GetNameSafe(PartnerCharacter));
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
			UE_LOG(LogPartnerFollow, Warning, TEXT("[Follow] Stop Partner=%s Reason=MissingReference Shooter=%s Capsule=%s Movement=%s"),
				*GetNameSafe(PartnerCharacter), *GetNameSafe(ShooterCharacter), *GetNameSafe(MovementCapsule), *GetNameSafe(CharacterMovement));
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
	const double Now = GetWorld()->GetTimeSeconds();
	if (CVarPartnerFollowDebug.GetValueOnGameThread() > 0 && Now >= FollowNextDiagnosticAt)
	{
		FollowNextDiagnosticAt = Now + 1.0;
		const FVector ToWaypoint = FollowPath.IsValidIndex(FollowWaypoint)
			? FollowPath[FollowWaypoint] - MovementCapsule->GetComponentLocation() : FVector::ZeroVector;
		UE_LOG(LogPartnerFollow, Log, TEXT("[Follow] Sample Partner=%s Shooter=%s Authority=%d Local=%d Mode=%s Distance=%.1f Points=%d Waypoint=%d Speed=%.1f Limit=%.1f MovementMode=%d Controller=%s InputIgnored=%d Holding=%d Searching=%d SearchChecks=%d SearchNodes=%d WaypointDistance=%.1f VelocityDot=%.2f NoProgressSeconds=%.2f"),
			*GetNameSafe(PartnerCharacter), *GetNameSafe(ShooterCharacter), PartnerCharacter->HasAuthority(),
			PartnerCharacter->IsLocallyControlled(), *UEnum::GetValueAsString(PartnerCharacter->MoveMode),
			FVector::Distance(MovementCapsule->GetComponentLocation(), GetAutoFollowTarget()), FollowPath.Num(), FollowWaypoint,
			CharacterMovement->Velocity.Size(), CharacterMovement->MaxFlySpeed, static_cast<int32>(CharacterMovement->MovementMode),
			*GetNameSafe(PartnerCharacter->GetController()), PartnerCharacter->IsMoveInputIgnored(), bFollowHolding,
			FollowSearch.IsValid(), FollowSearch ? FollowSearch->GetResult().CollisionChecks : 0,
			FollowSearch ? FollowSearch->GetResult().SearchNodes : 0, ToWaypoint.Size(),
			FVector::DotProduct(CharacterMovement->Velocity.GetSafeNormal(), ToWaypoint.GetSafeNormal()),
			PartnerCharacter->HasAuthority() && FollowPath.IsValidIndex(FollowWaypoint) ? Now - FollowLastProgressAt : 0.0);
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
	FVector HorizontalOffset = -Forward * LocalOffset.X + Right * LocalOffset.Y;
	HorizontalOffset.Z = 0.0;
	// 기본 오프셋이 Shooter 중심에 겹쳐도 캡슐 옆에서 합류한다. 도착 허용 반경만큼의 여유도 확보한다.
	const float SafeDistance = ShooterCharacter->GetCapsuleComponent()->GetScaledCapsuleRadius()
		+ MovementCapsule->GetScaledCapsuleRadius() + FMath::Max(FollowPathClearance, 0.0f)
		+ GetFollowAcceptanceRadius();
	if (HorizontalOffset.SizeSquared() < FMath::Square(SafeDistance))
	{
		const FVector Direction = HorizontalOffset.IsNearlyZero() ? Right.GetSafeNormal2D() : HorizontalOffset.GetSafeNormal();
		HorizontalOffset = Direction * SafeDistance;
	}
	return
		ShooterLocation +
		HorizontalOffset +
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
	const float Horizon = GetFollowTravelHorizon();
	Request.TravelHorizon = FVector::DistSquared(Request.Start, Goal) > FMath::Square(Horizon) ? Horizon : 0.0f;
	Request.RouteHistory = FollowRouteHistory;
	if (Request.TravelHorizon > 0.0f)
	{
		const double LocalExtent = Request.TravelHorizon + Request.CellSize * 4.0;
		Request.SearchBounds = FBox(Request.Start - FVector(LocalExtent), Request.Start + FVector(LocalExtent));
	}
	Request.Clearance = FMath::Max(FollowPathClearance, 0.0f);
	Request.GoalSearchRadius = FMath::Max(FollowGoalSearchRadius, 0.0f);
	// 추종은 최단 경로 보장보다 제한 예산 안의 합류를 우선한다. 충돌 검사는 그대로 유지한다.
	// 예산 실패만 목표 방향 우선도를 단계적으로 높인다. 경로 없음/실제 충돌은 기존 정책을 유지한다.
	Request.HeuristicScale = FollowLastFailure == EFlightPathStatus::BudgetExceeded ? 2.0 : 1.25;
	// 지역 안의 빈 공간은 큰 간격, 막힌 입구는 작은 간격으로 보완한다.
	Request.LongConnectionStride = FVector::DistSquared(Request.Start, Goal) > FMath::Square(Request.CellSize * 8.0f)
		? 4 : 1;
	Request.bAdaptiveResolution = Request.LongConnectionStride > 1;
	Request.bHierarchicalResolution = Request.bAdaptiveResolution;
	Request.bLimitDistantGoalChecks = true;
	Request.bReuseReverseSegmentChecks = true;
	Request.bSimplifyPath = true;
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
	if (FollowPath.IsEmpty() && !FollowSearch
		&& FollowRetry.IsExhausted(Now, FollowMaxRetryAttempts, FollowRetryTimeout))
	{
		UE_LOG(LogPartnerFollow, Warning, TEXT("[Follow] Stop Partner=%s Attempts=%d Reason=RetryLimit ReleaseRequired=1"),
			*GetNameSafe(PartnerCharacter), FollowRetry.Failures);
		StopAutoFollow();
		return;
	}

	const FVector Current = MovementCapsule->GetComponentLocation();
	// 도착 판정을 정체보다 먼저 적용한다. 마지막 구간에서는 FollowStoredPath가 정상 정지를 처리한다.
	if (FollowPath.IsValidIndex(FollowWaypoint) && FollowWaypoint == FollowPath.Num() - 1
		&& FVector::DistSquared(Current, FollowPath.Last()) <= FMath::Square(GetFollowAcceptanceRadius()))
	{
		if (FollowSearch) AdvanceFollowPath(Now, Goal);
		return;
	}
	UpdateFollowProgress(Now, Current);

	// 이동 중 막힘/정체는 기존 경로를 버리고 재시도한다. 벽을 통과하는 직선 대체 이동은 하지 않는다.
	if (FollowPath.IsValidIndex(FollowWaypoint) && Now >= FollowNextPathCheckAt)
	{
		FollowNextPathCheckAt = Now + FMath::Max(FollowRepathInterval, 0.05f);
		const EFlightPathStatus Status = CheckFollowSegment(FollowPath[FollowWaypoint]);
		if (Status != EFlightPathStatus::Success)
		{
			FailFollowPath(Now, Status);
			return;
		}
		const FFlightSpeedProfile& Profile = GetFollowSpeedProfile();
		const float AccelerationGrace = Profile.AccelerationCurve && FMath::IsFinite(Profile.AccelerationDuration)
			? FMath::Max(Profile.AccelerationDuration, 0.0f) : 0.0f;
		// 초기 커브가 느린 구간은 이동 실패가 아니다. 경로 취소 이후에는 다시 한정된 유예만 준다.
		if (Now - FollowLastProgressAt >= FMath::Max(FollowNoProgressTimeout, 0.1f) + AccelerationGrace)
		{
			UE_LOG(LogPartnerFollow, Warning, TEXT("[Follow] Stalled Partner=%s Mode=%s Seconds=%.2f"),
				*GetNameSafe(PartnerCharacter), *UEnum::GetValueAsString(FollowMode), Now - FollowLastProgressAt);
			FailFollowPath(Now, EFlightPathStatus::BlockedSegment);
			return;
		}
	}

	const bool bTargetMoved = FVector::DistSquared(Goal, FollowTargetSnapshot)
		>= FMath::Square(FMath::Max(FollowTargetUpdateDistance, 10.0f));
	// 대기 중에는 같은 요청을 이어서 실행한다. 현재 경로의 안전 검사와 이동은 위/호출부에서 계속 처리한다.
	if (FollowSearch)
	{
		AdvanceFollowPath(Now, Goal);
		return;
	}
	if (bFollowHolding && !bTargetMoved && FVector::DistSquared(Current, FollowHoldLocation)
		<= FMath::Square(GetFollowAcceptanceRadius()))
	{
		return;
	}
	if (Now >= FollowNextRepathAt && Now >= FollowRetry.NextAttemptAt
		&& (FollowPath.IsEmpty() || bTargetMoved || ShouldPrefetchFollowPath(Current)))
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
	// 목표가 크게 바뀌면 이전 목적지의 방문 이력을 새 이동에 적용하지 않는다.
	if (FVector::DistSquared(Goal, FollowTargetSnapshot) > FMath::Square(FollowTravelHorizon))
	{
		FollowRouteHistory.Reset();
		FollowRouteSegments = 0;
		FollowBestGoalDistance = TNumericLimits<double>::Max();
	}
	if (FollowRouteSegments >= 64)
	{
		UE_LOG(LogPartnerFollow, Warning, TEXT("[Follow] Stop Partner=%s Reason=NoGoalProgress Segments=%d"),
			*GetNameSafe(PartnerCharacter), FollowRouteSegments);
		StopAutoFollow();
		return;
	}
	FFlightPathRequest Request = MakeFollowPathRequest(Goal);
	bFollowSearchContinuation = !FollowPath.IsEmpty() && bFollowPartialRoute
		&& FVector::DistSquared(Goal, FollowTargetSnapshot) < FMath::Square(FMath::Max(FollowTargetUpdateDistance, 10.0f));
	if (bFollowSearchContinuation)
	{
		// 현재 구간 끝에서 다음 구간을 준비한다. 이동 중인 경로는 결과가 올 때까지 유지한다.
		Request.Start = FollowPath.Last();
		const float Horizon = GetFollowTravelHorizon();
		Request.TravelHorizon = FVector::DistSquared(Request.Start, Goal) > FMath::Square(Horizon) ? Horizon : 0.0f;
		const double Extent = Horizon + Request.CellSize * Request.LongConnectionStride;
		Request.SearchBounds = Request.TravelHorizon > 0.0f
			? FBox(Request.Start - FVector(Extent), Request.Start + FVector(Extent))
			: FBox(Request.Start.ComponentMin(Goal), Request.Start.ComponentMax(Goal)).ExpandBy(FMath::Max(FollowPathMargin, 100.0f));
	}
	FollowSearch = FlightPathfinding::BeginSearch(*GetWorld(), *MovementCapsule, Request);
	FollowSearchStartedAt = Now;
	if (!FollowSearch)
	{
		FailFollowPath(Now, EFlightPathStatus::Unavailable, true);
		return;
	}
	UE_LOG(LogPartnerFollow, Log, TEXT("[Follow] SearchStart Partner=%s Distance=%.1f SliceChecks=%d SliceMs=%.2f TotalChecks=%d TotalNodes=%d TotalMs=%.2f KeepPath=%d Horizon=%.1f Continuation=%d Start=%s Bounds=%s"),
		*GetNameSafe(PartnerCharacter), FVector::Distance(Request.Start, Goal), FollowSearchSliceChecks,
		FollowSearchSliceMilliseconds, Request.MaxCollisionChecks, Request.MaxSearchNodes,
		Request.MaxSearchMilliseconds, !FollowPath.IsEmpty(), Request.TravelHorizon, bFollowSearchContinuation,
		*Request.Start.ToCompactString(), *Request.SearchBounds.GetSize().ToCompactString());
	AdvanceFollowPath(Now, Goal);
}

bool UPartnerMovementComponent::ShouldPrefetchFollowPath(const FVector& Current) const
{
	// 탐색 대기 한도와 재시도 간격을 이동 거리로 환산해 고속 이동에서도 준비 시간을 확보한다.
	const float SearchSeconds = FMath::Max(FollowSearchTimeout, 0.1f);
	const float Speed = FMath::Max(CharacterMovement->Velocity.Size(), GetFollowDesiredSpeed());
	const float PreparationDistance = Speed * (SearchSeconds + FMath::Max(FollowRepathInterval, 0.05f));
	const float PrefetchDistance = FMath::Max3(FollowPrefetchDistance, PreparationDistance, GetFollowTravelHorizon() * 0.75f);
	return bFollowPartialRoute && FollowPath.IsValidIndex(FollowWaypoint)
		&& FollowSpeedState.GetRemainingDistance(Current, FollowPath, FollowWaypoint)
			<= PrefetchDistance;
}

float UPartnerMovementComponent::GetFollowTravelHorizon() const
{
	// 예산 실패는 진행 중인 경로와 별개다. 다음 탐색만 짧게 나눠 먼저 통과 가능한 구간을 확보한다.
	const float Scale = FollowLastFailure == EFlightPathStatus::BudgetExceeded ? 0.5f : 1.0f;
	return FMath::Max(FollowTravelHorizon * Scale, FMath::Max(FollowPathCellSize, 25.0f) * 4.0f);
}

EFlightPathStatus UPartnerMovementComponent::CheckFollowSegment(const FVector& Goal) const
{
	FFlightPathRequest Segment = MakeFollowPathRequest(Goal);
	Segment.GoalSearchRadius = 0.0f;
	const EFlightPathStatus Status = FlightPathfinding::CheckSegment(*GetWorld(), *MovementCapsule, Segment);
	if (Status != EFlightPathStatus::BlockedSegment && Status != EFlightPathStatus::BlockedEndpoint) return Status;
	if (Segment.Clearance <= 0.0f) return Status;
	// 선정은 여유 공간을 유지한다. 이동 중 여유만 닿았다면 실제 캡슐로 재검사하고, 실제 차단은 그대로 중단한다.
	Segment.Clearance = 0.0f;
	const EFlightPathStatus ActualStatus = FlightPathfinding::CheckSegment(*GetWorld(), *MovementCapsule, Segment);
	UE_LOG(LogPartnerFollow, Verbose, TEXT("[Follow] ClearanceCheck Partner=%s Padded=%s Actual=%s From=%s To=%s"),
		*GetNameSafe(PartnerCharacter), *UEnum::GetValueAsString(Status), *UEnum::GetValueAsString(ActualStatus),
		*Segment.Start.ToCompactString(), *Goal.ToCompactString());
	return ActualStatus;
}

void UPartnerMovementComponent::RememberFollowRoute(const FVector& Start)
{
	if (!FollowRouteHistory.IsEmpty() && FollowRouteHistory.Last().Equals(Start, FollowPathCellSize)) return;
	if (FollowRouteHistory.Num() >= 16) FollowRouteHistory.RemoveAt(0, 1, EAllowShrinking::No);
	FollowRouteHistory.Add(Start);
	++FollowRouteSegments;
}

void UPartnerMovementComponent::AdvanceFollowPath(double Now, const FVector& Goal)
{
	if (!FollowSearch) return;
	// 작은 목표 이동으로 탐색을 매 프레임 버리지 않는다. 급이동만 취소하고 다음 요청으로 넘긴다.
	const float CancelDistance = FMath::Max(FollowTargetUpdateDistance * 4.0f, FollowPathCellSize * 2.0f);
	if (FVector::DistSquared(Goal, FollowSearch->GetRequest().Goal) >= FMath::Square(CancelDistance))
	{
		UE_LOG(LogPartnerFollow, Log, TEXT("[Follow] SearchCancel Partner=%s Reason=TargetMoved KeepPath=%d"),
			*GetNameSafe(PartnerCharacter), !FollowPath.IsEmpty());
		FollowSearch.Reset();
		FollowNextRepathAt = Now + FMath::Max(FollowRepathInterval, 0.05f);
		return;
	}
	if (Now - FollowSearchStartedAt >= FMath::Max(FollowSearchTimeout, 0.1f))
	{
		UE_LOG(LogPartnerFollow, Warning, TEXT("[Follow] SearchTimeout Partner=%s Wait=%.2f KeepPath=%d"),
			*GetNameSafe(PartnerCharacter), Now - FollowSearchStartedAt, !FollowPath.IsEmpty());
		FollowSearch.Reset();
		FailFollowPath(Now, EFlightPathStatus::BudgetExceeded, true);
		return;
	}
	if (FlightPathfinding::AdvanceSearch(*GetWorld(), *FollowSearch,
		FMath::Max(FollowSearchSliceChecks, 128), FMath::Max(FollowSearchSliceMilliseconds, 0.1f)))
	{
		CompleteFollowPath(Now);
	}
}

bool UPartnerMovementComponent::TryJoinFollowPath(TArray<FVector>& Points) const
{
	if (Points.IsEmpty()) return false;
	const FVector Current = MovementCapsule->GetComponentLocation();
	int32 Nearest = Points.Num() > 1 ? 1 : 0;
	for (int32 Index = Nearest + 1; Index < Points.Num(); ++Index)
	{
		if (FVector::DistSquared(Current, Points[Index]) < FVector::DistSquared(Current, Points[Nearest])) Nearest = Index;
	}
	// 탐색 동안 이동한 현재 위치에서 새 경로로 합류한다. 가까운 후보 최대 네 개만 실제 캡슐로 확인한다.
	for (int32 Index = Nearest; Index < FMath::Min(Nearest + 4, Points.Num()); ++Index)
	{
		FFlightPathRequest Segment = MakeFollowPathRequest(Points[Index]);
		Segment.GoalSearchRadius = 0.0f;
		if (FlightPathfinding::CheckSegment(*GetWorld(), *MovementCapsule, Segment) != EFlightPathStatus::Success) continue;
		Points.RemoveAt(0, Index, EAllowShrinking::No);
		if (!Current.Equals(Points[0], 0.01)) Points.Insert(Current, 0);
		return true;
	}
	return false;
}

void UPartnerMovementComponent::CompleteFollowPath(double Now)
{
	const FFlightPathRequest Request = FollowSearch->GetRequest();
	FFlightPathResult Result = FollowSearch->GetResult();
	FollowSearch.Reset();
	const FVector Goal = Request.Goal;
	UE_LOG(LogPartnerFollow, Log, TEXT("[Follow] SearchFinish Partner=%s Status=%s Slices=%d CpuMs=%.3f PeakSliceMs=%.3f Wait=%.3fs Checks=%d Nodes=%d KeepPath=%d"),
		*GetNameSafe(PartnerCharacter), *UEnum::GetValueAsString(Result.Status), Result.SearchSlices,
		Result.ElapsedMilliseconds, Result.PeakSliceMilliseconds, Now - FollowSearchStartedAt,
		Result.CollisionChecks, Result.SearchNodes, !FollowPath.IsEmpty());
	UE_LOG(LogPartnerFollow, Log, TEXT("[Follow] Path Partner=%s Mode=%s Status=%s Points=%d Checks=%d Nodes=%d Ms=%.3f GoalAdjusted=%d Start=%s Goal=%s Distance=%.1f SegmentCacheHits=%d Pruned=%d ClearanceEscape=%d MaxChecks=%d MaxNodes=%d MaxMs=%.1f Passes=%d Cell=%.1f RawPoints=%d ResolvedGoal=%s GoalChecks=%d GoalSkipped=%d Stride=%d Adaptive=%d Wide=%d Fine=%d"),
		*GetNameSafe(PartnerCharacter), *UEnum::GetValueAsString(FollowMode), *UEnum::GetValueAsString(Result.Status), Result.Waypoints.Num(),
		Result.CollisionChecks, Result.SearchNodes, Result.ElapsedMilliseconds, Result.bGoalAdjusted,
		*MovementCapsule->GetComponentLocation().ToCompactString(), *Goal.ToCompactString(),
		FVector::Distance(MovementCapsule->GetComponentLocation(), Goal), Result.SegmentCacheHits, Result.PrunedTraversals,
		Result.bUsedClearanceEscape, Request.MaxCollisionChecks, Request.MaxSearchNodes, Request.MaxSearchMilliseconds,
		Result.SearchPasses, Result.CellSizeUsed, Result.RawWaypointCount, *Result.ResolvedGoal.ToCompactString(),
		Result.GoalConnectionChecks, Result.SkippedGoalConnections, Request.LongConnectionStride,
		Request.bAdaptiveResolution, Result.WideExpansionNodes, Result.FineExpansionNodes);
	if (Request.bHierarchicalResolution)
	{
		UE_LOG(LogPartnerFollow, Log, TEXT("[Follow] Hierarchy Partner=%s Refined=%d CoarseChecks=%d CoarseNodes=%d TotalChecks=%d TotalNodes=%d Candidates=%d FineEnabled=%d ProgressNodes=%d FallbackFine=%d Wide3D=%d Wide2D=%d RejectedPositions=%d RejectedSegments=%d BoundsRejected=%d CpuLimit=%d CheckLimit=%d NodeLimit=%d"),
			*GetNameSafe(PartnerCharacter), Result.SearchPasses > 1,
			Result.CoarseCollisionChecks, Result.CoarseSearchNodes, Result.CollisionChecks, Result.SearchNodes,
			Result.RefinementCandidates, Result.RefinedSearchNodes, Result.CoarseProgressNodes, Result.FallbackRefinedNodes,
			Result.WideRegion3DNodes, Result.WideRegion2DNodes,
			Result.RejectedPositions, Result.RejectedSegments,
			Result.OutOfBoundsNeighbours, Result.ElapsedMilliseconds >= Request.MaxSearchMilliseconds,
			Result.CollisionChecks >= Request.MaxCollisionChecks, Result.SearchNodes >= Request.MaxSearchNodes);
	}
	if (FollowLastFailure == EFlightPathStatus::BudgetExceeded)
	{
		UE_LOG(LogPartnerFollow, Log, TEXT("[Follow] BudgetRetry Partner=%s Attempt=%d Heuristic=%.2f Checks=%d/%d Ms=%.3f Status=%s"),
			*GetNameSafe(PartnerCharacter), FollowRetry.Failures, Request.HeuristicScale,
			Result.CollisionChecks, Request.MaxCollisionChecks, Result.ElapsedMilliseconds, *UEnum::GetValueAsString(Result.Status));
	}
	// 서버가 경로를 선정한 순간에만 그린다. 전용 서버에는 화면이 없고 진단용 추가 충돌 검사도 하지 않는다.
	if (CVarPartnerFollowPathDebug.GetValueOnGameThread() > 0 && GetWorld()->GetNetMode() != NM_DedicatedServer)
	{
		DrawDebugCapsule(GetWorld(), Request.Start, MovementCapsule->GetScaledCapsuleHalfHeight(),
			MovementCapsule->GetScaledCapsuleRadius(), MovementCapsule->GetComponentQuat(), FColor::Cyan, false, 3.0f);
		DrawDebugSphere(GetWorld(), Goal, 25.0f, 12, FColor::Yellow, false, 3.0f);
		DrawDebugSphere(GetWorld(), Result.ResolvedGoal, 35.0f, 12, FColor::Magenta, false, 3.0f);
		if (Result.Status != EFlightPathStatus::Success)
		{
			DrawDebugLine(GetWorld(), Request.Start, Goal, FColor::Red, false, 3.0f, 0, 2.0f);
		}
		for (int32 Index = 1; Index < Result.Waypoints.Num(); ++Index)
		{
			DrawDebugLine(GetWorld(), Result.Waypoints[Index - 1], Result.Waypoints[Index], FColor::Green, false, 3.0f, 0, 2.0f);
		}
	}
	if (Result.Status != EFlightPathStatus::Success || Result.Waypoints.IsEmpty())
	{
		FailFollowPath(Now, Result.Status, true);
		return;
	}
	const bool bAppend = bFollowSearchContinuation && !FollowPath.IsEmpty()
		&& FollowPath.Last().Equals(Request.Start, 0.01);
	if (!bAppend && !TryJoinFollowPath(Result.Waypoints))
	{
		FailFollowPath(Now, EFlightPathStatus::BlockedSegment, true);
		return;
	}
	if (bAppend)
	{
		// 두 경로는 같은 검증된 캡슐 중심에서 연결된다. 경유점/현재 속도를 되감지 않는다.
		FollowPath.RemoveAt(0, FMath::Max(FollowWaypoint - 1, 0), EAllowShrinking::No);
		FollowWaypoint = FollowPath.Num() > 1 ? 1 : 0;
		Result.Waypoints.RemoveAt(0, 1, EAllowShrinking::No);
		FollowPath.Append(Result.Waypoints);
	}
	else
	{
		FollowPath = Result.Waypoints;
		FollowWaypoint = FollowPath.Num() > 1 ? 1 : 0;
	}
	bFollowPartialRoute = Result.bPartialRoute;
	FollowLastFailure = EFlightPathStatus::Success;
	// 장거리 자체를 제한하지 않는다. 목적지 접근 없이 경유지만 계속 순환할 때만 종료한다.
	const double GoalDistance = FVector::Distance(Request.Start, Goal);
	if (GoalDistance + FMath::Max(FollowTravelHorizon * 0.5f, 100.0f) < FollowBestGoalDistance)
	{
		FollowBestGoalDistance = GoalDistance;
		FollowRouteSegments = 0;
	}
	RememberFollowRoute(Request.Start);
	UE_LOG(LogPartnerFollow, Log, TEXT("[Follow] RouteChunk Partner=%s Partial=%d Append=%d Segments=%d Horizon=%.1f Endpoint=%s FinalGoal=%s"),
		*GetNameSafe(PartnerCharacter), bFollowPartialRoute, bAppend, FollowRouteSegments,
		Request.TravelHorizon, *FollowPath.Last().ToCompactString(), *Goal.ToCompactString());
	FollowSpeedState.SetPath(FollowPath);
	FollowTargetSnapshot = Goal;
	// 같은 인덱스여도 새 경로의 경유점은 다르다. 거리 기준만 바꾸고 실패 이력은 실제 접근 후 지운다.
	FollowProgressLocation = MovementCapsule->GetComponentLocation();
	FollowProgressWaypoint = FollowWaypoint;
	FollowLastProgressAt = Now;
	bFollowHolding = false;
	PublishFollowPath(false);
}

void UPartnerMovementComponent::FailFollowPath(double Now, EFlightPathStatus Status, bool bKeepCurrentPath)
{
	FollowRetry.Fail(Now, FollowRepathInterval);
	FollowLastFailure = Status;
	FollowSearch.Reset();
	if (!bKeepCurrentPath || FollowPath.IsEmpty())
	{
		ClearFollowPath();
		PublishFollowPath(false);
	}
	UE_LOG(LogPartnerFollow, Warning, TEXT("[Follow] Retry Partner=%s Status=%s Attempt=%d/%d"),
		*GetNameSafe(PartnerCharacter), *UEnum::GetValueAsString(Status), FollowRetry.Failures, FollowMaxRetryAttempts);
}

void UPartnerMovementComponent::UpdateFollowProgress(double Now, const FVector& Current)
{
	if (!FollowPath.IsValidIndex(FollowWaypoint)) return;
	if (FollowProgressWaypoint != FollowWaypoint)
	{
		FollowProgressWaypoint = FollowWaypoint;
		FollowProgressLocation = Current;
		FollowLastProgressAt = Now;
		return;
	}
	const FVector& Waypoint = FollowPath[FollowWaypoint];
	const float Distance = FVector::Distance(Current, Waypoint);
	const bool bFinalApproach = FollowWaypoint == FollowPath.Num() - 1
		&& Distance <= GetFollowAcceptanceRadius() + 200.0f;
	const float RequiredProgress = bFinalApproach ? 1.0f : 25.0f;
	// 한 경유점에 가까워진 거리만 누적한다. 원을 돌거나 멀어졌다 돌아오는 움직임은 진행이 아니다.
	if (Distance + RequiredProgress > FVector::Distance(FollowProgressLocation, Waypoint)) return;
	FollowProgressLocation = Current;
	FollowLastProgressAt = Now;
	FollowRetry.Reset();
	// 이동 진행으로 탐색 예산 실패를 지우면 다음 구간이 같은 조건으로 반복 실패한다.
	if (FollowLastFailure != EFlightPathStatus::BudgetExceeded) FollowLastFailure = EFlightPathStatus::Success;
}

bool UPartnerMovementComponent::HasReachedFollowWaypoint(const FVector& Current) const
{
	const bool bFinalWaypoint = FollowWaypoint == FollowPath.Num() - 1;
	const float Acceptance = bFinalWaypoint ? GetFollowAcceptanceRadius() : 20.0f;
	const FVector Offset = Current - FollowPath[FollowWaypoint];
	if (Offset.SizeSquared() <= FMath::Square(Acceptance)) return true;
	if (bFinalWaypoint || FollowWaypoint <= 0 || Offset.SizeSquared() > FMath::Square(150.0f)) return false;
	// 중간 지점을 조금 비껴 지나쳐도 다시 돌아가지 않는다. 다음 구간의 안전 검사는 호출부에서 수행한다.
	const FVector Segment = FollowPath[FollowWaypoint] - FollowPath[FollowWaypoint - 1];
	return !Segment.IsNearlyZero() && FVector::DotProduct(Offset, Segment) >= 0.0f;
}

void UPartnerMovementComponent::FollowStoredPath(float DeltaTime)
{
	if (!FollowPath.IsValidIndex(FollowWaypoint) || DeltaTime <= KINDA_SMALL_NUMBER)
	{
		StopFollowMovement();
		return;
	}
	const FVector Current = MovementCapsule->GetComponentLocation();
	// 중간 지점은 같은 프레임에서 다음 구간으로 이어간다. 가속 커브/속도 초기화는 실제 정지에만 적용한다.
	while (FollowPath.IsValidIndex(FollowWaypoint))
	{
		const bool bFinalWaypoint = FollowWaypoint == FollowPath.Num() - 1;
		if (!HasReachedFollowWaypoint(Current)) break;
		if (!bFinalWaypoint && PartnerCharacter->HasAuthority())
		{
			// 다음 구간의 유효성은 서버가 판정한다. 클라이언트 월드의 다른 충돌 상태로 경유점을 붙잡지 않는다.
			if (CheckFollowSegment(FollowPath[FollowWaypoint + 1]) != EFlightPathStatus::Success)
			{
				// 20cm 조기 도착 판정으로 모서리를 잘라 넘지 않는다. 현재 구간 끝에 더 접근한 후 다시 검사한다.
				if (FVector::DistSquared(Current, FollowPath[FollowWaypoint]) > FMath::Square(2.0f)
					&& CheckFollowSegment(FollowPath[FollowWaypoint]) == EFlightPathStatus::Success) break;
				UE_LOG(LogPartnerFollow, Log, TEXT("[Follow] CornerBlocked Partner=%s Waypoint=%d Offset=%.1f Current=%s Corner=%s Next=%s"),
					*GetNameSafe(PartnerCharacter), FollowWaypoint, FVector::Distance(Current, FollowPath[FollowWaypoint]),
					*Current.ToCompactString(), *FollowPath[FollowWaypoint].ToCompactString(), *FollowPath[FollowWaypoint + 1].ToCompactString());
				FailFollowPath(GetWorld()->GetTimeSeconds(), EFlightPathStatus::BlockedSegment);
				return;
			}
		}
		if (!bFinalWaypoint)
		{
			UE_LOG(LogPartnerFollow, Verbose, TEXT("[Follow] Waypoint Partner=%s Authority=%d From=%d To=%d Speed=%.1f Continue=1"),
				*GetNameSafe(PartnerCharacter), PartnerCharacter->HasAuthority(), FollowWaypoint, FollowWaypoint + 1,
				CharacterMovement->Velocity.Size());
		}
		++FollowWaypoint;
		if (PartnerCharacter->HasAuthority()) UpdateFollowProgress(GetWorld()->GetTimeSeconds(), Current);
		if (bFinalWaypoint)
		{
			FollowHoldLocation = FollowPath.Last();
			UE_LOG(LogPartnerFollow, Log, TEXT("[Follow] %s Partner=%s"),
				bFollowPartialRoute ? TEXT("ChunkReached") : TEXT("Arrived"), *GetNameSafe(PartnerCharacter));
			// 이전 경로 도착은 이동만 끝낸다. 이미 진행 중인 새 목표 탐색은 그대로 이어간다.
			FollowPath.Reset();
			FollowWaypoint = 0;
			StopFollowMovement();
			bFollowHolding = !bFollowPartialRoute;
			FollowRetry.Reset();
			if (PartnerCharacter->HasAuthority())
			{
				PublishFollowPath(false);
			}
			return;
		}
	}
	const FVector ToWaypoint = FollowPath[FollowWaypoint] - Current;
	const bool bFinalWaypoint = FollowWaypoint == FollowPath.Num() - 1;
	const float Acceptance = bFinalWaypoint ? GetFollowAcceptanceRadius() : 20.0f;
	const bool bCameraAssist = FollowMode == EPartnerMoveMode::CameraAssist;
	const float MaxDistance = FMath::Max(bCameraAssist ? PartnerCharacter->AssistMaxDistance
		: PartnerCharacter->SyncMoveDistance, Acceptance + 1.0f);
	// 최종 합류만 입력 거리 감속을 쓴다. 모서리 감속은 아래의 실제 가속도 기반 속도 제한 한 곳에서 처리한다.
	float DistanceScale = bFinalWaypoint && !bFollowPartialRoute ? FMath::Clamp(ToWaypoint.Size() / MaxDistance, 0.15f, 1.0f) : 1.0f;
	const float Strength = bCameraAssist ? FMath::Clamp(PartnerCharacter->AssistStrength / 100.0f, 0.0f, 1.0f) : 1.0f;
	const float InterpSpeed = bCameraAssist ? PartnerCharacter->AssistInterpSpeed : PartnerCharacter->SyncMoveInterpSpeed;
	const float InterpScale = FMath::Clamp(InterpSpeed / 8.0f, 0.1f, 1.0f);
	const FFlightSpeedProfile& Profile = GetFollowSpeedProfile();
	// 최종 감속 커브가 속도를 관리하면 입력 거리 감속을 겹치지 않는다. 가속 전용 커브는 기존 감속을 유지한다.
	if (bFinalWaypoint && Profile.DecelerationCurve && FMath::IsFinite(Profile.DecelerationDistance)
		&& Profile.DecelerationDistance > 0.0f)
	{
		DistanceScale = 1.0f;
	}
	// 감쇠 중인 가중치로 매 프레임 커브 목표 속도를 바꾸면 가속 커브가 계속 재시작된다.
	// 기존 속도로 커브를 계산한 후 순간 회피 가중치를 곱하고, 코너/도착 제한은 마지막에 적용한다.
	const float BaseSpeed = GetFollowDesiredSpeed(false);
	const float BurstWeight = BaseSpeed > 0.0f ? GetFollowDesiredSpeed() / BaseSpeed : 1.0f;
	bOwnsFollowSpeed = true;
	if (Profile.IsEnabled()) 
	{
		const float Remaining = FMath::Max(FollowSpeedState.GetRemainingDistance(Current, FollowPath, FollowWaypoint)
			- GetFollowAcceptanceRadius(), bFollowPartialRoute ? Profile.DecelerationDistance + 100.0f : 0.0f);
		CharacterMovement->MaxFlySpeed = FollowSpeedState.Update(Profile, BaseSpeed,
			CharacterMovement->Velocity.Size() / BurstWeight, Remaining, DeltaTime, true, PartnerCharacter,
			bCameraAssist ? TEXT("CameraAssist") : TEXT("SyncMove"));
	}
	else
	{
		CharacterMovement->MaxFlySpeed = BaseSpeed;
	}
	CharacterMovement->MaxFlySpeed *= BurstWeight;
	// 급회전은 남은 거리와 실제 가속도로 진입 속도를 제한한다. 입력 크기만 줄이면 관성이 남는다.
	const float SteeringMaxAcceleration = FMath::Max(CharacterMovement->MaxAcceleration, 1.0f);
	if (!bFinalWaypoint)
	{
		const FVector NextDirection = (FollowPath[FollowWaypoint + 1] - FollowPath[FollowWaypoint]).GetSafeNormal();
		const float Turn = 1.0f - FMath::Clamp(FVector::DotProduct(ToWaypoint.GetSafeNormal(), NextDirection), 0.0f, 1.0f);
		if (Turn > 0.2f)
		{
			const float CornerSpeed = FMath::Sqrt(2.0f * SteeringMaxAcceleration * FMath::Max(ToWaypoint.Size(), 20.0f) / Turn);
			CharacterMovement->MaxFlySpeed = FMath::Min(CharacterMovement->MaxFlySpeed, CornerSpeed);
		}
	}
	// 낮은 FPS의 한 프레임 과주행을 막고, 목표 방향에 수직인 관성을 보정한다.
	CharacterMovement->MaxFlySpeed = FMath::Min(CharacterMovement->MaxFlySpeed, ToWaypoint.Size() / DeltaTime);
	CharacterMovement->Velocity = CharacterMovement->Velocity.GetClampedToMaxSize(CharacterMovement->MaxFlySpeed);
	const FVector TravelDirection = ToWaypoint.GetSafeNormal();
	const FVector LateralVelocity = CharacterMovement->Velocity
		- TravelDirection * FVector::DotProduct(CharacterMovement->Velocity, TravelDirection);
	const float ResponseTime = FMath::Max(1.0f / FMath::Max(InterpSpeed, 1.0f), DeltaTime);
	// 일정 속도에서도 전진 입력을 유지해 무입력 제동이 반복되지 않도록 한다.
	const FVector SteeringAcceleration = TravelDirection * SteeringMaxAcceleration - LateralVelocity / ResponseTime;
	PartnerCharacter->AddMovementInput(SteeringAcceleration.GetSafeNormal(), DistanceScale * Strength * InterpScale);
}

const FFlightSpeedProfile& UPartnerMovementComponent::GetFollowSpeedProfile() const
{
	return FollowMode == EPartnerMoveMode::CameraAssist ? CameraAssistSpeedProfile : SyncMoveSpeedProfile;
}

float UPartnerMovementComponent::GetFollowDesiredSpeed(bool bIncludeBurst) const
{
	if (FollowMode == EPartnerMoveMode::CameraAssist && FMath::IsFinite(CameraAssistMaxSpeed) && CameraAssistMaxSpeed > 0.0f)
	{
		return CameraAssistMaxSpeed;
	}
	return PartnerCharacter->bIsAccelerate
		? (bIncludeBurst ? GetEffectiveBoostSpeed() : PartnerCharacter->BoostSpeed) : PartnerCharacter->MoveSpeed;
}

void UPartnerMovementComponent::StopFollowMovement()
{
	FollowSpeedState.Reset();
	if (PartnerCharacter)
	{
		PartnerCharacter->ConsumeMovementInputVector();
	}
	if (CharacterMovement)
	{
		CharacterMovement->StopMovementImmediately();
		// 자동 이동이 소유한 제한만 복구한다. 수동/빙의 이동 설정은 변경하지 않는다.
		if (bOwnsFollowSpeed && PartnerCharacter)
		{
			CharacterMovement->MaxFlySpeed = PartnerCharacter->bIsAccelerate ? GetEffectiveBoostSpeed() : PartnerCharacter->MoveSpeed;
		}
	}
	bOwnsFollowSpeed = false;
}

void UPartnerMovementComponent::ClearFollowPath()
{
	FollowSearch.Reset();
	FollowPath.Reset();
	FollowWaypoint = 0;
	FollowProgressWaypoint = INDEX_NONE;
	bFollowHolding = false;
	bFollowPartialRoute = false;
	bFollowSearchContinuation = false;
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
		if (CVarPartnerFollowDebug.GetValueOnGameThread() > 0)
		{
			UE_LOG(LogPartnerFollow, Log, TEXT("[Follow] ClientPathRejected Partner=%s Mode=%s Serial=%u Points=%d Stop=%d"),
				*GetNameSafe(PartnerCharacter), *UEnum::GetValueAsString(Mode), InputSerial, Points.Num(), bStopped);
		}
		return;
	}
	if (CVarPartnerFollowDebug.GetValueOnGameThread() > 0)
	{
		UE_LOG(LogPartnerFollow, Log, TEXT("[Follow] ClientPathReceived Partner=%s Mode=%s Serial=%u Points=%d Stop=%d"),
			*GetNameSafe(PartnerCharacter), *UEnum::GetValueAsString(Mode), InputSerial, Points.Num(), bStopped);
	}
	if (bStopped)
	{
		StopAutoFollow();
		return;
	}
	if (!PartnerCharacter->CanAcceptInput())
	{
		ClearFollowPath();
		return;
	}
	// 같은 입력의 정상 재선정은 예측 클라이언트에서도 속도를 초기화하지 않는다.
	if (Points.IsEmpty()) ClearFollowPath();
	FollowPath = Points;
	// 경유지 여부는 표현에만 쓰며 경로/실패 판정은 서버가 소유한다.
	bFollowPartialRoute = ShooterCharacter && !Points.IsEmpty()
		&& FVector::DistSquared(Points.Last(), GetAutoFollowTarget()) > FMath::Square(GetFollowAcceptanceRadius() + FollowGoalSearchRadius);
	FollowSpeedState.SetPath(FollowPath);
	FollowWaypoint = FollowPath.Num() > 1 ? 1 : 0;
	FollowProgressWaypoint = FollowWaypoint;
	FollowProgressLocation = MovementCapsule->GetComponentLocation();
	FollowLastProgressAt = GetWorld()->GetTimeSeconds();
}
