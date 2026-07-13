// Fill out your copyright notice in the Description page of Project Settings.

#include "Interaction/InteractableComponent.h"

#include "Components/WidgetComponent.h"
#include "Engine/LocalPlayer.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "GameFramework/Actor.h"
#include "UI/InteractKeyWidget.h"

UInteractableComponent::UInteractableComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UInteractableComponent::CanInteract(const FGameplayTagContainer& InteractorTags) const
{
	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[InteractDebug] InteractorTags=%s BlockedTags=%s RequiredQueryEmpty=%d"),
		*InteractorTags.ToStringSimple(),
		*BlockedInteractorTags.ToStringSimple(),
		RequiredInteractorQuery.IsEmpty() ? 1 : 0
	);

	if (!RequiredInteractorQuery.IsEmpty()
		&& !RequiredInteractorQuery.Matches(InteractorTags))
	{
		UE_LOG(LogTemp, Warning, TEXT("[InteractDebug] Blocked: required query failed"));
		return false;
	}

	if (BlockedInteractorTags.Num() > 0
		&& InteractorTags.HasAny(BlockedInteractorTags))
	{
		UE_LOG(LogTemp, Warning, TEXT("[InteractDebug] Blocked: blocked tag matched"));
		return false;
	}

	return true;
}

UWidgetComponent* UInteractableComponent::EnsureInteractKeyWidgetComponent(APlayerController* PlayerController)
{
	if (InteractKeyWidgetComponent)
	{
		return InteractKeyWidgetComponent;
	}

	AActor* Owner = GetOwner();
	if (!Owner || !PlayerController || !InteractKeyWidgetClass)
	{
		return nullptr;
	}

	UWidgetComponent* NewWidgetComponent = NewObject<UWidgetComponent>(Owner, TEXT("InteractKeyWidgetComponent"));
	if (!NewWidgetComponent)
	{
		return nullptr;
	}

	NewWidgetComponent->SetWidgetClass(InteractKeyWidgetClass);
	NewWidgetComponent->SetWidgetSpace(EWidgetSpace::Screen);
	NewWidgetComponent->SetDrawSize(FVector2D(64.0f, 64.0f));
	NewWidgetComponent->SetDrawAtDesiredSize(false);
	NewWidgetComponent->SetRelativeLocation(FVector(0.0f, 0.0f, InteractKeyWidgetZOffset));
	NewWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NewWidgetComponent->SetGenerateOverlapEvents(false);
	NewWidgetComponent->SetVisibility(false);

	if (ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
	{
		NewWidgetComponent->SetOwnerPlayer(LocalPlayer);
	}

	if (USceneComponent* RootComponent = Owner->GetRootComponent())
	{
		NewWidgetComponent->SetupAttachment(RootComponent);
	}

	Owner->AddInstanceComponent(NewWidgetComponent);
	NewWidgetComponent->RegisterComponent();
	NewWidgetComponent->InitWidget();

	InteractKeyWidgetComponent = NewWidgetComponent;
	InteractKeyWidget = Cast<UInteractKeyWidget>(NewWidgetComponent->GetUserWidgetObject());

	return InteractKeyWidgetComponent;
}

void UInteractableComponent::InteractKeyWidgetActivate(AFirstPersonCharacter* Interactor)
{
	if (!Interactor || !Interactor->IsLocallyControlled())
	{
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(Interactor->GetController());
	if (!PlayerController || !InteractKeyWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("InteractKeyWidgetActivate failed: InteractKeyWidgetClass is null"));

		return;
	}

	UWidgetComponent* WidgetComponent = EnsureInteractKeyWidgetComponent(PlayerController);
	if (!WidgetComponent)
	{
		return;
	}

	WidgetComponent->SetRelativeLocation(FVector(0.0f, 0.0f, InteractKeyWidgetZOffset));
	WidgetComponent->SetVisibility(true);

	WidgetComponent->SetWorldLocation(WidgetComponent->GetAttachParent()? WidgetComponent->GetAttachParent()->GetComponentLocation() + FVector::UpVector * InteractKeyWidgetZOffset
		: GetOwner()->GetActorLocation() + FVector::UpVector * InteractKeyWidgetZOffset
	);

	if (!InteractKeyWidget)
	{
		InteractKeyWidget = Cast<UInteractKeyWidget>(WidgetComponent->GetUserWidgetObject());
	}

	if (InteractKeyWidget)
	{
		InteractKeyWidget->UpdateInteractKey(InteractKeyText);
	}
}

void UInteractableComponent::InteractKeyWidgetDeactivate()
{
	if (InteractKeyWidgetComponent)
	{
		InteractKeyWidgetComponent->SetVisibility(false);
	}

	if (InteractKeyWidget)
	{
		InteractKeyWidget->ClearInteractKey();
	}
}
