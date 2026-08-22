#pragma once

#include "CoreMinimal.h"
#include "Components/TimelineComponent.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "InteractableDoor.generated.h"

class UStaticMeshComponent;
class UCurveFloat;

UCLASS()
class OUTLIER_API AInteractableDoor : public AActor
{
	GENERATED_BODY()

public:
	AInteractableDoor();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

public:
	UFUNCTION(BlueprintCallable, Category = "Door")
	void SetDoorOpen(bool bOpen);

	UFUNCTION(BlueprintCallable, Category = "Door")
	void ToggleDoor();

	/** Submits the configured server-authoritative relevant world sound. */
	UFUNCTION(BlueprintCallable, Category = "Door|Audio")
	bool PlayDoorMovementAudio();

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<UStaticMeshComponent> DoorMeshLeft;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<UStaticMeshComponent> DoorMeshRight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door")
	FVector OpenOffsetLeft = FVector(-120.0f, 0.f, 0.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door")
	FVector OpenOffsetRight = FVector(120.0f, 0.f, 0.f);

	UPROPERTY(EditAnywhere, Category = "Door")
	TObjectPtr<UCurveFloat> DoorCurve;

	/** Played as server-authoritative Relevant AtLocation audio when movement starts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Audio", meta = (Categories = "Audio.Event"))
	FGameplayTag DoorMovementAudioEventTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Audio", meta = (Categories = "Audio.Context"))
	FGameplayTagContainer DoorMovementAudioContextTags;

protected:
	UPROPERTY(ReplicatedUsing = OnRep_IsOpen, BlueprintReadOnly, Category = "Door")
	bool bIsOpen = false;

private:
	FTimeline DoorTimeline;
	FVector ClosedLocationLeft = FVector::ZeroVector;
	FVector ClosedLocationRight = FVector::ZeroVector;

	UFUNCTION()
	void OnDoorTimelineUpdate(float Alpha);

	UFUNCTION()
	void OnDoorTimelineFinished();

	UFUNCTION()
	void OnRep_IsOpen();

	void ApplyDoorState(bool bOpen);

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_SetDoorState(bool bOpen);
};
