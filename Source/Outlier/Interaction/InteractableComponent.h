// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "InteractableComponent.generated.h"

class AFirstPersonCharacter;
class APlayerController;
class UInteractKeyWidget;
class UWidgetComponent;
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

	void InteractKeyWidgetActivate(AFirstPersonCharacter* Interactor);
	void InteractKeyWidgetDeactivate();

private:
	UWidgetComponent* EnsureInteractKeyWidgetComponent(APlayerController* PlayerController);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|UI", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UInteractKeyWidget> InteractKeyWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|UI", meta = (AllowPrivateAccess = "true"))
	FText InteractKeyText = FText::FromString(TEXT("F"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|UI", meta = (AllowPrivateAccess = "true"))
	float InteractKeyWidgetZOffset = 20.0f;

	UPROPERTY(Transient)
	TObjectPtr<UInteractKeyWidget> InteractKeyWidget;

	UPROPERTY(Transient)
	TObjectPtr<UWidgetComponent> InteractKeyWidgetComponent;
};
