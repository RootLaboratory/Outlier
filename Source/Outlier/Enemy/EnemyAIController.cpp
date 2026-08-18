#include "Enemy/EnemyAIController.h"

#include "Enemy/AutoTurret.h"

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

	enum class EEnemyTargetPriority : uint8
	{
		HackedTurret,
		PossessedDrone,
		Shooter,
		Partner,
		EnemyUnit,
		Invalid
	};

	EEnemyTargetPriority GetEnemyTargetPriority(const AActor* TargetActor)
	{
		if (const AEnemyBase* Enemy = Cast<AEnemyBase>(TargetActor))
		{
			if (Enemy->GetGenericTeamId() == FGenericTeamId(OutlierTeamIds::Player))
			{
				return Enemy->GetRuntimeStat().Type == EEnemyType::Turret
				? EEnemyTargetPriority::HackedTurret
				: EEnemyTargetPriority::PossessedDrone;
			}

			return EEnemyTargetPriority::EnemyUnit;
		}

		if (TargetActor->IsA<AShooterCharacter>())
		{
			return EEnemyTargetPriority::Shooter;
		}

		if (TargetActor->IsA<APartnerCharacter>())
		{
			return EEnemyTargetPriority::Partner;
		}

		return EEnemyTargetPriority::Invalid;
	}
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

	ProcessedStealthedTargets.Reset();

	if (AEnemyBase* Enemy = Cast<AEnemyBase>(InPawn))
	{
		RefreshTeamAndPerceptionFromPawn();
	}
}

void AEnemyAIController::OnUnPossess()
{
	StopSharedTargetReporting(true);
	ProcessedStealthedTargets.Reset();
	TaskDrivenControlPitchCount = 0;
	Super::OnUnPossess();
}

void AEnemyAIController::UpdateControlRotation(float DeltaTime, bool bUpdatePawn)
{
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

void AEnemyAIController::ForgetDetectionTarget(AActor* TargetActor)
{
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
	Enemy->SetPlayerCurrentlyVisible(bHasVisibleTarget);

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
		ConfigureSightFromEnemy(Enemy);
		ConfigureHearingFromEnemy(Enemy);
	}
}

void AEnemyAIController::RefreshTeamAndPerceptionFromPawn()
{
	AEnemyBase* Enemy = Cast<AEnemyBase>(GetPawn());
	if (!Enemy)
	{
		return;
	}

	SetGenericTeamId(Enemy->GetGenericTeamId());
	if (EnemyPerceptionComponent)
	{
		EnemyPerceptionComponent->ForgetAll();
		ProcessedStealthedTargets.Reset();
	}
	RefreshPerceptionConfigFromPawn();
	SetEnemyPerceptionEnabled(Enemy->CanUseEnemyPerception());
}

void AEnemyAIController::SetEnemyPerceptionEnabled(bool bEnabled)
{
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
		EnemyPerceptionComponent->ForgetAll();
		ProcessedStealthedTargets.Reset();
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

		const EEnemyTargetPriority TargetPriority = GetEnemyTargetPriority(Target);
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
		Enemy->SetPlayerCurrentlyVisible(bAnyPlayerVisible);
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
		HandleHearingStimulus(Enemy, Actor, Stimulus);
		LogPerceptionDiagnostic(*Enemy, *Actor, Stimulus);
	}
}

void AEnemyAIController::HandleSightStimulus(AEnemyBase* Enemy, AActor* Actor, const FAIStimulus& Stimulus)
{
	if (Stimulus.WasSuccessfullySensed())
	{
		// 여러 플레이어를 동시에 감지할 때 마지막 콜백 대상이 아니라
		// 현재 우선순위가 가장 높은 타겟을 전투 기준 위치로 사용한다.
		AActor* PreferredTarget = GetPreferredVisibleTarget();
		AActor* LocationSource = IsValid(PreferredTarget) ? PreferredTarget : Actor;
		const FVector TargetLocation = LocationSource->GetActorLocation();

		Enemy->SetPlayerCurrentlyVisible(true);
		Enemy->UpdateLastKnownPlayerLocation(TargetLocation);

		if (!Enemy->IsInCombat())
		{
			Enemy->EnterAlertInArena(
				TargetLocation,
				ResolveArenaIdFromTarget(LocationSource));
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
	Enemy->SetPlayerCurrentlyVisible(bAnyPlayerVisible);
}

void AEnemyAIController::HandleHearingStimulus(AEnemyBase* Enemy, AActor* Actor, const FAIStimulus& Stimulus)
{
	if (!Stimulus.WasSuccessfullySensed())
	{
		return;
	}
	if (!Enemy->UsesHearingPerception())
	{
		return;
	}

	const FVector HeardLocation = Stimulus.StimulusLocation;

	Enemy->UpdateLastKnownPlayerLocation(HeardLocation);

	const EEnemyCombatState State = Enemy->GetCombatState();
	if (State == EEnemyCombatState::NonCombat ||
		State == EEnemyCombatState::Stun)
	{
		Enemy->EnterAlertInArena(
			HeardLocation,
			ResolveArenaIdFromTarget(Actor));
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
		ResolveArenaIdFromTarget(TargetActor),
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

int32 AEnemyAIController::ResolveArenaIdFromTarget(const AActor* TargetActor) const
{
	const APawn* TargetPawn = Cast<APawn>(TargetActor);
	if (!TargetPawn)
	{
		return INDEX_NONE;
	}

	const AOutlierPlayerState* OutlierPlayerState = TargetPawn->GetPlayerState<AOutlierPlayerState>();
	return OutlierPlayerState ? OutlierPlayerState->GetArenaId() : INDEX_NONE;
}

bool AEnemyAIController::IsValidDetectionTarget(const AActor* TargetActor) const
{
	const APawn* TargetPawn = Cast<APawn>(TargetActor);
	if (!TargetPawn || OutlierEnemyTargetRules::IsUnavailable(TargetActor))
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
		return EnemyTarget->GetCurrentHealth() > 0.0f;
	}

	return true;
}

bool AEnemyAIController::IsStealthedDetectionTarget(const AActor* TargetActor) const
{
	const IGameplayTagProviderInterface* TagProvider = Cast<IGameplayTagProviderInterface>(TargetActor);
	return TagProvider
		&& TagProvider->GetOwnedGameplayTagsForQuery().HasTagExact(OutlierGameplayTags::State::Stealthed());
}
