// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "InteractableComponent.generated.h"

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class OUTLIER_API UInteractableComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInteractableComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
	FGameplayTagContainer InteractableTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
	FGameplayTagQuery RequiredInteractorQuery;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
	FGameplayTagContainer BlockedInteractorTags;

	UFUNCTION(BlueprintCallable, Category = "Interaction")
	bool CanInteract(const FGameplayTagContainer& InteractorTags) const;
};
