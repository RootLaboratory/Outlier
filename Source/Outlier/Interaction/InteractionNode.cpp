#include "Interaction/InteractionNode.h"

#include "Components/SceneComponent.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "Interaction/InteractableComponent.h"
#include "OutlierPlayerState.h"

AInteractionNode::AInteractionNode()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	InteractableComponent = CreateDefaultSubobject<UInteractableComponent>(TEXT("InteractableComponent"));
}

UInteractableComponent* AInteractionNode::GetInteractableComponent() const
{
	return InteractableComponent;
}

bool AInteractionNode::Interact(AFirstPersonCharacter* Interactor)
{
	if (!HasAuthority() || !Interactor || !InteractableComponent)
	{
		return false;
	}

	const EInteractionFlowResult FlowResult =
		InteractableComponent->AdvanceInteractionFlow(Interactor);

	if (FlowResult == EInteractionFlowResult::Rejected)
	{
		UE_LOG(LogTemp, Warning, TEXT("[InteractionNode] Interaction flow rejected Actor=%s"), *GetName());
		return false;
	}

	if (FlowResult == EInteractionFlowResult::HoldReady)
	{
		return true;
	}

	return AddNodeServer(Interactor);
}

bool AInteractionNode::AddNodeServer(AFirstPersonCharacter* Interactor)
{
	if (!HasAuthority())
	{
		return false;
	}

	AOutlierPlayerState* PlayerState =
		Interactor ? Interactor->GetPlayerState<AOutlierPlayerState>() : nullptr;

	if (!PlayerState)
	{
		return false;
	}

	if (!PlayerState->ShareNode(NodeRewardAmount))
	{
		return false;
	}

	/*UE_LOG(LogTemp, Verbose,
		TEXT("[InteractionNode] Shared node reward Player=%s Amount=%d PlayerTotal=%d"),
		*PlayerState->GetPlayerName(),
		NodeRewardAmount,
		PlayerState->GetNodeCount());*/

	return true;
}
