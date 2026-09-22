#include "Interaction/InteractionNode.h"

#include "Audio/OutlierAudioSubsystem.h"
#include "Components/SceneComponent.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "Interaction/InteractableComponent.h"
#include "OutlierPlayerState.h"
#include "Save/OutlierSaveSubSystem.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

AInteractionNode::AInteractionNode()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	InteractableComponent = CreateDefaultSubobject<UInteractableComponent>(TEXT("InteractableComponent"));
}

void AInteractionNode::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
	{
		return;
	}
	if (UOutlierSaveSubSystem* SaveSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
		: nullptr)
	{
		bProgressIdRegistered = SaveSubsystem->RegisterWorldProgressId(
			EOutlierWorldProgressType::CollectedNode,
			PickupId,
			this);
		if (bProgressIdRegistered
			&& SaveSubsystem->HasWorldProgress(EOutlierWorldProgressType::CollectedNode, PickupId))
		{
			// 저장된 습득 상태만 복원한다. AddNodeServer를 호출하면 이미 저장한 재화를 중복 지급한다.
			bCollected = true;
			InteractableComponent->RestoreUsedState(true);
			OnCollectedStateChanged(true);
			ForceNetUpdate();
		}
	}
}

void AInteractionNode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bProgressIdRegistered)
	{
		if (UOutlierSaveSubSystem* SaveSubsystem = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
			: nullptr)
		{
			SaveSubsystem->UnregisterWorldProgressId(
				EOutlierWorldProgressType::CollectedNode,
				PickupId,
				this);
		}
	}
	Super::EndPlay(EndPlayReason);
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
	UOutlierAudioSubsystem::PlayTaggedAtLocationFromServer(
		this,
		FGameplayTag::RequestGameplayTag(TEXT("Audio.Type.Interactable")),
		FGameplayTag::RequestGameplayTag(TEXT("Audio.Context.Object.Node.Acquire")));

	if (bProgressIdRegistered)
	{
		if (UOutlierSaveSubSystem* SaveSubsystem = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
			: nullptr)
		{
			SaveSubsystem->SetWorldProgressState(
				EOutlierWorldProgressType::CollectedNode,
				PickupId,
				true);
		}
	}
	bCollected = true;
	OnCollectedStateChanged(true);
	ForceNetUpdate();

	/*UE_LOG(LogTemp, Verbose,
		TEXT("[InteractionNode] Shared node reward Player=%s Amount=%d PlayerTotal=%d"),
		*PlayerState->GetPlayerName(),
		NodeRewardAmount,
		PlayerState->GetNodeCount());*/

	return true;
}

void AInteractionNode::OnRep_Collected()
{
	OnCollectedStateChanged(bCollected);
}

void AInteractionNode::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AInteractionNode, bCollected);
}
