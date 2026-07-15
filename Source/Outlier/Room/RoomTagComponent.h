// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "RoomTagComponent.generated.h"

class ARoomVolume;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class OUTLIER_API URoomTagComponent : public UActorComponent
{
	GENERATED_BODY()

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RoomTag")
	FGameplayTag CurrentRoomTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomTag")
	FGameplayTag DefaultRoomTag;

public:
	void EnterRoom(ARoomVolume* Room);
	void LeaveRoom(ARoomVolume* Room);

	FGameplayTag GetCurrentRoomTag() const;
	FGameplayTag GetDefaultRoomTag() const;

	void RefreshCurrentRoom();

private:
	// 겹치는 방이 소수, 순서가 필요함으로 TArray
	// 겹치는 방이 많고 순서가 불필요하면 TSet이 유리함 
	TArray<TWeakObjectPtr<ARoomVolume>> ActiveRooms;
};
