// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "GameplayTagContainer.h"
#include "Interface/ActorBasedWidgetInterface.h"
#include "UI/TargetPointHudWidget.h"
#include "InteractableComponent.generated.h"

class AFirstPersonCharacter;
class AInteractionNode;
class AInteractionDescActor;
class APlayerController;
class USceneComponent;
class UInteractInfoWidget;
class UInteractKeyWidget;
class UInteractionLockedWidget;
class UInteractionOutlineWidget;
class UUserWidget;
class UTargetPointHudWidget;

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
class OUTLIER_API UInteractableComponent : public UActorComponent, public IActorBasedWidgetInterface
{
	GENERATED_BODY()

public:
	UInteractableComponent();
	virtual void OnRegister() override;
	virtual void InitializeComponent() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void DismissActorBasedWidgets_Implementation() override;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_InteractableTags, Category = "Interaction")
	FGameplayTagContainer InteractableTags;

	// 같은 Actor의 SceneComponent 위치를 UI 기준으로 사용한다. 위치 Offset은 적용하지 않는다.
	// HackTargetPoint를 선택하면 일반 상호작용 UI와 해킹 UI의 기준점을 공유할 수 있다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|UI",
		meta = (UseComponentPicker, AllowedClasses = "/Script/Engine.SceneComponent"))
	FComponentReference InteractionTargetPointReference;

	UFUNCTION(BlueprintCallable, Category = "Interaction|UI")
	void RefreshInteractionTargetPoint();

	// 기준점이 유효하지 않으면 Actor 원점을 사용한다.
	UFUNCTION(BlueprintPure, Category = "Interaction|UI")
	FVector GetInteractionWidgetLocation() const;

	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
	FGameplayTagQuery RequiredInteractorQuery;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
	FGameplayTagContainer BlockedInteractorTags;

	UFUNCTION(BlueprintCallable, Category = "Interaction")
	bool CanInteract(const FGameplayTagContainer& InteractorTags) const;

	/** A hackable owner whose interaction is still locked. Independent of access queries. */
	UFUNCTION(BlueprintPure, Category = "Interaction")
	bool IsLockedYet() const;

	/** Hack lock prompts ignore player access restrictions, but remain hidden when used or disabled. */
	UFUNCTION(BlueprintPure, Category = "Interaction")
	bool CanShowInteractionWidgets(const FGameplayTagContainer& InteractorTags) const;

	/** Outline markers remain visible regardless of player access restrictions. */
	bool CanShowInteractionOutlineWidget() const;

	bool RequiresHoldInteract() const;
	bool AllowsConcurrentHold() const;
	bool IsMultipleUse() const;
	EInteractionFlowResult AdvanceInteractionFlow(AFirstPersonCharacter* Interactor);

	bool BeginHoldInteract(AFirstPersonCharacter* Interactor, int32 AttemptId = 0);
	void EndHoldInteract(AFirstPersonCharacter* Interactor, bool bCanceled);
	void ResetHoldInteraction(AFirstPersonCharacter* Interactor);
	/** Server-only cancellation of preparation; never interrupts an active hold. */
	bool ResetHoldReadyIfIdle();
	bool CanCommitHoldInteraction(const AFirstPersonCharacter* Interactor) const;
	bool CommitHoldInteraction(AFirstPersonCharacter* Interactor);
	/** Accepted interaction completion on the server; single-use policy is shared by instant and hold interactions. */
	void CommitInteraction(AFirstPersonCharacter* Interactor);

	void SyncInteractionStateFromServer(
		EInteractionFlowResult FlowResult);
	void HandleInteractionSucceeded(
		AFirstPersonCharacter* Interactor,
		EInteractionFlowResult FlowResult,
		bool bSyncStateFromServer);
	void MarkHoldReady();
	void RestoreUsedState(bool bUsed);

	void ActivateInteractionDesc(AFirstPersonCharacter* Interactor);
	void DeactivateInteractionDesc();
	void InteractInfoWidgetActivate(AFirstPersonCharacter* Interactor);
	void InteractInfoWidgetDeactivate();

	void InteractKeyWidgetActivate(AFirstPersonCharacter* Interactor);
	void InteractKeyWidgetDeactivate();
	void UpdateInnerRangeWidgets(AFirstPersonCharacter* Interactor, bool bFocused);
	void InteractionLockedWidgetActivate(AFirstPersonCharacter* Interactor);
	void InteractionLockedWidgetDeactivate();
	void InteractionOutlineWidgetActivate(AFirstPersonCharacter* Interactor);
	void InteractionOutlineWidgetDeactivate();

	/** Local outline-sphere enter/leave hook. Presentation is owned by this component. */
	UFUNCTION(BlueprintNativeEvent, Category = "Interaction|Outline")
	void OnOutlineRangeChanged(AFirstPersonCharacter* Interactor, bool bInRange);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Widget")
	FVector2D InteractKeyWidgetDrawSize = FVector2D(64.0f, 64.0f);

private:
	friend class AInteractionNode;
	friend class FInteractionNodePresentationTest;
	friend class FInteractionNodeHoldAttemptTest;
	friend class FInteractionTargetPointTest;
	UPROPERTY(Transient)
	TObjectPtr<AInteractionNode> InteractionNode;

	void UpdateWidgetPresentation();
	TWeakObjectPtr<USceneComponent> CachedInteractionTargetPoint;
	void RefreshWidgetTick();
	bool MeetsInteractionRequirements(const FGameplayTagContainer& InteractorTags) const;

	UFUNCTION()
	void OnRep_InteractableTags();

	int32 FindHoldSessionIndex(const AFirstPersonCharacter* Interactor) const;
	void TickHoldInteractions(float DeltaTime);
	void CancelOtherHoldSessions(AFirstPersonCharacter* CompletedInteractor);
	void CancelAllHoldSessions(bool bCanceled = true);
	void MarkUsed(AFirstPersonCharacter* CompletedInteractor);
	void ClearHoldReady();

	AInteractionDescActor* EnsureInteractionDescActor();
	void ClearInteractionDescActor();
	UUserWidget* EnsureInteractInfoWidget(APlayerController* PlayerController);
	UUserWidget* EnsureInteractKeyWidget(APlayerController* PlayerController);
	UUserWidget* CreateHudWidget(APlayerController* PlayerController, TSubclassOf<UUserWidget> WidgetClass);
	bool GetPrimaryInteractTag(FGameplayTag& OutInteractTag) const;

	/** Concurrent is a separate policy from access queries and multiple use. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|Hold", meta = (AllowPrivateAccess = "true", ClampMin = "0.01"))
	float HoldDuration = 1.5f;

	static constexpr int32 MaxConcurrentHoldSessions = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|Desc", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<AInteractionDescActor> InteractionDescActorClass;


	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|UI", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UInteractInfoWidget> InteractInfoWidgetClass;


	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|UI", meta = (AllowPrivateAccess = "true", ClampMin = "1.0"))
	FVector2D InteractInfoWidgetDrawSize = FVector2D(1500.0f, 400.0f);

	/** 소유 BP의 컴포넌트에서 지정한다. 미지정 시 키 위젯을 생성하지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|UI", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UInteractKeyWidget> InteractKeyWidgetClass;


	/** 소유 BP의 컴포넌트에서 지정한다. 미지정 시 잠금 위젯을 생성하지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|UI", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UInteractionLockedWidget> InteractionLockedWidgetClass;

	UPROPERTY(EditAnywhere, Category = "Interaction|UI", meta = (ClampMin = "1.0"))
	FVector2D InteractionLockedWidgetDrawSize = FVector2D(64.0f, 64.0f);


	/** 소유 BP의 컴포넌트에서 지정한다. 미지정 시 외곽 위젯을 생성하지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|UI", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UInteractionOutlineWidget> InteractionOutlineWidgetClass;

	UPROPERTY(EditAnywhere, Category = "Interaction|UI", meta = (ClampMin = "1.0"))
	FVector2D InteractionOutlineWidgetDrawSize = FVector2D(32.0f, 32.0f);


	UPROPERTY(Transient)
	TObjectPtr<UInteractionLockedWidget> InteractionLockedWidget;
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> InteractionLockedHudWidget;
	UPROPERTY(Transient)
	TObjectPtr<UInteractionOutlineWidget> InteractionOutlineWidget;
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> InteractionOutlineHudWidget;

	UPROPERTY(Transient)
	TObjectPtr<AInteractionDescActor> InteractionDescActor;

	UPROPERTY(Transient)
	TObjectPtr<UInteractInfoWidget> InteractInfoWidget;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> InteractInfoHudWidget;

	UPROPERTY(Transient)
	TObjectPtr<UInteractKeyWidget> InteractKeyWidget;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> InteractKeyHudWidget;

	UPROPERTY(Transient)
	TObjectPtr<UTargetPointHudWidget> HudLayer;
	bool bInfoShown = false;
	bool bKeyShown = false;
	bool bLockedShown = false;
	bool bOutlineShown = false;

	TArray<FHoldInteractionSession> HoldSessions;
};
