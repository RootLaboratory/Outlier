#include "Tutorial/Actors/Tuto_Node.h"

#include "FirstPerson/FirstPersonCharacter.h"
#include "Interaction/InteractableComponent.h"
#include "OutlierPlayerState.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideTargetComponent.h"

ATuto_Node::ATuto_Node()
{
	EventTriggers.SetNum(2);
	WorldGuideTarget = CreateDefaultSubobject<UOutlierWorldGuideTargetComponent>(TEXT("WorldGuideTarget"));
	WorldGuideTarget->SetupAttachment(GetRootComponent());
}

bool ATuto_Node::Interact(AFirstPersonCharacter* Interactor)
{
	AOutlierPlayerState* SourcePlayerState = IsValid(Interactor) ? Interactor->GetPlayerState<AOutlierPlayerState>() : nullptr;
	if (HasAuthority() && SourcePlayerState) HandleNodeInteractionAttempted(SourcePlayerState);
	const bool bCompletesCollection = InteractableComponent && InteractableComponent->RequiresHoldInteract();
	if (!Super::Interact(Interactor)) return false;
	if (bCompletesCollection) HandleNodeCollected(SourcePlayerState);
	return true;
}

void ATuto_Node::HandleNodeInteractionAttempted(AOutlierPlayerState* SourcePlayerState)
{
	TriggerEvent(0, SourcePlayerState);
}

void ATuto_Node::HandleNodeCollected(AOutlierPlayerState* SourcePlayerState)
{
	UOutlierWorldGuideTargetComponent::CompleteOnActor(this);
	TriggerEvent(1, SourcePlayerState);
}
