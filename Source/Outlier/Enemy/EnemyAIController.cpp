#include "Enemy/EnemyAIController.h"

#include "Enemy/AutoTurret.h"
#include "AbilitySystemComponent.h"

#include "Drone/Partner/PartnerCharacter.h"
#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "Enemy/EnemyTargetRules.h"
#include "Engine/World.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "HAL/IConsoleManager.h"
#include "Interface/GameplayTagProviderInterface.h"
#include "TimerManager.h"
#include "Outlier.h"
#include "OutlierPlayerState.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Shooter/ShooterCharacter.h"
#include "Team/OutlierTeamIds.h"

namespace
{
	TAutoConsoleVariable<int32> CVarEnemyPerceptionDiagnostics(
		TEXT("outlier.Enemy.PerceptionDiagnostics"),
		0,
		TEXT("Logs Enemy AI perception events and sight-loss geometry when set to 1."),
		ECVF_Cheat);

	bool IsEnemyPerceptionDiagnosticsEnabled()
	{
		return CVarEnemyPerceptionDiagnostics.GetValueOnGameThread() != 0;
	}

	const TCHAR* GetStimulusSenseName(const FAIStimulus& Stimulus)
	{
		if (Stimulus.Type == UAISense::GetSenseID<UAISense_Sight>())
		{
			return TEXT("Sight");
		}

		if (Stimulus.Type == UAISense::GetSenseID<UAISense_Hearing>())
		{
			return TEXT("Hearing");
		}

		return TEXT("Other");
	}

	void LogPerceptionDiagnostic(
		const AEnemyBase& Enemy,
		const AActor& Target,
		const FAIStimulus& Stimulus)
	{
		if (!IsEnemyPerceptionDiagnosticsEnabled())
		{
			return;
		}

		if (Stimulus.Type == UAISense::GetSenseID<UAISense_Hearing>())
		{
			// 청각 진단은 사건 스냅샷만 기록한다. 숨은 출처의 현재 위치 조회나 시야 Trace를 추가하지 않는다.
			const double Now = Enemy.GetWorld()->GetTimeSeconds();
			UE_LOG(LogOutlier, Log,
				TEXT("[EnemyHearingDiag] Stage=Receive Enemy=%s Source=%s OccurredAt=%.3f ReceivedAt=%.3f Sensed=%d Location=%s Tag=%s"),
				*Enemy.GetName(), *Target.GetName(), Now - Stimulus.GetAge(), Now,
				Stimulus.WasSuccessfullySensed(), *Stimulus.StimulusLocation.ToCompactString(), *Stimulus.Tag.ToString());
			return;
		}

		const FVector SightOrigin = Stimulus.ReceiverLocation.IsNearlyZero()
			? Enemy.GetPawnViewLocation()
			: Stimulus.ReceiverLocation;
		const FVector ToTarget = Target.GetActorLocation() - SightOrigin;
		const float Distance = ToTarget.Size();
		const FVector TargetDirection = ToTarget.GetSafeNormal();
		const FVector ViewDirection = Enemy.GetViewRotation().Vector();
		const float ViewAngle = TargetDirection.IsNearlyZero()
			? 0.0f
			: FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
				FVector::DotProduct(ViewDirection, TargetDirection),
				-1.0f,
				1.0f)));

		FHitResult VisibilityHit;
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(EnemyPerceptionDiagnostics), false, &Enemy);
		const bool bVisibilityBlocked = Enemy.GetWorld()
			&& Enemy.GetWorld()->LineTraceSingleByChannel(
				VisibilityHit,
				SightOrigin,
				Target.GetActorLocation(),
				ECC_Visibility,
				QueryParams)
			&& VisibilityHit.GetActor() != &Target;

		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[EnemyPerceptionDiag] Enemy=%s Class=%s Type=%s Sense=%s Sensed=%s Target=%s State=%s Visible=%s Distance=%.1f ViewAngle=%.1f StimulusAge=%.2f Strength=%.2f StimulusLocation=%s ReceiverLocation=%s VisibilityBlocked=%s HitActor=%s HitComponent=%s"),
			*Enemy.GetName(),
			*Enemy.GetClass()->GetName(),
			*UEnum::GetValueAsString(Enemy.GetRuntimeStat().Type),
			GetStimulusSenseName(Stimulus),
			Stimulus.WasSuccessfullySensed() ? TEXT("true") : TEXT("false"),
			*Target.GetName(),
			*UEnum::GetValueAsString(Enemy.GetCombatState()),
			Enemy.IsPlayerCurrentlyVisible() ? TEXT("true") : TEXT("false"),
			Distance,
			ViewAngle,
			Stimulus.GetAge(),
			Stimulus.Strength,
			*Stimulus.StimulusLocation.ToCompactString(),
			*Stimulus.ReceiverLocation.ToCompactString(),
			bVisibilityBlocked ? TEXT("true") : TEXT("false"),
			*GetNameSafe(VisibilityHit.GetActor()),
			*GetNameSafe(VisibilityHit.GetComponent()));
	}

	using EEnemyTargetPriority = OutlierEnemyTargetRules::ETargetPriority;
}

AEnemyAIController::AEnemyAIController()
{
	SetGenericTeamId(FGenericTeamId(OutlierTeamIds::Enemy));

	EnemyPerceptionComponent = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("EnemyPerceptionComponent"));
	SetPerceptionComponent(*EnemyPerceptionComponent);

	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = false;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = false;

	HearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("HearingConfig"));
	HearingConfig->DetectionByAffiliation.bDetectEnemies = true;
	HearingConfig->DetectionByAffiliation.bDetectNeutrals = false;
	HearingConfig->DetectionByAffiliation.bDetectFriendlies = false;

	EnemyPerceptionComponent->ConfigureSense(*SightConfig);
	EnemyPerceptionComponent->ConfigureSense(*HearingConfig);
	EnemyPerceptionComponent->SetDominantSense(SightConfig->GetSenseImplementation());

	EnemyPerceptionComponent->OnTargetPerceptionUpdated.AddDynamic(
		this,
		&AEnemyAIController::HandleTargetPerceptionUpdated
	);
}

void AEnemyAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	ResetInitialAwareness();

	ProcessedStealthedTargets.Reset();
	LastVisibleTarget.Reset();
	DirectlyVisibleTargets.Reset();

	if (AEnemyBase* Enemy = Cast<AEnemyBase>(InPawn))
	{
		RefreshTeamAndPerceptionFromPawn();
	}
}

void AEnemyAIController::OnUnPossess()
{
	ResetInitialAwareness();
	if (AEnemyBase* Enemy = Cast<AEnemyBase>(GetPawn()))
	{
		Enemy->ClearAlertClue();
		Enemy->ClearHitClues();
		Enemy->ResetTargetAttackContinuity();
		Enemy->ClearTargetTracking();
		Enemy->ClearHeardNoise();
	}
	StopSharedTargetReporting(true);
	ProcessedStealthedTargets.Reset();
	LastVisibleTarget.Reset();
	DirectlyVisibleTargets.Reset();
	TaskDrivenControlPitchCount = 0;
	bAttackAimActive = false;
	bTargetAttackAimOwned = false;
	Super::OnUnPossess();
}

void AEnemyAIController::UpdateControlRotation(float DeltaTime, bool bUpdatePawn)
{
	// 기존 컨트롤러 갱신에서 참조 한 개만 확인한다. 공격 조준이 회전을 소유해도 추적은 갱신한다.
	if (AEnemyBase* Enemy = Cast<AEnemyBase>(GetPawn()))
	{
		Enemy->RefreshTargetTracking();
		Enemy->TickHitResponse(DeltaTime);
		Enemy->RefreshCombatAimPoint();
		if (Enemy->IsHitDirectionConfirmationActive())
		{
			Enemy->SetCombatAimPresentationOwned(false);
			return;
		}
		// 시야/피격/새 목적지 판단 뒤에만 청각 회전을 허용한다. 기본 Focus는 이 프레임에 양보한다.
		if (Enemy->UpdateNoiseDirectionFacing(DeltaTime))
		{
			Enemy->SetCombatAimPresentationOwned(false);
			return;
		}
		// 최초 전조·회복·연사 보류에서도 같은 회전 경로를 사용한다. 무기 타이머는 이 방향을 검사만 한다.
		if (Enemy->UpdateTargetAttackAim(DeltaTime))
		{
			bTargetAttackAimOwned = true;
			return;
		}
		Enemy->SetCombatAimPresentationOwned(false);
		// 대상 조준이 빌린 소유권만 반환한다. 고정 좌표/터렛 Task가 설정한 조준은 유지한다.
		if (bTargetAttackAimOwned) SetAttackAimActive(false);
		if (Enemy->GetCombatState() == EEnemyCombatState::Stun)
		{
			// 기절 중 지난 시간을 노출 시간으로 소급하지 않는다. 복귀 후 새 관측부터 확인한다.
			AwarenessTargets.Reset();
		}
		else if (Enemy->GetCombatState() != EEnemyCombatState::Alert)
		{
			ResetInitialAwareness();
		}
	}
	// 공격 경로가 시점 기준으로 Pitch/Yaw와 본체 Yaw를 함께 설정한다.
	// 기본 Focus/이동 회전으로 덮으면 다음 연사 타이머가 잘못된 방향을 읽는다.
	if (bAttackAimActive)
	{
		return;
	}

	const float TaskPitch = GetControlRotation().Pitch;
	Super::UpdateControlRotation(DeltaTime, bUpdatePawn);

	// 터렛은 총구의 상하 조준값이 실제 Hitscan 방향이므로 AIController의 기본 Pitch 초기화를 막는다.
	const bool bPreserveTurretPitch = Cast<AAutoTurret>(GetPawn()) != nullptr;
	if (TaskDrivenControlPitchCount > 0 || bPreserveTurretPitch)
	{
		FRotator EnemyControlRotation = GetControlRotation();
		EnemyControlRotation.Pitch = TaskPitch;
		SetControlRotation(EnemyControlRotation);
	}
}

void AEnemyAIController::BeginTaskDrivenControlPitch()
{
	++TaskDrivenControlPitchCount;
}

void AEnemyAIController::EndTaskDrivenControlPitch()
{
	TaskDrivenControlPitchCount = FMath::Max(TaskDrivenControlPitchCount - 1, 0);
}

void AEnemyAIController::SetAttackAimActive(bool bActive)
{
	bAttackAimActive = bActive;
	if (!bActive) bTargetAttackAimOwned = false;
}

void AEnemyAIController::ForgetDetectionTarget(AActor* TargetActor)
{
	AwarenessTargets.Remove(TargetActor);
	if (!IsValid(TargetActor) || !EnemyPerceptionComponent)
	{
		return;
	}

	EnemyPerceptionComponent->ForgetActor(TargetActor);
	ProcessedStealthedTargets.Remove(TWeakObjectPtr<AActor>(TargetActor));
	// Gameplay Tag 변화만으로는 Sight 쿼리가 즉시 다시 평가되지 않을 수 있다.
	// 이전 결과를 지운 뒤 리스너를 갱신해 스텔스 해제 대상을 새 감지로 처리한다.
	EnemyPerceptionComponent->RequestStimuliListenerUpdate();

	AEnemyBase* Enemy = Cast<AEnemyBase>(GetPawn());
	if (!IsValid(Enemy) || Enemy->IsAIControlSuppressed())
	{
		return;
	}

	AActor* PreferredTarget = GetPreferredVisibleTarget();
	const bool bHasVisibleTarget = IsValid(PreferredTarget);
	UpdateVisibleTarget(Enemy, PreferredTarget);

	if (bHasVisibleTarget)
	{
		StartSharedTargetReporting();
		ReportSharedTargetContact(Enemy, PreferredTarget);
	}
	else
	{
		StopSharedTargetReporting(true);
	}
}

void AEnemyAIController::RefreshPerceptionConfigFromPawn()
{
	if (AEnemyBase* Enemy = Cast<AEnemyBase>(GetPawn()))
	{
		if (Enemy->GetCombatState() == EEnemyCombatState::Stun) AwarenessTargets.Reset();
		ConfigureSightFromEnemy(Enemy);
		ConfigureHearingFromEnemy(Enemy);
	}
}

void AEnemyAIController::RefreshTeamAndPerceptionFromPawn()
{
	ResetInitialAwareness();
	AEnemyBase* Enemy = Cast<AEnemyBase>(GetPawn());
	if (!Enemy)
	{
		return;
	}

	SetGenericTeamId(Enemy->GetGenericTeamId());
	Enemy->ClearAlertClue();
	Enemy->ClearHitClues();
	Enemy->ResetTargetAttackContinuity();
	Enemy->ClearTargetTracking();
	Enemy->ClearHeardNoise();
	if (EnemyPerceptionComponent)
	{
		EnemyPerceptionComponent->ForgetAll();
		ProcessedStealthedTargets.Reset();
		LastVisibleTarget.Reset();
		DirectlyVisibleTargets.Reset();
	}
	RefreshPerceptionConfigFromPawn();
	SetEnemyPerceptionEnabled(Enemy->CanUseEnemyPerception());
}

void AEnemyAIController::SetEnemyPerceptionEnabled(bool bEnabled)
{
	if (!bEnabled) ResetInitialAwareness();
	if (!EnemyPerceptionComponent)
	{
		return;
	}

	EnemyPerceptionComponent->SetComponentTickEnabled(bEnabled);
	EnemyPerceptionComponent->SetSenseEnabled(UAISense_Sight::StaticClass(), bEnabled);
	const AEnemyBase* Enemy = Cast<AEnemyBase>(GetPawn());
	EnemyPerceptionComponent->SetSenseEnabled(
		UAISense_Hearing::StaticClass(),
		bEnabled && (!Enemy || Enemy->UsesHearingPerception()));
	if (!bEnabled)
	{
		if (AEnemyBase* ControlledEnemy = Cast<AEnemyBase>(GetPawn()))
		{
			ControlledEnemy->ResetTargetAttackContinuity();
			ControlledEnemy->ClearTargetTracking();
			ControlledEnemy->ClearHeardNoise();
		}
		EnemyPerceptionComponent->ForgetAll();
		ProcessedStealthedTargets.Reset();
		LastVisibleTarget.Reset();
		DirectlyVisibleTargets.Reset();
	}

	if (bEnabled)
	{
		EnemyPerceptionComponent->Activate();
	}
	else
	{
		StopSharedTargetReporting(true);
		EnemyPerceptionComponent->Deactivate();
	}
}

AActor* AEnemyAIController::GetPreferredVisibleTarget() const
{
	if (!EnemyPerceptionComponent)
	{
		return nullptr;
	}

	PerceivedActorScratch.Reset();
	EnemyPerceptionComponent->GetCurrentlyPerceivedActors(
		UAISense_Sight::StaticClass(),
		PerceivedActorScratch);

	return SelectPreferredVisibleTarget(PerceivedActorScratch);
}

AActor* AEnemyAIController::SelectPreferredVisibleTarget(
	const TArray<AActor*>& VisibleActors) const
{
	DirectlyVisibleTargets.Reset();
	const APawn* ControlledPawn = GetPawn();
	AActor* BestTarget = nullptr;
	EEnemyTargetPriority BestPriority = EEnemyTargetPriority::Invalid;
	float BestDistanceSquared = TNumericLimits<float>::Max();
	for (AActor* Target : VisibleActors)
	{
		if (!IsValidDetectionTarget(Target))
		{
			continue;
		}
		DirectlyVisibleTargets.Add(Target);

		const EEnemyTargetPriority TargetPriority = OutlierEnemyTargetRules::GetPriority(Target);
		if (TargetPriority == EEnemyTargetPriority::Invalid)
		{
			continue;
		}

		const float DistanceSquared = ControlledPawn
			? FVector::DistSquared(ControlledPawn->GetActorLocation(), Target->GetActorLocation())
			: 0.0f;
		if (!BestTarget ||
			TargetPriority < BestPriority ||
			(TargetPriority == BestPriority && DistanceSquared < BestDistanceSquared))
		{
			BestTarget = Target;
			BestPriority = TargetPriority;
			BestDistanceSquared = DistanceSquared;
		}
	}

	return BestTarget;
}

AActor* AEnemyAIController::GetPreferredKnownVisibleTarget() const
{
	const APawn* ControlledPawn = GetPawn();
	AActor* Best = IsValidDetectionTarget(LastVisibleTarget.Get()) ? LastVisibleTarget.Get() : nullptr;
	for (const TWeakObjectPtr<AActor>& Entry : DirectlyVisibleTargets)
	{
		AActor* Candidate = Entry.Get();
		if (!IsValidDetectionTarget(Candidate))
		{
			continue;
		}
		const auto Priority = OutlierEnemyTargetRules::GetPriority(Candidate);
		const auto BestPriority = OutlierEnemyTargetRules::GetPriority(Best);
		if (Priority != OutlierEnemyTargetRules::ETargetPriority::Invalid && (!Best || Priority < BestPriority
			|| (Priority == BestPriority && ControlledPawn && FVector::DistSquared(ControlledPawn->GetActorLocation(), Candidate->GetActorLocation())
				< FVector::DistSquared(ControlledPawn->GetActorLocation(), Best->GetActorLocation()))))
		{
			Best = Candidate;
		}
	}
	return Best;
}

AActor* AEnemyAIController::GetPreferredAttackableTurretTarget(const AActor* CurrentTarget) const
{
	const AAutoTurret* Turret = Cast<AAutoTurret>(GetPawn());
	if (!Turret || !Turret->UsesNormalTurretAttackPolicy())
	{
		return nullptr;
	}
	FVector AimPoint;
	// 종류 순위보다 현재의 실제 공격 가능성을 먼저 본다. 쿨다운은 물리적 사선과 별개다.
	if (Turret->TryGetAttackableVisibleTargetAim(CurrentTarget, AimPoint))
	{
		return const_cast<AActor*>(CurrentTarget);
	}
	AActor* Best = nullptr;
	const auto Consider = [&](AActor* Candidate)
	{
		if (!Turret->TryGetAttackableVisibleTargetAim(Candidate, AimPoint))
		{
			return;
		}
		const auto Priority = OutlierEnemyTargetRules::GetPriority(Candidate);
		const auto BestPriority = OutlierEnemyTargetRules::GetPriority(Best);
		if (!Best || Priority < BestPriority
			|| (Priority == BestPriority && FVector::DistSquared(Turret->GetActorLocation(), Candidate->GetActorLocation())
				< FVector::DistSquared(Turret->GetActorLocation(), Best->GetActorLocation())))
		{
			Best = Candidate;
		}
	};
	Consider(LastVisibleTarget.Get());
	for (const TWeakObjectPtr<AActor>& Entry : DirectlyVisibleTargets)
	{
		Consider(Entry.Get());
	}
	return Best;
}

bool AEnemyAIController::IsTargetDirectlyVisible(const AActor* Target) const
{
	return IsValidTrackingTarget(Target)
		&& (LastVisibleTarget.Get() == Target || DirectlyVisibleTargets.Contains(Target));
}

void AEnemyAIController::HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	AEnemyBase* Enemy = Cast<AEnemyBase>(GetPawn());
	if (!Enemy || !Actor || !Enemy->CanUseEnemyPerception())
	{
		return;
	}

	if (IsStealthedDetectionTarget(Actor))
	{
		const TWeakObjectPtr<AActor> TargetKey(Actor);
		// Keep a stealthed target out of the perception cache so ending stealth
		// produces a fresh successful sight stimulus.
		EnemyPerceptionComponent->ForgetActor(Actor);
		if (ProcessedStealthedTargets.Contains(TargetKey))
		{
			return;
		}

		ProcessedStealthedTargets.Add(TargetKey);
		AActor* PreferredTarget = GetPreferredVisibleTarget();
		const bool bAnyPlayerVisible = IsValid(PreferredTarget);
		UpdateVisibleTarget(Enemy, PreferredTarget);
		if (bAnyPlayerVisible && Enemy->CanUseRoomTargetSharing())
		{
			StartSharedTargetReporting();
			ReportSharedTargetContact(Enemy, PreferredTarget);
		}
		else
		{
			StopSharedTargetReporting(true);
		}
		return;
	}

	ProcessedStealthedTargets.Remove(TWeakObjectPtr<AActor>(Actor));

	if (!IsValidDetectionTarget(Actor))
	{
		return;
	}

	if (Stimulus.Type == UAISense::GetSenseID<UAISense_Sight>())
	{
		HandleSightStimulus(Enemy, Actor, Stimulus);
		LogPerceptionDiagnostic(*Enemy, *Actor, Stimulus);
	}
	else if (Stimulus.Type == UAISense::GetSenseID<UAISense_Hearing>())
	{
		LogPerceptionDiagnostic(*Enemy, *Actor, Stimulus);
		HandleHearingStimulus(Enemy, Actor, Stimulus);
	}
}

void AEnemyAIController::HandleSightStimulus(AEnemyBase* Enemy, AActor* Actor, const FAIStimulus& Stimulus)
{
	if (Stimulus.WasSuccessfullySensed() && Enemy->GetCombatState() != EEnemyCombatState::Stun)
	{
		// 기존 조사 소유권을 해제하기 전에 시각 목표의 대상·발생 시각을 보관한다.
		AActor* ObservationTarget = GetPreferredVisibleTarget();
		if (!IsValid(ObservationTarget)) ObservationTarget = Actor;
		Enemy->RecordInvestigationSight(ObservationTarget, ObservationTarget->GetActorLocation(), GetWorld()->GetTimeSeconds());
		Enemy->OnDirectSightForHitResponse(ObservationTarget->GetActorLocation());
		Enemy->RecordAlertClue(GetWorld()->GetTimeSeconds());
	}
	if (Enemy->HasAuthority() && Enemy->GetCombatState() != EEnemyCombatState::Combat)
	{
		// 시야 변경 시각을 먼저 기록하여 Task 갱신 사이의 노출/은폐 구간도 보존한다.
		if (Enemy->GetCombatState() != EEnemyCombatState::Stun)
		{
			ObserveInitialSight(*Enemy, *Actor, Stimulus.WasSuccessfullySensed(), &Stimulus.ReceiverLocation);
		}
		if (Stimulus.WasSuccessfullySensed()) bInitialAwarenessAlert = true;
	}
	if (Stimulus.WasSuccessfullySensed())
	{
		// 여러 플레이어를 동시에 감지할 때 마지막 콜백 대상이 아니라
		// 현재 우선순위가 가장 높은 타겟을 전투 기준 위치로 사용한다.
		AActor* PreferredTarget = GetPreferredVisibleTarget();
		AActor* LocationSource = IsValid(PreferredTarget) ? PreferredTarget : Actor;
		const FVector TargetLocation = LocationSource->GetActorLocation();

		UpdateVisibleTarget(Enemy, LocationSource);
		if (!Enemy->HasTrackableTarget()) Enemy->UpdateLastKnownPlayerLocation(TargetLocation);

		if (!Enemy->IsInCombat())
		{
			// 이미 전투 중인 방에서는 직접 감지도 즉시 합류 조건이다.
			// 아직 전투가 시작되지 않은 방은 기존 Alert 확인 절차를 유지한다.
			const UEnemyRoomSubsystem* Rooms = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>();
			if (Rooms && Rooms->IsRoomInCombat(Enemy->GetDefaultRoomTag()))
			{
				Enemy->EnterCombatFromRoom(TargetLocation, false, true);
			}
			else
			{
				Enemy->EnterAlertFromPerception(TargetLocation);
			}
		}

		if (Enemy->CanUseRoomTargetSharing())
		{
			StartSharedTargetReporting();
			ReportSharedTargetContact(Enemy, LocationSource);
		}
		return;
	}

	AActor* PreferredTarget = GetPreferredVisibleTarget();
	const bool bAnyPlayerVisible = IsValid(PreferredTarget);
	if (bAnyPlayerVisible && Enemy->CanUseRoomTargetSharing())
	{
		StartSharedTargetReporting();
		ReportSharedTargetContact(Enemy, PreferredTarget);
	}
	else
	{
		StopSharedTargetReporting(true);
	}
	UpdateVisibleTarget(Enemy, PreferredTarget);
}

void AEnemyAIController::ResetInitialAwareness()
{
	AwarenessTargets.Reset();
	bInitialAwarenessAlert = false;
}

void AEnemyAIController::ObserveInitialSight(AEnemyBase& Enemy, AActor& Target, bool bVisible, const FVector* ReceiverLocation)
{
	FAwarenessTarget* State = AwarenessTargets.Find(&Target);
	if (!State && !bVisible) return;
	if (!State) State = &AwarenessTargets.Add(&Target);
	State->bVisible = bVisible;
	if (bVisible)
	{
		// 실제 시각 관측에서만 거리를 읽는다. 은폐 중 청각 갱신은 마지막 시각 속도와 소리 좌표만 사용한다.
		const FEnemyStat& Stat = Enemy.GetRuntimeStat();
		State->LastSeenLocation = Target.GetActorLocation();
		const FVector Origin = ReceiverLocation ? *ReceiverLocation : Enemy.GetPawnViewLocation();
		State->SightFillDuration = OutlierEnemySight::ResolveSightFillDuration(
			FVector::Distance(Origin, State->LastSeenLocation), Stat.AwarenessNearFillDuration,
			Stat.AwarenessFillDuration, Stat.AwarenessNearDistance, Stat.AwarenessFarDistance);
		State->LastSeenAt = GetWorld()->GetTimeSeconds();
		Enemy.RecordAlertClue(State->LastSeenAt);
	}
	AdvanceInitialAwareness(Enemy, *State);
}

void AEnemyAIController::AdvanceInitialAwareness(AEnemyBase& Enemy, FAwarenessTarget& State)
{
	const FEnemyStat& Stat = Enemy.GetRuntimeStat();
	const double HearingExpiresAt = State.LastHeardAt < 0.0 ? -1.0 : State.LastHeardAt
		+ OutlierEnemySight::ResolveHearingAwarenessHoldDuration(Stat.HearingAwarenessHoldDuration);
	State.Progress.Observe(GetWorld()->GetTimeSeconds(), State.bVisible,
		Stat.AwarenessFillDuration, Stat.AwarenessDecayDuration, HearingExpiresAt, Stat.HearingAwarenessRatio,
		State.SightFillDuration);
}

void AEnemyAIController::ObserveInitialHearing(AEnemyBase& Enemy, AActor& Target,
	const FVector& Location, double OccurredAt)
{
	if (Enemy.GetCombatState() == EEnemyCombatState::Combat || Enemy.GetCombatState() == EEnemyCombatState::Stun) return;
	const double Hold = OutlierEnemySight::ResolveHearingAwarenessHoldDuration(Enemy.GetRuntimeStat().HearingAwarenessHoldDuration);
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now >= OccurredAt + Hold) return;
	FAwarenessTarget& State = AwarenessTargets.FindOrAdd(&Target);
	if (OccurredAt <= State.LastHeardAt) return;
	// 먼저 이전 관측을 현재 시각까지 계산한다. 늦게 받은 소리로 과거의 미인지 구간을 채우지 않는다.
	AdvanceInitialAwareness(Enemy, State);
	State.LastHeardAt = OccurredAt;
	State.LastHeardLocation = Location;
	AdvanceInitialAwareness(Enemy, State);
	bInitialAwarenessAlert = true;
}

bool AEnemyAIController::ResolveInitialAwarenessAlert()
{
	AEnemyBase* Enemy = Cast<AEnemyBase>(GetPawn());
	// 일시 감지 억제/스턴으로 인지 캐시가 비어도 원래 단서의 최소 기한은 유지한다.
	if (!HasAuthority() || !Enemy || (!bInitialAwarenessAlert && !Enemy->HasAlertClue())
		|| Enemy->GetCombatState() != EEnemyCombatState::Alert) return false;

	// 같은 대상의 수치는 Task 재선택이나 다른 대상의 우선순위 변경으로 초기화하지 않는다.
	// 반대로 사망/은신/팀 변경된 대상의 이전 수치를 새 관측 수명으로 넘기지 않는다.
	for (auto It = AwarenessTargets.CreateIterator(); It; ++It)
	{
		if (!IsValidDetectionTarget(It.Key().Get())) It.RemoveCurrent();
	}
	GetPreferredVisibleTarget();
	for (const TWeakObjectPtr<AActor>& Target : DirectlyVisibleTargets)
	{
		if (AActor* Actor = Target.Get()) ObserveInitialSight(*Enemy, *Actor, true);
	}

	bool bHasPendingAwareness = false;
	for (auto& Pair : AwarenessTargets)
	{
		FAwarenessTarget& State = Pair.Value;
		State.bVisible = DirectlyVisibleTargets.Contains(Pair.Key);
		AdvanceInitialAwareness(*Enemy, State);
		if (State.Progress.IsComplete())
		{
			// 청각으로 완료한 대상은 실제 위치 추적 대신 해당 출처의 마지막 소리 좌표를 조사한다.
			// 마지막 시각/청각의 순서가 아니라 실제 100%를 채운 관측 구간으로 구분한다.
			const bool bHeardCompletion = !State.bVisible && State.Progress.WasCompletedFromHearing();
			bool bCommitted = false;
			if (bHeardCompletion)
			{
				// 피격 조사 중 청각 수치는 계속 누적하되, 소리만으로 현재 조사/회전을 교체하지 않는다.
				if (Enemy->HasHitResponse()) return true;
				bCommitted = Enemy->CommitAlertToCombatFromHearing(Pair.Key.Get(), State.LastHeardLocation, State.LastHeardAt);
			}
			else
			{
				Enemy->UpdateLastKnownPlayerLocation(State.LastSeenLocation);
				bCommitted = Enemy->CommitAlertToCombat();
			}
			if (bCommitted) ResetInitialAwareness();
			return true;
		}
		bHasPendingAwareness |= State.bVisible || State.Progress.IsHearingActive(GetWorld()->GetTimeSeconds())
			|| State.Progress.GetAwareness() > 0.0;
	}
	// 수치는 조사 이동·수색 중에도 진행하되, 0이 됐다는 이유로 진행 중인 조사를 취소하지 않는다.
	const bool bInvestigating = Enemy->IsLocationInvestigationActive()
		|| Enemy->GetPerceptionGoal().Type == EEnemyPerceptionGoalType::InvestigateNoise;
	if (!bHasPendingAwareness && !bInvestigating && Enemy->CommitAlertToNonCombat()) ResetInitialAwareness();
	return true;
}

void AEnemyAIController::UpdateVisibleTarget(AEnemyBase* Enemy, AActor* PreferredTarget)
{
	const bool bIsVisible = IsValid(PreferredTarget);
	const bool bVisibilityChanged = Enemy->IsPlayerCurrentlyVisible() != bIsVisible;
	LastVisibleTarget = bIsVisible ? PreferredTarget : nullptr;
	if (!bIsVisible) DirectlyVisibleTargets.Reset();
	// 직접 감지 캐시는 공유 보고용이다. 전투 대상 교체는 현재 추적의 유효성을 먼저 확인한다.
	Enemy->SetPlayerCurrentlyVisible(bIsVisible);
	if (!bVisibilityChanged) Enemy->RefreshTargetTracking();
}

void AEnemyAIController::HandleHearingStimulus(AEnemyBase* Enemy, AActor* Actor, const FAIStimulus& Stimulus)
{
	if (!Enemy->HasAuthority() || !Stimulus.WasSuccessfullySensed())
	{
		return;
	}
	if (!Enemy->UsesHearingPerception())
	{
		return;
	}

	const FVector HeardLocation = Stimulus.StimulusLocation;
	const float Age = Stimulus.GetAge();
	// 청각 기록은 추적과 별개다. 성공한 자극의 경과시간을 빼서 오래된 수신을 연장하지 않는다.
	if (!FMath::IsFinite(Age) || Age < 0.0f) return;
	const double OccurredAt = Enemy->GetWorld()->GetTimeSeconds() - Age;
	bool bValidStimulus = false;
	const bool bRecorded = Enemy->RecordHeardNoise(Actor, HeardLocation, OccurredAt, &bValidStimulus);
	// 조사 목적지 채택/변경과 인지 시간은 별개다. 조사 중인 같은 출처의 발소리도 인지 만료를 연장한다.
	if (bValidStimulus) ObserveInitialHearing(*Enemy, *Actor, HeardLocation, OccurredAt);
	if (!bRecorded) return;

	// 후보 수신과 목적지 채택은 다르다. 보류된 소리로 경계 방향이나 수색 중심을 덮어쓰지 않는다.
	const FEnemyPerceptionGoal& Goal = Enemy->GetPerceptionGoal();
	const EEnemyCombatState State = Enemy->GetCombatState();
	if (Enemy->IsLocationInvestigationActive()) return;
	FVector SelectedLocation = Goal.Location;
	if (State == EEnemyCombatState::Stun)
	{
		// 기절 중에는 감지 의도가 비어 있다. 조사를 시작하지 않고 기존 복귀 상태 승격만 유지한다.
		FEnemyHeardNoise Candidate;
		if (!Enemy->TryGetHeardNoise(Candidate)) return;
		SelectedLocation = Candidate.Location;
	}
	else if (Goal.Type != EEnemyPerceptionGoalType::InvestigateNoise) return;
	// 유효한 전투 대상의 실제 추적 위치를 소리로 덮어쓰지 않는다.
	// 청각만 남았다면 들린 위치를 기록하되 직접 시야나 방 공유 관측으로 취급하지 않는다.
	// 진행 중인 수색의 고정 중심/공유 버전도 이 갱신만으로는 바뀌지 않는다.
	if (!Enemy->HasTrackableTarget()) Enemy->UpdateLastKnownPlayerLocation(SelectedLocation);

	// 이미 참여 중인 적의 수색이나 공격을 소리만으로 다시 시작하지 않는다.
	if (State == EEnemyCombatState::Combat)
	{
		return;
	}

	// 소속 방이 전투 중이면 공유 타깃/시야/Alert 대기 없이 자신만 합류한다.
	// 방 전투를 다시 전파하지 않으며, Stun이면 기존 진입 함수가 복귀 상태만 Combat으로 바꾼다.
	const UEnemyRoomSubsystem* Rooms = Enemy->GetWorld()->GetSubsystem<UEnemyRoomSubsystem>();
	if (Rooms && Rooms->IsRoomInCombat(Enemy->GetDefaultRoomTag()))
	{
		Enemy->EnterCombatFromRoom(SelectedLocation, false, true);
		return;
	}

	// 아직 전투가 시작되지 않은 방에서는 기존 경계 흐름을 유지한다.
	if (State == EEnemyCombatState::NonCombat || State == EEnemyCombatState::Returning ||
		State == EEnemyCombatState::Stun)
	{
		Enemy->EnterAlertFromPerception(SelectedLocation);
	}
}

void AEnemyAIController::StartSharedTargetReporting()
{
	if (!HasAuthority()
		|| !GetWorld())
	{
		return;
	}

	if (GetWorldTimerManager().IsTimerActive(SharedTargetReportTimerHandle))
	{
		return;
	}

	GetWorldTimerManager().SetTimer(
		SharedTargetReportTimerHandle,
		this,
		&AEnemyAIController::RefreshSharedTargetContact,
		FMath::Max(SharedTargetReportInterval, 0.05f),
		true);
}

void AEnemyAIController::StopSharedTargetReporting(bool bRemoveObserver)
{
	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(SharedTargetReportTimerHandle);
	}

	if (!bRemoveObserver)
	{
		return;
	}

	AEnemyBase* Enemy = Cast<AEnemyBase>(GetPawn());
	UEnemyRoomSubsystem* RoomSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UEnemyRoomSubsystem>()
		: nullptr;
	if (Enemy && RoomSubsystem)
	{
		RoomSubsystem->RemoveRoomTargetObserver(Enemy);
	}
}

void AEnemyAIController::RefreshSharedTargetContact()
{
	AEnemyBase* Enemy = Cast<AEnemyBase>(GetPawn());
	if (Enemy && Enemy->GetCombatState() == EEnemyCombatState::Stun)
	{
		if (UEnemyRoomSubsystem* RoomSubsystem = GetWorld()
			? GetWorld()->GetSubsystem<UEnemyRoomSubsystem>()
			: nullptr)
		{
			RoomSubsystem->RemoveRoomTargetObserver(Enemy);
		}
		return;
	}

	// 이미 감지 중이던 플레이어가 은신한 경우에도 Perception 캐시에서 제거한다.
	AActor* TargetActor = ForgetStealthedPerceivedActors();
	ReportSharedTargetContact(Enemy, TargetActor);
}

void AEnemyAIController::ReportSharedTargetContact(
	AEnemyBase* Enemy,
	AActor* TargetActor)
{
	if (!Enemy
		|| !Enemy->CanUseRoomTargetSharing()
		|| !IsValid(TargetActor))
	{
		StopSharedTargetReporting(true);
		if (Enemy)
		{
			Enemy->SetPlayerCurrentlyVisible(false);
		}
		return;
	}

	UEnemyRoomSubsystem* RoomSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UEnemyRoomSubsystem>()
		: nullptr;
	if (!RoomSubsystem)
	{
		return;
	}

	RoomSubsystem->ReportRoomTargetContact(
		Enemy,
		TargetActor,
		TargetActor->GetActorLocation());
}

AActor* AEnemyAIController::ForgetStealthedPerceivedActors()
{
	if (!EnemyPerceptionComponent)
	{
		return nullptr;
	}

	PerceivedActorScratch.Reset();
	EnemyPerceptionComponent->GetCurrentlyPerceivedActors(
		UAISense_Sight::StaticClass(),
		PerceivedActorScratch);

	for (AActor* Actor : PerceivedActorScratch)
	{
		if (IsStealthedDetectionTarget(Actor))
		{
			const TWeakObjectPtr<AActor> TargetKey(Actor);
			// A forgotten target can be sensed again while stealth remains active.
			// Remove it every time, while keeping logs and state changes one-shot.
			EnemyPerceptionComponent->ForgetActor(Actor);
			if (ProcessedStealthedTargets.Contains(TargetKey))
			{
				continue;
			}

			ProcessedStealthedTargets.Add(TargetKey);
		}
	}

	return SelectPreferredVisibleTarget(PerceivedActorScratch);
}

void AEnemyAIController::ConfigureSightFromEnemy(AEnemyBase* Enemy)
{
	if (!Enemy || !SightConfig || !EnemyPerceptionComponent)
	{
		return;
	}

	const FEnemyStat& RuntimeStat = Enemy->GetRuntimeStat();
	const float PeripheralVisionAngle = Enemy->IsInCombat()
		? RuntimeStat.BattlePeripheralVisionAngle
		: RuntimeStat.PeripheralVisionAngle;

	SightConfig->SightRadius = FMath::Max(RuntimeStat.SightRadius, 0.0f);
	SightConfig->LoseSightRadius = FMath::Max(RuntimeStat.LoseSightRadius, SightConfig->SightRadius);
	SightConfig->PeripheralVisionAngleDegrees = FMath::Clamp(PeripheralVisionAngle, 0.0f, 180.0f);
	EnemyPerceptionComponent->ConfigureSense(*SightConfig);
	EnemyPerceptionComponent->RequestStimuliListenerUpdate();

	if (IsEnemyPerceptionDiagnosticsEnabled())
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[EnemyPerceptionDiag] SightConfig Enemy=%s Class=%s Type=%s State=%s SightRadius=%.1f LoseSightRadius=%.1f PeripheralVisionAngle=%.1f ActorForward=%s ViewForward=%s"),
			*Enemy->GetName(),
			*Enemy->GetClass()->GetName(),
			*UEnum::GetValueAsString(RuntimeStat.Type),
			*UEnum::GetValueAsString(Enemy->GetCombatState()),
			SightConfig->SightRadius,
			SightConfig->LoseSightRadius,
			SightConfig->PeripheralVisionAngleDegrees,
			*Enemy->GetActorForwardVector().ToCompactString(),
			*Enemy->GetViewRotation().Vector().ToCompactString());
	}
}

void AEnemyAIController::ConfigureHearingFromEnemy(AEnemyBase* Enemy)
{
	if (!Enemy || !HearingConfig || !EnemyPerceptionComponent)
	{
		return;
	}

	const FEnemyStat& RuntimeStat = Enemy->GetRuntimeStat();
	if (!Enemy->UsesHearingPerception())
	{
		HearingConfig->HearingRange = 0.0f;
		EnemyPerceptionComponent->ConfigureSense(*HearingConfig);
		EnemyPerceptionComponent->SetSenseEnabled(UAISense_Hearing::StaticClass(), false);
		return;
	}
	const float HearingRange = Enemy->IsInCombat() ? RuntimeStat.BattleHearingRange : RuntimeStat.HearingRange;

	HearingConfig->HearingRange = FMath::Max(HearingRange, 0.0f);
	EnemyPerceptionComponent->ConfigureSense(*HearingConfig);
	EnemyPerceptionComponent->RequestStimuliListenerUpdate();
}

bool AEnemyAIController::IsValidDetectionTarget(const AActor* TargetActor) const
{
	const APawn* TargetPawn = Cast<APawn>(TargetActor);
	if (!TargetPawn || OutlierEnemyTargetRules::IsUnavailable(TargetActor) || TargetActor->IsActorBeingDestroyed())
	{
		return false;
	}

	const AEnemyBase* ControlledEnemy = Cast<AEnemyBase>(GetPawn());
	if (!ControlledEnemy)
	{
		return false;
	}

	FGenericTeamId TargetTeamId(OutlierTeamIds::Player);
	if (const AEnemyBase* EnemyTarget = Cast<AEnemyBase>(TargetActor))
	{
		TargetTeamId = EnemyTarget->GetGenericTeamId();
	}
	else if (!TargetActor->IsA<AShooterCharacter>() && !TargetActor->IsA<APartnerCharacter>())
	{
		return false;
	}

	if (ControlledEnemy->GetGenericTeamId() == TargetTeamId)
	{
		return false;
	}

	if (const AShooterCharacter* Shooter = Cast<AShooterCharacter>(TargetActor))
	{
		return !Shooter->IsDead();
	}

	if (const AEnemyBase* EnemyTarget = Cast<AEnemyBase>(TargetActor))
	{
		const UAbilitySystemComponent* AbilitySystem = EnemyTarget->GetAbilitySystemComponent();
		return !EnemyTarget->IsDead() && EnemyTarget->GetCurrentHealth() > 0.0f
			&& (!AbilitySystem || (!AbilitySystem->HasMatchingGameplayTag(OutlierGameplayTags::State::Stealthed())
				&& !AbilitySystem->HasMatchingGameplayTag(OutlierGameplayTags::State::Rebooting())));
	}

	return true;
}

bool AEnemyAIController::IsStealthedDetectionTarget(const AActor* TargetActor) const
{
	const IGameplayTagProviderInterface* TagProvider = Cast<IGameplayTagProviderInterface>(TargetActor);
	return TagProvider
		&& TagProvider->GetOwnedGameplayTagsForQuery().HasTagExact(OutlierGameplayTags::State::Stealthed());
}
