// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "RoomVolume.generated.h"

class UBoxComponent;
class UWorldPartitionStreamingSourceComponent;

UCLASS()
class OUTLIER_API ARoomVolume : public AActor
{
	GENERATED_BODY()
	
public:
	ARoomVolume();

	FGameplayTag GetRoomTag() const { return RoomTag; }
	void SetCombatStreamingSourceEnabled(bool bEnabled);
	bool IsCombatStreamingSourceEnabled() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> TriggerBox;

	// 플레이어가 Room을 벗어나도 진행 중인 전투 셀이 언로드되지 않게 서버에서만 켠다.
	UPROPERTY(VisibleAnywhere, Category = "Room|Combat")
	TObjectPtr<UWorldPartitionStreamingSourceComponent> CombatStreamingSource;

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
