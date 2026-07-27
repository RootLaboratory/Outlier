#include "Interaction/InteractionNode.h"

#include "Components/SceneComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/LocalPlayer.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/InteractionDescActor.h"
#include "Interaction/InteractableComponent.h"
#include "Interaction/InteractInfoSubsystem.h"
#include "UI/InteractInfoWidget.h"

AInteractionNode::AInteractionNode()
{
	PrimaryActorTick.bCanEverTick = true;
	SetActorTickEnabled(false);
	//bCanEverTick->Tick 가능
	//SetActorTickEnabeld ->가능한 상태에서 지금은 boolean flag로

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	InteractableComponent = CreateDefaultSubobject<UInteractableComponent>(TEXT("InteractableComponent"));
}

void AInteractionNode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	TickHoldInteract(DeltaSeconds);
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
		UE_LOG(LogTemp, Warning, TEXT("[InteractionNode] Interact blocked by tags Actor=%s InteractorTags=%s InteractableTags=%s"), *GetName(), *InteractorTags.ToStringSimple(), *InteractableComponent->InteractableTags.ToStringSimple());
		return;
	}

	if (AInteractionDescActor* DescActor = EnsureInteractionDescActor())
	{
		//Node는 즉발 Interaction 후, Tag를 받아 Holding 이벤트를 받게 되어 있음.
		if (RequiresHoldInteract())
		{
			//Holding 이벤트 후, 서버 Interact를 받은 다음. 사용 처리.
			MarkUsed();
			ClearInteractionDescActor();
			return;
		}

		DescActor->SetActorLocation(GetActorLocation() + FVector::UpVector * InteractionDescActorZOffset);
		DescActor->SetSourceInteractionNode(this);
		UE_LOG(LogTemp, Error, TEXT("Interact"));
		DescActor->ActivateDescFromSource(Interactor, InteractableComponent);

		DescActor->PopupAnimationCall(false);

		MarkHoldReady();
		return;
	}

	InteractInfoWidgetActivate(Interactor);
}

bool AInteractionNode::RequiresHoldInteract() const
{
	if (!InteractableComponent)
	{
		return false;
	}

	const FGameplayTag HoldTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Interact.Type.Hold")), false);
	return HoldTag.IsValid() && InteractableComponent->InteractableTags.HasTagExact(HoldTag);
}

void AInteractionNode::BeginHoldInteract(AFirstPersonCharacter* Interactor)
{
	if (!Interactor)
	{
		return;
	}

	HoldingInteractor = Interactor;
	HoldElapsed = 0.0f;
	bIsHoldingInteract = true;

	if (AInteractionDescActor* DescActor = EnsureInteractionDescActor())
	{
		DescActor->SetActorLocation(GetActorLocation() + FVector::UpVector * InteractionDescActorZOffset);
		DescActor->SetSourceInteractionNode(this);
		DescActor->ActivateDescFromSource(Interactor, InteractableComponent);
		DescActor->SetProgress(0.0f);
	}

	SetActorTickEnabled(true);
}

void AInteractionNode::EndHoldInteract(AFirstPersonCharacter* Interactor, bool bCanceled)
{
	if (HoldingInteractor.IsValid() && HoldingInteractor.Get() != Interactor)
	{
		return;
	}

	bIsHoldingInteract = false;
	HoldingInteractor.Reset();
	SetActorTickEnabled(false);

	if (bCanceled)
	{
		HoldElapsed = 0.0f;
		ClearHoldReady();
		ClearInteractionDescActor();
	}
}

AInteractionDescActor* AInteractionNode::EnsureInteractionDescActor()
{
	if (IsValid(InteractionDescActor))
	{
		return InteractionDescActor;
	}

	if (!InteractionDescActorClass)
	{
		return nullptr;
	}

	if (!GetWorld())
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;

	InteractionDescActor = GetWorld()->SpawnActor<AInteractionDescActor>(
		InteractionDescActorClass,
		GetActorLocation() + FVector::UpVector * InteractionDescActorZOffset,
		GetActorRotation(),
		SpawnParams
	);

	if (InteractionDescActor)
	{
		InteractionDescActor->SetSourceInteractionNode(this);
	}

	return InteractionDescActor;
}

void AInteractionNode::TickHoldInteract(float DeltaSeconds)
{
	if (!bIsHoldingInteract || HoldDuration <= 0.0f)
	{
		return;
	}

	HoldElapsed += DeltaSeconds;

	const float HoldProgress = FMath::Clamp(HoldElapsed / HoldDuration, 0.0f, 1.0f);
	if (IsValid(InteractionDescActor))
	{
		InteractionDescActor->SetProgress(HoldProgress);
	}

	if (HoldProgress < 1.0f)
	{
		return;
	}

	bIsHoldingInteract = false;
	SetActorTickEnabled(false);

	if (AFirstPersonCharacter* Interactor = HoldingInteractor.Get())
	{
		Interactor->NotifyHoldInteractCompleted(this);
	}
}

void AInteractionNode::MarkHoldReady()
{
	if (!InteractableComponent)
	{
		return;
	}

	const FGameplayTag HoldTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Interact.Type.Hold")), false);
	if (HoldTag.IsValid())
	{
		InteractableComponent->InteractableTags.AddTag(HoldTag);
	}
}

void AInteractionNode::ClearHoldReady()
{
	if (!InteractableComponent)
	{
		return;
	}

	const FGameplayTag HoldTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Interact.Type.Hold")), false);
	if (HoldTag.IsValid())
	{
		InteractableComponent->InteractableTags.RemoveTag(HoldTag);
	}
}

void AInteractionNode::MarkUsed()
{
	if (!InteractableComponent)
	{
		return;
	}

	const FGameplayTag UsedTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Interact.State.Used")), false);
	const FGameplayTag AbledTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Interact.State.Abled")), false);
	const FGameplayTag UnusedTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Interact.State.Unused")), false);
	const FGameplayTag HoldTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Interact.Type.Hold")), false);

	if (UsedTag.IsValid())
	{
		InteractableComponent->InteractableTags.AddTag(UsedTag);
	}

	if (AbledTag.IsValid())
	{
		InteractableComponent->InteractableTags.RemoveTag(AbledTag);
	}

	if (UnusedTag.IsValid())
	{
		InteractableComponent->InteractableTags.RemoveTag(UnusedTag);
	}

	if (HoldTag.IsValid())
	{
		InteractableComponent->InteractableTags.RemoveTag(HoldTag);
	}
}

void AInteractionNode::ClearInteractionDescActor()
{
	if (IsValid(InteractionDescActor))
	{
		InteractionDescActor->Destroy();
	}

	InteractionDescActor = nullptr;
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
