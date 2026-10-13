#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Event/OutlierEventTypes.h"
#include "InputTriggers.h"
#include "TutorialComponent.generated.h"

class APawn;
class UEnhancedInputComponent;
class UInputAction;
class UOutlierEventSubsystem;
struct FInputActionInstance;

DECLARE_MULTICAST_DELEGATE_OneParam(FOutlierTutorialInputCompleted, FGameplayTag /*EventTag*/);

USTRUCT()
struct FOutlierTutorialInputWatch
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UInputAction> InputAction;

	UPROPERTY()
	FOutlierEventContext Context;

	UPROPERTY()
	FGameplayTagQuery Condition;

	FGameplayTag EventTag;
	ETriggerEvent TriggerEvent = ETriggerEvent::Triggered;
	uint32 BindingHandle = 0;
	uint32 Revision = 0;
	bool bOnce = true;
	bool bCompleted = false;
	bool bCompletesInputUI = false;
};

/** Binds existing input actions only to emit tutorial events, never gameplay actions. */
UCLASS(Abstract, Blueprintable, ClassGroup = (Tutorial))
class OUTLIER_API UTutorialComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTutorialComponent();

	/** Attaches the role-specific component once, only to a locally controlled pawn. */
	static UTutorialComponent* AttachTutorial(APawn* Pawn);

	/** Console/editor preview compatibility entry: request on the owning controller, server sends to both players. */
	static bool SendTriggerEvent(APawn* Pawn, FGameplayTag EventTag, const FString& Source);

	bool SendTutorialEvent(FGameplayTag EventTag, const FOutlierEventContext& Context);

	/** Trigger entry. Every upper row, input hints included, lasts for its DT Duration. */
	bool ReceiveEvent(FGameplayTag EventTag);

	/** Shows the data-table input hint, then completes it on the selected input event. */
	bool StartInputTutorial(FGameplayTag EventTag, ETriggerEvent TriggerEvent, const FOutlierEventContext& Context);

	/** Observes an action and sends an event; can also be used without displaying a hint. */
	bool WatchInput(UInputAction* InputAction, ETriggerEvent TriggerEvent, FGameplayTag EventTag,
		const FOutlierEventContext& Context, bool bOnce = true);

	/** Runtime state used by input conditions; configure after attaching the component. */
	void SetTutorialTags(const FGameplayTagContainer& Tags);

	/** An empty query accepts any state. Configure after WatchInput/StartInputTutorial. */
	bool SetInputCondition(FGameplayTag EventTag, const FGameplayTagQuery& Condition);

	void ClearInputWatches();

	void RefreshInputBindings();

	FOutlierTutorialInputCompleted OnInputTutorialCompleted;

	virtual void Activate(bool bReset = false) override;
	virtual void Deactivate() override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual bool SupportsOwner(const APawn* Pawn) const PURE_VIRTUAL(UTutorialComponent::SupportsOwner, return false;);

	UPROPERTY(EditAnywhere, Category = "Tutorial")
	FGameplayTagContainer TutorialTags;

private:
	UOutlierEventSubsystem* GetEventSubsystem() const;
	void BindInput(UEnhancedInputComponent* InputComponent);
	void UnbindInput();
	void HandleInput(const FInputActionInstance& Instance, int32 WatchIndex, uint32 Revision);
	bool AddInputWatch(UInputAction* InputAction, ETriggerEvent TriggerEvent, FGameplayTag EventTag,
		const FOutlierEventContext& Context, bool bOnce, bool bCompletesInputUI);

	UPROPERTY(Transient)
	TArray<FOutlierTutorialInputWatch> InputWatches;

	TWeakObjectPtr<UEnhancedInputComponent> BoundInputComponent;
	TWeakObjectPtr<UOutlierEventSubsystem> StartedInputEventSubsystem;
	TArray<FGameplayTag> StartedInputEventTags;
	uint32 NextWatchRevision = 1;
};
