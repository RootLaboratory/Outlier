// Fill out your copyright notice in the Description page of Project Settings.


#include "Room/RoomTagComponent.h"
#include "Room/RoomVolume.h"

void URoomTagComponent::BeginPlay()
{
	Super::BeginPlay();
	RefreshCurrentRoom();
}

void URoomTagComponent::EnterRoom(ARoomVolume* Room)
{
	if (!IsValid(Room))
	{
		return;
	}

	ActiveRooms.Remove(Room);
	ActiveRooms.Add(Room);

	RefreshCurrentRoom();
}

void URoomTagComponent::LeaveRoom(ARoomVolume* Room)
{
	ActiveRooms.Remove(Room);
	RefreshCurrentRoom();
}

FGameplayTag URoomTagComponent::GetCurrentRoomTag() const
{
	return CurrentRoomTag;
}

FGameplayTag URoomTagComponent::GetDefaultRoomTag() const
{
	return DefaultRoomTag;
}

void URoomTagComponent::RefreshCurrentRoom()
{
	ActiveRooms.RemoveAll(
		[](const TWeakObjectPtr<ARoomVolume>& Room)
		{
			return !Room.IsValid();
		}
	);

	const FGameplayTag PreviousRoomTag = CurrentRoomTag;
	CurrentRoomTag = ActiveRooms.IsEmpty() ? DefaultRoomTag : ActiveRooms.Last()->GetRoomTag();

	if (CurrentRoomTag != PreviousRoomTag)
	{
		OnCurrentRoomTagChanged.Broadcast(PreviousRoomTag, CurrentRoomTag);
	}
}
