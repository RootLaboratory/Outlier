// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "RoomVolume.generated.h"

class UBoxComponent;
class URoomCombatDefinition;

UCLASS()
class OUTLIER_API ARoomVolume : public AActor
{
	GENERATED_BODY()
	
public:
	ARoomVolume();

	FGameplayTag GetRoomTag() const { return RoomTag; }
	URoomCombatDefinition* GetCombatDefinition() const { return CombatDefinition; }

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

	UPROPERTY(EditInstanceOnly, Category = "Room|Combat")
	TObjectPtr<URoomCombatDefinition> CombatDefinition;

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
