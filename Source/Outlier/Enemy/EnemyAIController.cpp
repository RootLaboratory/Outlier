#include "Enemy/EnemyAIController.h"

#include "Drone/Partner/PartnerCharacter.h"
#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "Engine/World.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Interface/GameplayTagProviderInterface.h"
#include "TimerManager.h"
#include "OutlierPlayerState.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Shooter/ShooterCharacter.h"
#include "Team/OutlierTeamIds.h"

DEFINE_LOG_CATEGORY_STATIC(LogEnemyPerception, Log, All);

namespace
{
	enum class EEnemyTargetPriority : uint8
	{
		HackedTurret,
		PossessedDrone,
		Shooter,
		Partner,
		Invalid
	};

	EEnemyTargetPriority GetEnemyTargetPriority(const AActor* TargetActor)
	{
		if (const AEnemyBase* Enemy = Cast<AEnemyBase>(TargetActor))
		{
			if (!Enemy->IsEnemyPossessed())
			{
				return EEnemyTargetPriority::Invalid;
			}

			return Enemy->GetRuntimeStat().Type == EEnemyType::Turret
				? EEnemyTargetPriority::HackedTurret
				: EEnemyTargetPriority::PossessedDrone;
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
		RefreshPerceptionConfigFromPawn();
		SetEnemyPerceptionEnabled(!Enemy->IsEnemyPossessed());
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

	// LookAround Task가 Pitch를 제어하는 동안에는 AIController의 기본 Pitch 초기화를 막는다.
	if (TaskDrivenControlPitchCount > 0)
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

void AEnemyAIController::RefreshPerceptionConfigFromPawn()
{
	if (AEnemyBase* Enemy = Cast<AEnemyBase>(GetPawn()))
	{
		ConfigureSightFromEnemy(Enemy);
		ConfigureHearingFromEnemy(Enemy);
	}
}

void AEnemyAIController::SetEnemyPerceptionEnabled(bool bEnabled)
{
	if (!EnemyPerceptionComponent)
	{
		return;
	}

	EnemyPerceptionComponent->SetComponentTickEnabled(bEnabled);
	EnemyPerceptionComponent->SetSenseEnabled(UAISense_Sight::StaticClass(), bEnabled);
	EnemyPerceptionComponent->SetSenseEnabled(UAISense_Hearing::StaticClass(), bEnabled);

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

	TArray<AActor*> VisibleActors;
	EnemyPerceptionComponent->GetCurrentlyPerceivedActors(
		UAISense_Sight::StaticClass(),
		VisibleActors);

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
	if (!Enemy || !Actor || Enemy->IsEnemyPossessed())
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
		const bool bAnyPlayerVisible = HasAnyVisiblePlayer();
		UE_LOG(
			LogEnemyPerception,
			Display,
			TEXT("[TargetLost][Stealth] Enemy=%s Target=%s AnyOtherTargetVisible=%d CombatState=%s"),
			*GetNameSafe(Enemy),
			*GetNameSafe(Actor),
			bAnyPlayerVisible ? 1 : 0,
			*UEnum::GetValueAsString(Enemy->GetCombatState()));
		Enemy->SetPlayerCurrentlyVisible(bAnyPlayerVisible);
		if (bAnyPlayerVisible)
		{
			StartSharedTargetReporting();
			RefreshSharedTargetContact();
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
	}
	else if (Stimulus.Type == UAISense::GetSenseID<UAISense_Hearing>())
	{
		HandleHearingStimulus(Enemy, Actor, Stimulus);
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

		StartSharedTargetReporting();
		RefreshSharedTargetContact();
		return;
	}

	const bool bAnyPlayerVisible = HasAnyVisiblePlayer();
	UE_LOG(
		LogEnemyPerception,
		Display,
		TEXT("[TargetLost][Sight] Enemy=%s Target=%s StimulusAge=%.2f AnyOtherTargetVisible=%d LastKnown=%s CombatState=%s"),
		*GetNameSafe(Enemy),
		*GetNameSafe(Actor),
		Stimulus.GetAge(),
		bAnyPlayerVisible ? 1 : 0,
		*Enemy->GetLastKnownPlayerLocation().ToCompactString(),
		*UEnum::GetValueAsString(Enemy->GetCombatState()));
	if (bAnyPlayerVisible)
	{
		StartSharedTargetReporting();
		RefreshSharedTargetContact();
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

bool AEnemyAIController::HasAnyVisiblePlayer() const
{
	if (!EnemyPerceptionComponent)
	{
		return false;
	}

	TArray<AActor*> VisibleActors;
	EnemyPerceptionComponent->GetCurrentlyPerceivedActors(
		UAISense_Sight::StaticClass(),
		VisibleActors);

	return VisibleActors.ContainsByPredicate(
		[this](const AActor* Target)
		{
			return IsValidDetectionTarget(Target);
		});
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
	ForgetStealthedPerceivedActors();

	AActor* TargetActor = GetPreferredVisibleTarget();
	if (!Enemy
		|| Enemy->IsEnemyPossessed()
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

void AEnemyAIController::ForgetStealthedPerceivedActors()
{
	if (!EnemyPerceptionComponent)
	{
		return;
	}

	TArray<AActor*> VisibleActors;
	EnemyPerceptionComponent->GetCurrentlyPerceivedActors(
		UAISense_Sight::StaticClass(),
		VisibleActors);

	for (AActor* Actor : VisibleActors)
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
			UE_LOG(
				LogEnemyPerception,
				Display,
				TEXT("[TargetLost][StealthCache] Enemy=%s Target=%s Action=ForgetActor"),
				*GetNameSafe(GetPawn()),
				*GetNameSafe(Actor));
		}
	}
}

void AEnemyAIController::ConfigureSightFromEnemy(AEnemyBase* Enemy)
{
	if (!Enemy || !SightConfig || !EnemyPerceptionComponent)
	{
		return;
	}

	const FEnemyStat& RuntimeStat = Enemy->GetRuntimeStat();
	const float PeripheralVisionAngle = RuntimeStat.Type == EEnemyType::Turret
		? 180.0f
		: Enemy->IsInCombat() ? RuntimeStat.BattlePeripheralVisionAngle : RuntimeStat.PeripheralVisionAngle;

	SightConfig->SightRadius = FMath::Max(RuntimeStat.SightRadius, 0.0f);
	SightConfig->LoseSightRadius = FMath::Max(RuntimeStat.LoseSightRadius, SightConfig->SightRadius);
	SightConfig->PeripheralVisionAngleDegrees = FMath::Clamp(PeripheralVisionAngle, 0.0f, 180.0f);
	EnemyPerceptionComponent->ConfigureSense(*SightConfig);
	EnemyPerceptionComponent->RequestStimuliListenerUpdate();
}

void AEnemyAIController::ConfigureHearingFromEnemy(AEnemyBase* Enemy)
{
	if (!Enemy || !HearingConfig || !EnemyPerceptionComponent)
	{
		return;
	}

	const FEnemyStat& RuntimeStat = Enemy->GetRuntimeStat();
	const float HearingRange = Enemy->IsInCombat() ? RuntimeStat.BattleHearingRange : RuntimeStat.HearingRange;

	HearingConfig->HearingRange = FMath::Max(HearingRange, 0.0f);
	UE_LOG(LogTemp, Warning, TEXT("%f"), HearingConfig->HearingRange);
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
	return TargetPawn
		&& TargetPawn->GetPlayerState<AOutlierPlayerState>() != nullptr
		&& !IsStealthedDetectionTarget(TargetActor);
}

bool AEnemyAIController::IsStealthedDetectionTarget(const AActor* TargetActor) const
{
	const IGameplayTagProviderInterface* TagProvider = Cast<IGameplayTagProviderInterface>(TargetActor);
	return TagProvider
		&& TagProvider->GetOwnedGameplayTagsForQuery().HasTagExact(OutlierGameplayTags::State::Stealthed());
}
