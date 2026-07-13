#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interface/InteractableInterface.h"
#include "InteractableSwitch.generated.h"

class AFirstPersonCharacter;
class AInteractableDoor;
class UInteractableComponent;
class UStaticMeshComponent;

UCLASS()
class OUTLIER_API AInteractableSwitch : public AActor, public IInteractableInterface
{
	GENERATED_BODY()

public:
	AInteractableSwitch();

public:
	virtual UInteractableComponent* GetInteractableComponent() const override;
	virtual void Interact(AFirstPersonCharacter* Interactor) override;

protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "Switch")
	void OnSwitchActivated(AFirstPersonCharacter* Interactor);

private:
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_OnSwitchActivated(AFirstPersonCharacter* Interactor);

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<UStaticMeshComponent> SwitchMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<UInteractableComponent> InteractableComponent;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Switch")
	TObjectPtr<AInteractableDoor> TargetDoor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Switch")
	bool bCanToggleDoor = true;
};
