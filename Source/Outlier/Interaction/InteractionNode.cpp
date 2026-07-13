#include "Interaction/InteractionNode.h"

#include "Components/SceneComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/LocalPlayer.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/InteractableComponent.h"
#include "Interaction/InteractInfoSubsystem.h"
#include "UI/InteractInfoWidget.h"

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

void AInteractionNode::Interact(AFirstPersonCharacter* Interactor)
{
	if (!Interactor || !InteractableComponent)
	{
		return;
	}

	const FGameplayTagContainer InteractorTags = Interactor->GetOwnedGameplayTagsForQuery();
	if (!InteractableComponent->CanInteract(InteractorTags))
	{
		UE_LOG(LogTemp, Warning, TEXT("[InteractionNode] Interact blocked by tags Actor=%s"), *GetName());
		return;
	}

	InteractInfoWidgetActivate(Interactor);
}

UWidgetComponent* AInteractionNode::EnsureInteractInfoWidgetComponent(APlayerController* PlayerController)
{
	if (InteractInfoWidgetComponent)
	{
		return InteractInfoWidgetComponent;
	}

	if (!PlayerController || !InteractInfoWidgetClass)
	{
		return nullptr;
	}

	UWidgetComponent* NewWidgetComponent = NewObject<UWidgetComponent>(this, TEXT("InteractInfoWidgetComponent"));
	if (!NewWidgetComponent)
	{
		return nullptr;
	}

	NewWidgetComponent->SetWidgetClass(InteractInfoWidgetClass);
	NewWidgetComponent->SetWidgetSpace(EWidgetSpace::Screen);
	NewWidgetComponent->SetDrawSize(InteractInfoWidgetDrawSize);
	NewWidgetComponent->SetDrawAtDesiredSize(false);
	NewWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NewWidgetComponent->SetGenerateOverlapEvents(false);
	NewWidgetComponent->SetVisibility(false);

	if (ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
	{
		NewWidgetComponent->SetOwnerPlayer(LocalPlayer);
	}

	if (USceneComponent* OwnerRootComponent = GetRootComponent())
	{
		NewWidgetComponent->SetupAttachment(OwnerRootComponent);
	}

	AddInstanceComponent(NewWidgetComponent);
	NewWidgetComponent->RegisterComponent();
	NewWidgetComponent->InitWidget();

	InteractInfoWidgetComponent = NewWidgetComponent;
	InteractInfoWidget = Cast<UInteractInfoWidget>(NewWidgetComponent->GetUserWidgetObject());

	return InteractInfoWidgetComponent;
}

bool AInteractionNode::GetPrimaryInteractTag(FGameplayTag& OutInteractTag) const
{
	if (!InteractableComponent)
	{
		return false;
	}

	const FGameplayTag TargetRootTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Interact.Target")), false);
	TArray<FGameplayTag> InteractableTagArray;
	InteractableComponent->InteractableTags.GetGameplayTagArray(InteractableTagArray);

	FGameplayTag FirstValidTag;
	for (const FGameplayTag& Tag : InteractableTagArray)
	{
		if (Tag.IsValid())
		{
			if (!FirstValidTag.IsValid())
			{
				FirstValidTag = Tag;
			}

			if (TargetRootTag.IsValid() && Tag.MatchesTag(TargetRootTag))
			{
				OutInteractTag = Tag;
				return true;
			}
		}
	}

	if (FirstValidTag.IsValid())
	{
		OutInteractTag = FirstValidTag;
		return true;
	}

	return false;
}

void AInteractionNode::InteractInfoWidgetActivate(AFirstPersonCharacter* Interactor)
{
	if (!Interactor || !Interactor->IsLocallyControlled())
	{
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(Interactor->GetController());
	if (!PlayerController || !InteractInfoWidgetClass)
	{
		return;
	}

	UWidgetComponent* WidgetComponent = EnsureInteractInfoWidgetComponent(PlayerController);
	if (!WidgetComponent)
	{
		return;
	}

	FGameplayTag InteractTag;
	FInteractInfoRow InteractInfo;
	const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	const UInteractInfoSubsystem* InteractInfoSubsystem = GameInstance ? GameInstance->GetSubsystem<UInteractInfoSubsystem>() : nullptr;

	if (!GetPrimaryInteractTag(InteractTag)
		|| !InteractInfoSubsystem
		|| !InteractInfoSubsystem->TryGetInteractInfo(InteractTag, InteractInfo))
	{
		return;
	}

	if (InteractableComponent)
	{
		InteractableComponent->InteractKeyWidgetDeactivate();
	}

	WidgetComponent->SetVisibility(true);
	WidgetComponent->SetWorldLocation(
		WidgetComponent->GetAttachParent()
			? WidgetComponent->GetAttachParent()->GetComponentLocation() + FVector::UpVector * InteractInfoWidgetZOffset
			: GetActorLocation() + FVector::UpVector * InteractInfoWidgetZOffset
	);

	if (!InteractInfoWidget)
	{
		InteractInfoWidget = Cast<UInteractInfoWidget>(WidgetComponent->GetUserWidgetObject());
	}

	if (InteractInfoWidget)
	{
		InteractInfoWidget->UpdateInteractInfo(InteractTag, InteractInfo);
	}
}

void AInteractionNode::InteractInfoWidgetDeactivate()
{
	if (InteractInfoWidgetComponent)
	{
		InteractInfoWidgetComponent->SetVisibility(false);
	}

	if (InteractInfoWidget)
	{
		InteractInfoWidget->ClearInteractInfo();
	}
}
