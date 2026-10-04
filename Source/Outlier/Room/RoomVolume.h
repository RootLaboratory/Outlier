// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "RoomVolume.generated.h"

class UBoxComponent;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnRoomActorOverlapChanged, AActor*, bool /*bEntered*/);

UCLASS()
class OUTLIER_API ARoomVolume : public AActor
{
	GENERATED_BODY()
	
public:
	ARoomVolume();

	FGameplayTag GetRoomTag() const { return RoomTag; }
	bool ContainsWorldLocation(const FVector& Location) const;
	// 스트리밍이 BeginOverlap 알림 없이 만든 기존 overlap에서도 RoomTag를 복구한다.
	void RefreshOverlappingRoomAssignments();
	FOnRoomActorOverlapChanged OnRoomActorOverlapChanged;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> TriggerBox;

	UPROPERTY(EditInstanceOnly, Category = "Room", meta = (Categories = "Room"))
	FGameplayTag RoomTag;

	UFUNCTION()
	void HandleBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void HandleEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex
	);
};
