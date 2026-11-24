#if WITH_DEV_AUTOMATION_TESTS

#include "Network/ArenaTransitionPlacement.h"

#include "Misc/AutomationTest.h"
#include "OutlierArenaSettings.h"
#include "Save/OutlierCheckpointSnapshot.h"

namespace
{
FArenaTransitionPlacementBinding MakePlacementTestBinding(FName ActorTag)
{
	FArenaTransitionPlacementBinding Binding;
	FArenaTransitionPlacementTarget& Target = Binding.Targets.AddDefaulted_GetRef();
	Target.LevelPackage = TEXT("/Game/Maps/TestArena");
	Target.ActorTag = ActorTag;
	return Binding;
}

void ExpectPlacementTestTarget(FAutomationTestBase& Test,
	const TArray<FArenaTransitionPlacementTarget>& Targets, FName ExpectedActorTag)
{
	if (Test.TestEqual(TEXT("A single target is selected"), Targets.Num(), 1))
	{
		Test.TestEqual(TEXT("The selected actor tag matches the binding"), Targets[0].ActorTag, ExpectedActorTag);
	}
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierArenaPlacementRestoreRouteTest,
	"Outlier.Network.ArenaPlacement.RestoreRoutes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierArenaPlacementRestoreRouteTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FGameplayTag RoomTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")), false);
	if (!TestTrue(TEXT("The test room tag is registered"), RoomTag.IsValid()))
	{
		return false;
	}

	TArray<FArenaTransitionPlacementBinding> Bindings;
	Bindings.Add_GetRef(MakePlacementTestBinding(TEXT("StartDoor"))).PresetIds = { TEXT("Start") };
	Bindings.Add_GetRef(MakePlacementTestBinding(TEXT("CheckpointDoor"))).CheckpointIds = { TEXT("Tuto") };
	Bindings.Add_GetRef(MakePlacementTestBinding(TEXT("PresetDoor"))).PresetIds = { TEXT("Level3") };
	Bindings.Add_GetRef(MakePlacementTestBinding(TEXT("CombatDoor"))).RoomTags = { RoomTag };

	TArray<FArenaTransitionPlacementTarget> Targets;
	FString Reason;
	FOutlierCheckpointSnapshot Snapshot;
	Snapshot.bInitialSnapshot = true;
	TestTrue(TEXT("The initial snapshot uses the Start preset"),
		OutlierArenaTransitionPlacement::ResolveTargets(Bindings, NAME_None, &Snapshot, Targets, Reason));
	ExpectPlacementTestTarget(*this, Targets, TEXT("StartDoor"));

	Snapshot.bInitialSnapshot = false;
	Snapshot.CheckpointId = TEXT("Tuto");
	Snapshot.RoomTag = RoomTag;
	TestTrue(TEXT("A trigger with a valid room tag still uses its checkpoint ID"),
		OutlierArenaTransitionPlacement::ResolveTargets(Bindings, NAME_None, &Snapshot, Targets, Reason));
	ExpectPlacementTestTarget(*this, Targets, TEXT("CheckpointDoor"));

	TestTrue(TEXT("An explicit preset keeps its own binding"),
		OutlierArenaTransitionPlacement::ResolveTargets(Bindings, TEXT("Level3"), nullptr, Targets, Reason));
	ExpectPlacementTestTarget(*this, Targets, TEXT("PresetDoor"));

	TestTrue(TEXT("The current-position developer reload can have no targets"),
		OutlierArenaTransitionPlacement::ResolveTargets(Bindings, NAME_None, nullptr, Targets, Reason));
	TestTrue(TEXT("The current-position reload clears earlier targets"), Targets.IsEmpty());

	Snapshot.CheckpointId = TEXT("UnmappedTrigger");
	TestFalse(TEXT("An unmapped trigger does not inherit a valid room binding"),
		OutlierArenaTransitionPlacement::ResolveTargets(Bindings, NAME_None, &Snapshot, Targets, Reason));
	TestFalse(TEXT("An unmapped trigger reports a failure reason"), Reason.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierArenaPlacementRoomSelectionTest,
	"Outlier.Network.ArenaPlacement.RoomSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierArenaPlacementRoomSelectionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FGameplayTag RoomTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")), false);
	const FGameplayTag OtherRoomTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.2")), false);
	const FGameplayTag StageTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01")), false);
	const FGameplayTag RootTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Room")), false);
	if (!TestTrue(TEXT("The room and its tag hierarchy are registered"),
		RoomTag.IsValid() && OtherRoomTag.IsValid() && StageTag.IsValid() && RootTag.IsValid()))
	{
		return false;
	}

	TArray<FArenaTransitionPlacementBinding> Bindings;
	Bindings.Add_GetRef(MakePlacementTestBinding(TEXT("AllRooms"))).RoomTags = { RootTag };
	Bindings.Add_GetRef(MakePlacementTestBinding(TEXT("StageDoor"))).RoomTags = { StageTag };
	Bindings.Add_GetRef(MakePlacementTestBinding(TEXT("RoomDoor"))).RoomTags = { RoomTag };
	FArenaTransitionPlacementBinding& ClearedBinding = Bindings.Add_GetRef(MakePlacementTestBinding(TEXT("ExitDoor")));
	ClearedBinding.RoomTags = { RoomTag };
	ClearedBinding.RoomSaveReasons = { EOutlierCheckpointSaveReason::EncounterCleared };

	FOutlierCheckpointSnapshot Snapshot;
	Snapshot.SaveReason = EOutlierCheckpointSaveReason::PhaseTransition;
	Snapshot.CheckpointId = TEXT("Room.Room.Level01.1.Phase.1");
	Snapshot.RoomTag = RoomTag;
	TArray<FArenaTransitionPlacementTarget> Targets;
	FString Reason;
	TestTrue(TEXT("A phase save resolves by its exact room tag"),
		OutlierArenaTransitionPlacement::ResolveTargets(Bindings, NAME_None, &Snapshot, Targets, Reason));
	ExpectPlacementTestTarget(*this, Targets, TEXT("RoomDoor"));

	Snapshot.SaveReason = EOutlierCheckpointSaveReason::EncounterCleared;
	Snapshot.CheckpointId = TEXT("Room.Room.Level01.1.Cleared.-1");
	TestTrue(TEXT("A reason-specific binding takes priority for that room"),
		OutlierArenaTransitionPlacement::ResolveTargets(Bindings, NAME_None, &Snapshot, Targets, Reason));
	ExpectPlacementTestTarget(*this, Targets, TEXT("ExitDoor"));

	Snapshot.RoomTag = OtherRoomTag;
	TestTrue(TEXT("The nearest configured parent is used for another room"),
		OutlierArenaTransitionPlacement::ResolveTargets(Bindings, NAME_None, &Snapshot, Targets, Reason));
	ExpectPlacementTestTarget(*this, Targets, TEXT("StageDoor"));

	Snapshot.RoomTag = StageTag;
	TestTrue(TEXT("A stage tag stored by an older save matches on its own"),
		OutlierArenaTransitionPlacement::ResolveTargets(Bindings, NAME_None, &Snapshot, Targets, Reason));
	ExpectPlacementTestTarget(*this, Targets, TEXT("StageDoor"));

	Bindings.Add_GetRef(MakePlacementTestBinding(TEXT("ConflictingStageDoor"))).RoomTags = { StageTag };
	TestFalse(TEXT("Two equally specific candidates are rejected"),
		OutlierArenaTransitionPlacement::ResolveTargets(Bindings, NAME_None, &Snapshot, Targets, Reason));
	TestTrue(TEXT("Ambiguity clears previous targets"), Targets.IsEmpty());
	TestFalse(TEXT("Ambiguity reports a failure reason"), Reason.IsEmpty());

	Snapshot.RoomTag = RoomTag;
	TestTrue(TEXT("Ambiguity among lower-priority candidates does not hide an exact match"),
		OutlierArenaTransitionPlacement::ResolveTargets(Bindings, NAME_None, &Snapshot, Targets, Reason));
	ExpectPlacementTestTarget(*this, Targets, TEXT("ExitDoor"));

	Bindings.Reset();
	FArenaTransitionPlacementBinding EmptyBinding;
	EmptyBinding.RoomTags = { RoomTag };
	Bindings.Add(EmptyBinding);
	TestTrue(TEXT("An explicitly empty room binding is accepted"),
		OutlierArenaTransitionPlacement::ResolveTargets(Bindings, NAME_None, &Snapshot, Targets, Reason));
	TestTrue(TEXT("The explicit empty binding has no targets"), Targets.IsEmpty());

	Snapshot.RoomTag = OtherRoomTag;
	TestFalse(TEXT("A missing room binding is rejected"),
		OutlierArenaTransitionPlacement::ResolveTargets(Bindings, NAME_None, &Snapshot, Targets, Reason));
	Snapshot.RoomTag = FGameplayTag();
	TestFalse(TEXT("An invalid room tag cannot fall back to another binding"),
		OutlierArenaTransitionPlacement::ResolveTargets(Bindings, NAME_None, &Snapshot, Targets, Reason));
	TestFalse(TEXT("An invalid room tag reports a failure reason"), Reason.IsEmpty());

	Snapshot.RoomTag = RoomTag;
	Bindings[0].Targets.AddDefaulted();
	TestFalse(TEXT("A binding with an invalid target is rejected"),
		OutlierArenaTransitionPlacement::ResolveTargets(Bindings, NAME_None, &Snapshot, Targets, Reason));
	TestTrue(TEXT("An invalid target is not published"), Targets.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierArenaPlacementProductionSaveTest,
	"Outlier.Network.ArenaPlacement.ProductionAutoSaves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierArenaPlacementProductionSaveTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const TArray<FArenaTransitionPlacementBinding>& Bindings =
		GetDefault<UOutlierArenaSettings>()->TransitionPlacementBindings;
	FOutlierCheckpointSnapshot Snapshot;
	Snapshot.SaveReason = EOutlierCheckpointSaveReason::PhaseTransition;
	Snapshot.CheckpointId = TEXT("Room.Room.Level01.Phase.1");
	Snapshot.RoomTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01")), false);
	TArray<FArenaTransitionPlacementTarget> Targets;
	FString Reason;
	TestTrue(TEXT("An older Level01 phase save resolves through the production configuration"),
		OutlierArenaTransitionPlacement::ResolveTargets(Bindings, NAME_None, &Snapshot, Targets, Reason));
	TestTrue(TEXT("Level01 retains the configured door requirement"),
		Targets.ContainsByPredicate([](const FArenaTransitionPlacementTarget& Target)
		{
			return Target.ActorTag == FName(TEXT("ArenaPlacement.Level1.Door"));
		}));

	Snapshot.SaveReason = EOutlierCheckpointSaveReason::EncounterCleared;
	Snapshot.CheckpointId = TEXT("Room.Room.Level03.Cleared.-1");
	Snapshot.RoomTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level03")), false);
	TestTrue(TEXT("An older Level03 clear save resolves through the production configuration"),
		OutlierArenaTransitionPlacement::ResolveTargets(Bindings, NAME_None, &Snapshot, Targets, Reason));
	TestTrue(TEXT("Level03 retains the configured elevator requirement"),
		Targets.ContainsByPredicate([](const FArenaTransitionPlacementTarget& Target)
		{
			return Target.ActorTag == FName(TEXT("ArenaPlacement.Level3.Elevator"));
		}));

	Snapshot.CheckpointId = TEXT("Room.Room.Level02.1.Cleared.-1");
	Snapshot.RoomTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level02.1")), false);
	TestTrue(TEXT("Level02 child rooms inherit the stage definition"),
		OutlierArenaTransitionPlacement::ResolveTargets(Bindings, NAME_None, &Snapshot, Targets, Reason));
	TestTrue(TEXT("Level02 retains the configured combat barrier requirement"),
		Targets.ContainsByPredicate([](const FArenaTransitionPlacementTarget& Target)
		{
			return Target.ActorTag == FName(TEXT("ArenaPlacement.Level2.CombatBarrier"));
		}));

	Snapshot.CheckpointId = TEXT("Room.Room.Level04.1.Cleared.-1");
	Snapshot.RoomTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level04.1")), false);
	TestTrue(TEXT("Level04 child rooms resolve through the explicit empty definition"),
		OutlierArenaTransitionPlacement::ResolveTargets(Bindings, NAME_None, &Snapshot, Targets, Reason));
	TestTrue(TEXT("Level04 room saves have no additional placement targets"), Targets.IsEmpty());
	return true;
}

#endif
