// Fill out your copyright notice in the Description page of Project Settings.


#include "Room/RoomTagComponent.h"
#include "Room/RoomVolume.h"

bool URoomTagComponent::IsValidChildRoomTag(FGameplayTag RoomTag)
{
	return GetRoomGroupTag(RoomTag).IsValid();
}

FGameplayTag URoomTagComponent::GetRoomGroupTag(FGameplayTag RoomTag)
{
	const FGameplayTag GroupTag = RoomTag.RequestDirectParent();
	const FGameplayTag LevelTag = GroupTag.RequestDirectParent();
	const FGameplayTag RootTag = LevelTag.RequestDirectParent();
	static const FName RoomRootName(TEXT("Room"));

	// 세 번 올라간 태그가 정확히 Room이어야 네 단계다. 기존 세 단계 태그나
	// 더 깊은 태그를 임의의 그룹으로 해석하지 않아 콘텐츠 이전 전 오판을 막는다.
	return RootTag.GetTagName() == RoomRootName ? GroupTag : FGameplayTag();
}

bool URoomTagComponent::AreRoomsInSameGroup(FGameplayTag FirstRoomTag, FGameplayTag SecondRoomTag)
{
	const FGameplayTag FirstGroupTag = GetRoomGroupTag(FirstRoomTag);
	const FGameplayTag SecondGroupTag = GetRoomGroupTag(SecondRoomTag);

	// 둘 다 잘못된 형식이면 빈 태그끼리 같아지므로 유효성도 함께 검사한다.
	return FirstGroupTag.IsValid() && FirstGroupTag == SecondGroupTag;
}

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

bool URoomTagComponent::HasActiveRoom(const ARoomVolume* Room) const
{
	return ActiveRooms.ContainsByPredicate([Room](const TWeakObjectPtr<ARoomVolume>& ActiveRoom)
	{
		return ActiveRoom.IsValid() && ActiveRoom.Get() == Room;
	});
}

void URoomTagComponent::AssignDefaultRoomTag(FGameplayTag InRoomTag)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	DefaultRoomTag = InRoomTag;
	RefreshCurrentRoom();
}

void URoomTagComponent::ClearRuntimeRoomAssignment()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	// Pool로 돌아간 Actor가 이전 위치의 Overlap과 전투 귀속을 다음 대여에 남기지 않는다.
	ActiveRooms.Reset();
	DefaultRoomTag = FGameplayTag();
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
