#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Interface/InteractableInterface.h"
#include "InteractionNode.generated.h"

class AFirstPersonCharacter;
class AInteractionDescActor;
class APlayerController;
class UInteractableComponent;
class UInteractInfoWidget;
class USceneComponent;
class UWidgetComponent;

UCLASS()
class OUTLIER_API AInteractionNode : public AActor, public IInteractableInterface
{
	GENERATED_BODY()

public:
	AInteractionNode();

	virtual void Tick(float DeltaSeconds) override;

	virtual UInteractableComponent* GetInteractableComponent() const override;
	virtual void Interact(AFirstPersonCharacter* Interactor) override;
	virtual bool RequiresHoldInteract() const override;
	virtual void BeginHoldInteract(AFirstPersonCharacter* Interactor) override;
	virtual void EndHoldInteract(AFirstPersonCharacter* Interactor, bool bCanceled) override;
	void ResetHoldInteraction(AFirstPersonCharacter* Interactor);
	void SyncInteractionStateFromServer(bool bCompletedHoldInteract);
	void ActivateInteractionDesc(AFirstPersonCharacter* Interactor);
	void DeactivateInteractionDesc();

	void InteractInfoWidgetActivate(AFirstPersonCharacter* Interactor);
	void InteractInfoWidgetDeactivate();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<UInteractableComponent> InteractableComponent;

private:
	AInteractionDescActor* EnsureInteractionDescActor();
	UWidgetComponent* EnsureInteractInfoWidgetComponent(APlayerController* PlayerController);
	void TickHoldInteract(float DeltaSeconds);
	void MarkHoldReady();
	void ClearHoldReady();
	void MarkUsed();
	void ClearInteractionDescActor();
	bool GetPrimaryInteractTag(FGameplayTag& OutInteractTag) const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|Desc", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<AInteractionDescActor> InteractionDescActor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|Desc", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<AInteractionDescActor> InteractionDescActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|Desc", meta = (AllowPrivateAccess = "true"))
	float InteractionDescActorZOffset = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|Hold", meta = (AllowPrivateAccess = "true", ClampMin = "0.01"))
	float HoldDuration = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|UI", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UInteractInfoWidget> InteractInfoWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|UI", meta = (AllowPrivateAccess = "true"))
	float InteractInfoWidgetZOffset = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|UI", meta = (AllowPrivateAccess = "true", ClampMin = "1.0"))
	FVector2D InteractInfoWidgetDrawSize = FVector2D(1500.0f, 400.0f);

	UPROPERTY(Transient)
	TObjectPtr<UInteractInfoWidget> InteractInfoWidget;

	UPROPERTY(Transient)
	TObjectPtr<UWidgetComponent> InteractInfoWidgetComponent;

	UPROPERTY(Transient)
	TWeakObjectPtr<AFirstPersonCharacter> HoldingInteractor;

	float HoldElapsed = 0.0f;
	bool bIsHoldingInteract = false;
};
