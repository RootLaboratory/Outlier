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
	// 거부된 두 번째 시도가 활성 예약의 소유자를 바꾸면 안 된다.
	if (AShooterCharacter* Existing = WatchedShooter.Get(); Existing && IsReservedFor(Existing))
		return Super::Interact(Interactor);

	StopWatchingAcquisition();
	WatchedShooter = Shooter;
	WatchedPlayerState = PlayerState;
	PlayerState->OnAcquiredSuitChanged.AddUObject(this, &ThisClass::HandleAcquiredSuitChanged);
	// Interact는 비동기 예약만 받으며, 아직 슈트를 지급하지 않는다.
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
	// 이 액터의 진행 중 지급만 이벤트를 발동할 수 있다. 복원/다른 픽업은 무시한다.
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
