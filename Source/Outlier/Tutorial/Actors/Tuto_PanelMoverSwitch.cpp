#include "Tutorial/Actors/Tuto_PanelMoverSwitch.h"

#include "FirstPerson/FirstPersonCharacter.h"
#include "OutlierPlayerState.h"
#include "Interaction/PanelMover.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideTargetComponent.h"

ATuto_PanelMoverSwitch::ATuto_PanelMoverSwitch()
{
	WorldGuideTarget = CreateDefaultSubobject<UOutlierWorldGuideTargetComponent>(TEXT("WorldGuideTarget"));
	WorldGuideTarget->SetupAttachment(GetRootComponent());
}

bool ATuto_PanelMoverSwitch::Interact(AFirstPersonCharacter* Interactor)
{
	ActivatingPlayer = IsValid(Interactor) ? Interactor->GetPlayerState<AOutlierPlayerState>() : nullptr;
	const bool bAccepted = Super::Interact(Interactor);
	if (bAccepted) CompleteInteraction(ActivatingPlayer.Get());
	ActivatingPlayer.Reset();
	return bAccepted;
}

void ATuto_PanelMoverSwitch::BeginPlay()
{
	Super::BeginPlay();
	// 체크포인트에서 복원한 스위치는 이동 도착을 기다리지 않고 완료 상태로 복원한다.
	if (bIsActivated) CompleteInteraction(nullptr);
}

bool ATuto_PanelMoverSwitch::ActivateTarget()
{
	ITuto_ProgressSourceInterface* Progress = Cast<ITuto_ProgressSourceInterface>(TargetPanel);
	if (!Progress) return false;
	Progress->SetProgressInstigator(ActivatingPlayer.Get());
	return Super::ActivateTarget();
}

void ATuto_PanelMoverSwitch::CompleteInteraction(AOutlierPlayerState* SourcePlayerState)
{
	if (!HasAuthority() || bEffectCompleted) return;
	bEffectCompleted = true;
	UOutlierWorldGuideTargetComponent::CompleteOnActor(this);
	CompleteProgress(SourcePlayerState);
}
