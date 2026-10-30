#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Room/RoomTagComponent.h"

namespace
{
	// 테스트 태그는 개발 빌드에서만 등록하며 실제 맵의 태그 이전과 분리한다.
	UE_DEFINE_GAMEPLAY_TAG_STATIC(RoomA, "Room.GroupTestLevel01.1.1");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(RoomB, "Room.GroupTestLevel01.1.2");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(OtherGroupRoom, "Room.GroupTestLevel01.2.1");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(PrefixGroupRoom, "Room.GroupTestLevel01.10.1");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(OtherLevelRoom, "Room.GroupTestLevel02.1.1");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TooDeepRoom, "Room.GroupTestLevel01.1.1.Extra");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(OtherRootRoom, "RoomGroupTest.OtherLevel.1.1");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRoomTagGroupIdentityTest,
	"Outlier.Room.Tags.GroupIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomTagGroupIdentityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestTrue(TEXT("A four-segment Room is a valid child"),
		URoomTagComponent::IsValidChildRoomTag(RoomA));
	TestEqual(TEXT("The group key is the exact direct parent"),
		URoomTagComponent::GetRoomGroupTag(RoomA), RoomA.GetTag().RequestDirectParent());
	TestTrue(TEXT("The same valid child belongs to the same group"),
		URoomTagComponent::AreRoomsInSameGroup(RoomA, RoomA));
	TestTrue(TEXT("Sibling children belong to the same group"),
		URoomTagComponent::AreRoomsInSameGroup(RoomA, RoomB));
	TestTrue(TEXT("Group comparison is symmetric"),
		URoomTagComponent::AreRoomsInSameGroup(RoomB, RoomA));
	TestFalse(TEXT("Different group segments remain separate"),
		URoomTagComponent::AreRoomsInSameGroup(RoomA, OtherGroupRoom));
	TestFalse(TEXT("Group 1 is different from group 10"),
		URoomTagComponent::AreRoomsInSameGroup(RoomA, PrefixGroupRoom));
	TestFalse(TEXT("Different levels remain separate"),
		URoomTagComponent::AreRoomsInSameGroup(RoomA, OtherLevelRoom));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRoomTagInvalidGroupTest,
	"Outlier.Room.Tags.InvalidFormat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomTagInvalidGroupTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FGameplayTag GroupTag = RoomA.GetTag().RequestDirectParent();
	const FGameplayTag LevelTag = GroupTag.RequestDirectParent();
	const FGameplayTag InvalidRooms[] =
	{
		FGameplayTag(),
		LevelTag.RequestDirectParent(),
		LevelTag,
		GroupTag,
		TooDeepRoom,
		OtherRootRoom,
		FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")))
	};

	for (const FGameplayTag InvalidRoom : InvalidRooms)
	{
		const FString Label = FString::Printf(TEXT("Invalid child [%s]"), *InvalidRoom.ToString());
		TestFalse(Label + TEXT(" fails format validation"),
			URoomTagComponent::IsValidChildRoomTag(InvalidRoom));
		TestFalse(Label + TEXT(" has no group key"),
			URoomTagComponent::GetRoomGroupTag(InvalidRoom).IsValid());
		TestFalse(Label + TEXT(" cannot group with a valid child"),
			URoomTagComponent::AreRoomsInSameGroup(InvalidRoom, RoomA));
		TestFalse(Label + TEXT(" cannot group with itself"),
			URoomTagComponent::AreRoomsInSameGroup(InvalidRoom, InvalidRoom));
	}
	TestFalse(TEXT("Two different invalid formats cannot form a group"),
		URoomTagComponent::AreRoomsInSameGroup(GroupTag, TooDeepRoom));
	return true;
}

#endif
