#include "Tutorial/Actors/Tuto_InteractionDoorSwitch.h"

#include "FirstPerson/FirstPersonCharacter.h"
#include "OutlierPlayerState.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideTargetComponent.h"

ATuto_InteractionDoorSwitch::ATuto_InteractionDoorSwitch()
{
	EventTriggers.SetNum(1);
	WorldGuideTarget = CreateDefaultSubobject<UOutlierWorldGuideTargetComponent>(TEXT("WorldGuideTarget"));
	WorldGuideTarget->SetupAttachment(GetRootComponent());
}

bool ATuto_InteractionDoorSwitch::Interact(AFirstPersonCharacter* Interactor)
{
	if (!Super::Interact(Interactor)) return false;
	HandleSwitchInteracted(Interactor->GetPlayerState<AOutlierPlayerState>());
	return true;
}

void ATuto_InteractionDoorSwitch::HandleSwitchInteracted(AOutlierPlayerState* SourcePlayerState)
{
	UOutlierWorldGuideTargetComponent::CompleteOnActor(this);
	TriggerEvent(0, SourcePlayerState);
}
