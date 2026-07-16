#include "Enemy/EnemyAIController.h"

#include "Enemy/EnemyBase.h"
#include "OutlierPlayerState.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Team/OutlierTeamIds.h"

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

	if (AEnemyBase* Enemy = Cast<AEnemyBase>(InPawn))
	{
		RefreshPerceptionConfigFromPawn();
		SetEnemyPerceptionEnabled(!Enemy->IsEnemyPossessed());
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
		EnemyPerceptionComponent->Deactivate();
	}
}

void AEnemyAIController::HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	AEnemyBase* Enemy = Cast<AEnemyBase>(GetPawn());
	if (!Enemy || !Actor || Enemy->IsEnemyPossessed() || !IsValidDetectionTarget(Actor))
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
		const FVector TargetLocation = Actor->GetActorLocation();

		Enemy->SetPlayerCurrentlyVisible(true);
		Enemy->UpdateLastKnownPlayerLocation(TargetLocation);

		if (!Enemy->IsInCombat())
		{
			Enemy->EnterAlertInArena(TargetLocation, ResolveArenaIdFromTarget(Actor));
		}

		return;
	}

	const bool bAnyPlayerVisible = HasAnyVisiblePlayer();
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

	HearingConfig->HearingRange = FMath::Max(RuntimeStat.HearingRange, 0.0f);
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
	return TargetPawn && TargetPawn->GetPlayerState<AOutlierPlayerState>() != nullptr;
}
