#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interface/InteractableInterface.h"
#include "InteractionNode.generated.h"

class AFirstPersonCharacter;
class UInteractableComponent;
class USceneComponent;

UCLASS()
class OUTLIER_API AInteractionNode : public AActor, public IInteractableInterface
{
	GENERATED_BODY()

public:
	AInteractionNode();

	virtual UInteractableComponent* GetInteractableComponent() const override;
	virtual bool Interact(AFirstPersonCharacter* Interactor) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "Node")
	void OnCollectedStateChanged(bool bCollectedState);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<UInteractableComponent> InteractableComponent;

private:
	bool AddNodeServer(AFirstPersonCharacter* Interactor);
	bool bProgressIdRegistered = false;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Node", meta = (AllowPrivateAccess = "true"))
	FName PickupId = NAME_None;

	UPROPERTY(ReplicatedUsing = OnRep_Collected, VisibleInstanceOnly, BlueprintReadOnly, Category = "Node", meta = (AllowPrivateAccess = "true"))
	bool bCollected = false;

	UFUNCTION()
	void OnRep_Collected();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Node", meta = (AllowPrivateAccess = "true", ClampMin = "1"))
	int32 NodeRewardAmount = 4;
};
