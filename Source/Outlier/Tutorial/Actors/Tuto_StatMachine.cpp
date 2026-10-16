#include "Tutorial/Actors/Tuto_StatMachine.h"

#include "FirstPerson/FirstPersonCharacter.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/World.h"
#include "Network/OutlierArenaSubsystem.h"
#include "OutlierPlayerState.h"
#include "Shooter/ShooterCharacter.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideTargetComponent.h"

ATuto_StatMachine::ATuto_StatMachine()
{
	EventTriggers.SetNum(2);
	WorldGuideTarget = CreateDefaultSubobject<UOutlierWorldGuideTargetComponent>(TEXT("WorldGuideTarget"));
	WorldGuideTarget->SetupAttachment(GetRootComponent());
}

bool ATuto_StatMachine::Interact(AFirstPersonCharacter* Interactor)
{
	AOutlierPlayerState* Source = IsValid(Interactor) ? Interactor->GetPlayerState<AOutlierPlayerState>() : nullptr;
	AShooterCharacter* Shooter = Source ? Source->GetShooterCharacter() : nullptr;
	APartnerCharacter* Partner = Source ? Source->GetPartnerCharacter() : nullptr;
	AOutlierPlayerState* ShooterState = IsValid(Shooter) ? Shooter->GetPlayerState<AOutlierPlayerState>() : nullptr;
	AOutlierPlayerState* PartnerState = IsValid(Partner) ? Partner->GetPlayerState<AOutlierPlayerState>() : nullptr;
	const UOutlierArenaSubsystem* Arena = GetWorld() ? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>() : nullptr;
	if (!HasAuthority() || !IsValid(Source) || !IsValid(ShooterState) || !IsValid(PartnerState)
		|| !Arena || Interactor != Shooter || Source->GetPairId() == INDEX_NONE
		|| ShooterState->GetPairId() != Source->GetPairId() || PartnerState->GetPairId() != Source->GetPairId())
		return Super::Interact(Interactor);

	const int32 PairId = Source->GetPairId();
	const uint32 Generation = Arena->GetGameplayGeneration();
	const FCompletionSession* Existing = CompletionSessions.Find(PairId);
	const bool bExistingSession = Existing && Existing->Shooter == ShooterState && Existing->Partner == PartnerState
		&& Existing->GameplayGeneration == Generation && Existing->bInteractionAccepted;
	if (!bExistingSession)
	{
		FCompletionSession& Session = CompletionSessions.FindOrAdd(PairId);
		Session = FCompletionSession();
		Session.Shooter = ShooterState;
		Session.Partner = PartnerState;
		Session.GameplayGeneration = Generation;
		WatchPlayerState(ShooterState);
		WatchPlayerState(PartnerState);
	}
	// 소유 클라이언트가 닫힘을 보고하기 전에 바인딩한다. 거부된 상호작용은 활성 세션을 만들지 않는다.
	const bool bAccepted = Super::Interact(Interactor);
	if (!bAccepted)
	{
		if (!bExistingSession) CompletionSessions.Remove(PairId);
		return false;
	}
	if (FCompletionSession* Session = CompletionSessions.Find(PairId)) Session->bInteractionAccepted = true;
	UOutlierWorldGuideTargetComponent::CompleteOnActor(this);
	TryCompleteSession(PairId);
	return true;
}

void ATuto_StatMachine::HandleHackEffect(FGameplayTag EffectTag, const FHackResultContext& Context)
{
	const bool bWasBlocked = IsInteractionBlocked();
	Super::HandleHackEffect(EffectTag, Context);
	if (HasAuthority() && bWasBlocked && !IsInteractionBlocked())
	{
		const APawn* InstigatorPawn = Cast<APawn>(Context.InstigatorActor);
		HandleMachineUnlocked(InstigatorPawn ? InstigatorPawn->GetPlayerState<AOutlierPlayerState>() : nullptr);
	}
}

void ATuto_StatMachine::HandleMachineUnlocked(AOutlierPlayerState* SourcePlayerState)
{
	TriggerEvent(0, SourcePlayerState);
}

void ATuto_StatMachine::HandleMachineUICompleted(AOutlierPlayerState* SourcePlayerState)
{
	TriggerEvent(1, SourcePlayerState);
}

void ATuto_StatMachine::WatchPlayerState(AOutlierPlayerState* PlayerState)
{
	if (WatchedPlayerStates.Contains(PlayerState)) return;
	WatchedPlayerStates.Add(PlayerState);
	PlayerState->OnStatAllocatorUICompleted.AddUObject(this, &ThisClass::HandleStatAllocatorUICompleted);
}

void ATuto_StatMachine::HandleStatAllocatorUICompleted(AOutlierPlayerState* PlayerState, uint32 GameplayGeneration)
{
	if (!HasAuthority() || !IsValid(PlayerState)) return;
	const int32 PairId = PlayerState->GetPairId();
	FCompletionSession* Session = CompletionSessions.Find(PairId);
	if (!Session || Session->GameplayGeneration != GameplayGeneration) return;
	if (Session->Shooter == PlayerState) Session->bShooterCompleted = true;
	else if (Session->Partner == PlayerState) Session->bPartnerCompleted = true;
	else return;
	TryCompleteSession(PairId);
}

void ATuto_StatMachine::TryCompleteSession(int32 PairId)
{
	const FCompletionSession* Session = CompletionSessions.Find(PairId);
	if (!Session || !Session->bInteractionAccepted || !Session->bShooterCompleted || !Session->bPartnerCompleted) return;
	AOutlierPlayerState* ShooterState = Session->Shooter.Get();
	AOutlierPlayerState* PartnerState = Session->Partner.Get();
	if (!IsValid(ShooterState) || !IsValid(PartnerState) || ShooterState->GetPairId() != PairId
		|| PartnerState->GetPairId() != PairId || !ShooterState->IsShooterPlayer() || !PartnerState->IsPartnerPlayer()
		|| !ShooterState->IsStatAllocatorUICompletedForGeneration(Session->GameplayGeneration)
		|| !PartnerState->IsStatAllocatorUICompletedForGeneration(Session->GameplayGeneration)) return;
	CompletionSessions.Remove(PairId);
	HandleMachineUICompleted(ShooterState);
}

void ATuto_StatMachine::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (const TWeakObjectPtr<AOutlierPlayerState>& State : WatchedPlayerStates)
		if (AOutlierPlayerState* PlayerState = State.Get()) PlayerState->OnStatAllocatorUICompleted.RemoveAll(this);
	WatchedPlayerStates.Reset();
	CompletionSessions.Reset();
	Super::EndPlay(EndPlayReason);
}
