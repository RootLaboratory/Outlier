// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "InteractableComponent.generated.h"

class AFirstPersonCharacter;
class AInteractionDescActor;
class APlayerController;
class UInteractInfoWidget;
class UInteractKeyWidget;
class UWidgetComponent;

/**
 * One active hold attempt. The interaction supports at most two players, so a
 * small linear array is simpler and safer than maintaining a map.
 */
struct FHoldInteractionSession
{
	TWeakObjectPtr<AFirstPersonCharacter> Interactor;
	float ElapsedTime = 0.0f;
	bool bCompletionNotified = false;
};

UENUM()
enum class EInteractionFlowResult : uint8
{
	Rejected,
	HoldReady,
	Completed
};

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class OUTLIER_API UInteractableComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInteractableComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_InteractableTags, Category = "Interaction")
	FGameplayTagContainer InteractableTags;

	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
	FGameplayTagQuery RequiredInteractorQuery;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
	FGameplayTagContainer BlockedInteractorTags;

	UFUNCTION(BlueprintCallable, Category = "Interaction")
	bool CanInteract(const FGameplayTagContainer& InteractorTags) const;

	bool RequiresHoldInteract() const;
	bool AllowsConcurrentHold() const;
	bool IsMultipleUse() const;
	EInteractionFlowResult AdvanceInteractionFlow(AFirstPersonCharacter* Interactor);

	bool BeginHoldInteract(AFirstPersonCharacter* Interactor);
	void EndHoldInteract(AFirstPersonCharacter* Interactor, bool bCanceled);
	void ResetHoldInteraction(AFirstPersonCharacter* Interactor);
	bool CanCommitHoldInteraction(const AFirstPersonCharacter* Interactor) const;
	bool CommitHoldInteraction(AFirstPersonCharacter* Interactor);

	void SyncInteractionStateFromServer(
		EInteractionFlowResult FlowResult,
		bool bCompletedHoldInteract);
	void HandleInteractionSucceeded(
		AFirstPersonCharacter* Interactor,
		EInteractionFlowResult FlowResult,
		bool bCompletedHoldInteract,
		bool bSyncStateFromServer);
	void MarkHoldReady();

	void ActivateInteractionDesc(AFirstPersonCharacter* Interactor);
	void DeactivateInteractionDesc();
	void InteractInfoWidgetActivate(AFirstPersonCharacter* Interactor);
	void InteractInfoWidgetDeactivate();

	void InteractKeyWidgetActivate(AFirstPersonCharacter* Interactor);
	void InteractKeyWidgetDeactivate();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Widget")
	FVector2D InteractKeyWidgetDrawSize = FVector2D(64.0f, 64.0f);

private:
	UFUNCTION()
	void OnRep_InteractableTags();

	int32 FindHoldSessionIndex(const AFirstPersonCharacter* Interactor) const;
	void TickHoldInteractions(float DeltaTime);
	void CancelOtherHoldSessions(AFirstPersonCharacter* CompletedInteractor);
	void CancelAllHoldSessions();
	void MarkUsed(AFirstPersonCharacter* CompletedInteractor);
	void ClearHoldReady();

	AInteractionDescActor* EnsureInteractionDescActor();
	void ClearInteractionDescActor();
	UWidgetComponent* EnsureInteractInfoWidgetComponent(APlayerController* PlayerController);
	UWidgetComponent* EnsureInteractKeyWidgetComponent(APlayerController* PlayerController);
	bool GetPrimaryInteractTag(FGameplayTag& OutInteractTag) const;

	/** Concurrent is a separate policy from access queries and multiple use. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|Hold", meta = (AllowPrivateAccess = "true", ClampMin = "0.01"))
	float HoldDuration = 1.5f;

	static constexpr int32 MaxConcurrentHoldSessions = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|Desc", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<AInteractionDescActor> InteractionDescActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|Desc", meta = (AllowPrivateAccess = "true"))
	float InteractionDescActorZOffset = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|UI", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UInteractInfoWidget> InteractInfoWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|UI", meta = (AllowPrivateAccess = "true"))
	float InteractInfoWidgetZOffset = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|UI", meta = (AllowPrivateAccess = "true", ClampMin = "1.0"))
	FVector2D InteractInfoWidgetDrawSize = FVector2D(1500.0f, 400.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|UI", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UInteractKeyWidget> InteractKeyWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|UI", meta = (AllowPrivateAccess = "true"))
	FText InteractKeyText = FText::FromString(TEXT("F"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|UI", meta = (AllowPrivateAccess = "true"))
	float InteractKeyWidgetZOffset = 20.0f;

	UPROPERTY(Transient)
	TObjectPtr<AInteractionDescActor> InteractionDescActor;

	UPROPERTY(Transient)
	TObjectPtr<UInteractInfoWidget> InteractInfoWidget;

	UPROPERTY(Transient)
	TObjectPtr<UWidgetComponent> InteractInfoWidgetComponent;

	UPROPERTY(Transient)
	TObjectPtr<UInteractKeyWidget> InteractKeyWidget;

	UPROPERTY(Transient)
	TObjectPtr<UWidgetComponent> InteractKeyWidgetComponent;

	TArray<FHoldInteractionSession> HoldSessions;
};
