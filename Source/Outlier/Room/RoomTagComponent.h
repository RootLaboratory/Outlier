// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "RoomTagComponent.generated.h"

class ARoomVolume;

// 현재 방이 실제로 변경됐을 때만 이전/새 RoomTag를 전달한다.
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnCurrentRoomTagChanged, FGameplayTag, FGameplayTag);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class OUTLIER_API URoomTagComponent : public UActorComponent
{
	GENERATED_BODY()

protected:
	void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RoomTag")
	FGameplayTag CurrentRoomTag;

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "RoomTag")
	FGameplayTag DefaultRoomTag;

public:
	void EnterRoom(ARoomVolume* Room);
	void LeaveRoom(ARoomVolume* Room);

	FGameplayTag GetCurrentRoomTag() const;
	FGameplayTag GetDefaultRoomTag() const;

	void RefreshCurrentRoom();

	// 수색 슬롯처럼 방에 종속된 런타임 데이터를 정리하는 시스템이 구독한다.
	FOnCurrentRoomTagChanged OnCurrentRoomTagChanged;

private:
	// 겹치는 방이 소수, 순서가 필요함으로 TArray
	// 겹치는 방이 많고 순서가 불필요하면 TSet이 유리함 
	TArray<TWeakObjectPtr<ARoomVolume>> ActiveRooms;
};
