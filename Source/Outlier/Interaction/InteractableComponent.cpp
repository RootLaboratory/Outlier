// Fill out your copyright notice in the Description page of Project Settings.

#include "Interaction/InteractableComponent.h"

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
