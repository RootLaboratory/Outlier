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
	virtual bool Interact(AFirstPersonCharacter* Interactor) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "Switch")
	void OnSwitchActivated(AFirstPersonCharacter* Interactor);

private:
	UFUNCTION()
	void OnRep_IsActivated();

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_OnSwitchActivated(AFirstPersonCharacter* Interactor);

	void ApplySwitchActivated(AFirstPersonCharacter* Interactor);

	bool bProgressIdRegistered = false;
	bool bActivationEventApplied = false;

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<UStaticMeshComponent> SwitchMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<UInteractableComponent> InteractableComponent;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Switch")
	TObjectPtr<AInteractableDoor> TargetDoor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Switch")
	bool bCanToggleDoor = true;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Switch")
	FName SwitchId = NAME_None;

	UPROPERTY(ReplicatedUsing = OnRep_IsActivated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Switch")
	bool bIsActivated = false;
};
