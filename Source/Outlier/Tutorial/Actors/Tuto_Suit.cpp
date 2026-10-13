#include "Tutorial/Actors/Tuto_Suit.h"

#include "OutlierPlayerState.h"
#include "Shooter/ShooterCharacter.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideTargetComponent.h"

ATuto_Suit::ATuto_Suit()
{
	EventTriggers.SetNum(1);
	WorldGuideTarget = CreateDefaultSubobject<UOutlierWorldGuideTargetComponent>(TEXT("WorldGuideTarget"));
	WorldGuideTarget->SetupAttachment(GetRootComponent());
}

bool ATuto_Suit::Interact(AFirstPersonCharacter* Interactor)
{
	AShooterCharacter* Shooter = Cast<AShooterCharacter>(Interactor);
	AOutlierPlayerState* PlayerState = IsValid(Shooter) ? Shooter->GetPlayerState<AOutlierPlayerState>() : nullptr;
	if (!HasAuthority() || !IsValid(PlayerState) || PlayerState->GetAcquiredSuit()) return Super::Interact(Interactor);
	// A rejected second attempt must not replace the owner of an active reservation.
	if (AShooterCharacter* Existing = WatchedShooter.Get(); Existing && IsReservedFor(Existing))
		return Super::Interact(Interactor);

	StopWatchingAcquisition();
	WatchedShooter = Shooter;
	WatchedPlayerState = PlayerState;
	PlayerState->OnAcquiredSuitChanged.AddUObject(this, &ThisClass::HandleAcquiredSuitChanged);
	// Interact accepts an asynchronous reservation; it does not grant the suit yet.
	const bool bAccepted = Super::Interact(Interactor);
	if (!bAccepted) StopWatchingAcquisition();
	return bAccepted;
}

void ATuto_Suit::HandleAcquiredSuitChanged(AOutlierPlayerState* PlayerState)
{
	AShooterCharacter* Shooter = WatchedShooter.Get();
	if (!HasAuthority() || !IsValid(PlayerState) || PlayerState != WatchedPlayerState.Get()
		|| !PlayerState->GetAcquiredSuit() || !IsValid(Shooter)
		|| Shooter->GetPlayerState<AOutlierPlayerState>() != PlayerState || !IsReservedFor(Shooter)) return;
	// Only this actor's in-progress grant may trigger the event. Restore/another pickup is ignored.
	StopWatchingAcquisition();
	HandleSuitAcquired(PlayerState);
}

void ATuto_Suit::HandleSuitAcquired(AOutlierPlayerState* SourcePlayerState)
{
	UOutlierWorldGuideTargetComponent::CompleteOnActor(this);
	TriggerEvent(0, SourcePlayerState);
}

void ATuto_Suit::StopWatchingAcquisition()
{
	if (AOutlierPlayerState* PlayerState = WatchedPlayerState.Get()) PlayerState->OnAcquiredSuitChanged.RemoveAll(this);
	WatchedPlayerState.Reset();
	WatchedShooter.Reset();
}

void ATuto_Suit::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopWatchingAcquisition();
	Super::EndPlay(EndPlayReason);
}
