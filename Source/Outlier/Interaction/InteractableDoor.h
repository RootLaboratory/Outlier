#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interface/InteractableInterface.h"
#include "Components/TimelineComponent.h"
#include "InteractableDoor.generated.h"

class UInteractableComponent;
class UStaticMeshComponent;
class UCurveFloat;
class AFirstPersonCharacter;

UCLASS()
class OUTLIER_API AInteractableDoor : public AActor, public IInteractableInterface
{
	GENERATED_BODY()

public:
	AInteractableDoor();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

public:
	virtual UInteractableComponent* GetInteractableComponent() const override;
	virtual void Interact(AFirstPersonCharacter* Interactor) override;

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<UInteractableComponent> InteractableComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<UStaticMeshComponent> DoorMeshLeft;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<UStaticMeshComponent> DoorMeshRight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door")
	FVector OpenOffsetLeft = FVector(-120.f, 0.f, 0.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door")
	FVector OpenOffsetRight = FVector(120.f, 0.f, 0.f);

	UPROPERTY(EditAnywhere, Category = "Door")
	TObjectPtr<UCurveFloat> DoorCurve;

protected:
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Door")
	bool bIsOpen = false;

private:
	FTimeline DoorTimeline;

	UFUNCTION()
	void OnDoorTimelineUpdate(float Alpha);

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_SetDoorState(bool bOpen);
};
