#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"
#include "Save/OutlierSaveSubSystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierCheckpointRuntimeSnapshotTest,
	"Outlier.Save.Checkpoint.RuntimeSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierCheckpointRuntimeSnapshotTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UOutlierSaveSubSystem* SaveSubsystem = NewObject<UOutlierSaveSubSystem>(GameInstance);
	if (!TestNotNull(TEXT("Runtime save subsystem can be created"), SaveSubsystem))
	{
		return false;
	}

	FOutlierCheckpointSnapshot Initial;
	Initial.bInitialSnapshot = true;
	Initial.ShooterSpawnTransform.SetLocation(FVector(100.0, 200.0, 300.0));
	Initial.WorldProgress.CollectedNodeIds.Add(TEXT("InitialNode"));
	TestTrue(TEXT("The first initial snapshot is accepted"), SaveSubsystem->CaptureInitialSnapshot(Initial));

	FOutlierCheckpointSnapshot ReplacementInitial = Initial;
	ReplacementInitial.ShooterSpawnTransform.SetLocation(FVector(999.0));
	TestFalse(TEXT("The initial snapshot cannot be replaced"), SaveSubsystem->CaptureInitialSnapshot(ReplacementInitial));

	FOutlierCheckpointSnapshot RestoreTarget;
	TestTrue(TEXT("The initial snapshot is selected before a checkpoint commit"), SaveSubsystem->GetRestoreSnapshot(RestoreTarget));
	TestEqual(TEXT("Initial snapshot values are copied"), RestoreTarget.ShooterSpawnTransform.GetLocation(), FVector(100.0, 200.0, 300.0));

	FOutlierCheckpointSnapshot Checkpoint;
	Checkpoint.CheckpointId = TEXT("Checkpoint.A");
	Checkpoint.WorldProgress.CollectedNodeIds.Add(TEXT("SavedNode"));
	TestTrue(TEXT("A valid checkpoint snapshot is committed"), SaveSubsystem->CommitCheckpointSnapshot(Checkpoint));

	FOutlierCheckpointSnapshot Duplicate = Checkpoint;
	Duplicate.WorldProgress.CollectedNodeIds.Add(TEXT("LateNode"));
	TestFalse(TEXT("The same checkpoint cannot be committed twice"), SaveSubsystem->CommitCheckpointSnapshot(Duplicate));
	TestTrue(TEXT("The latest checkpoint is selected after commit"), SaveSubsystem->GetRestoreSnapshot(RestoreTarget));
	TestTrue(TEXT("Committed world progress is preserved"), RestoreTarget.WorldProgress.CollectedNodeIds.Contains(TEXT("SavedNode")));
	TestFalse(TEXT("A rejected duplicate cannot replace the saved snapshot"), RestoreTarget.WorldProgress.CollectedNodeIds.Contains(TEXT("LateNode")));

	FOutlierCheckpointSnapshot LaterCheckpoint;
	LaterCheckpoint.CheckpointId = TEXT("Checkpoint.B");
	LaterCheckpoint.WorldProgress.OpenedDoorIds.Add(TEXT("Door.B"));
	TestTrue(TEXT("A later checkpoint replaces the restore target"), SaveSubsystem->CommitCheckpointSnapshot(LaterCheckpoint));
	TestTrue(TEXT("The newer checkpoint is selected"), SaveSubsystem->GetRestoreSnapshot(RestoreTarget));
	TestEqual(TEXT("The newer checkpoint Id is preserved"), RestoreTarget.CheckpointId, FName(TEXT("Checkpoint.B")));
	TestTrue(TEXT("The newer checkpoint world state is copied"), RestoreTarget.WorldProgress.OpenedDoorIds.Contains(TEXT("Door.B")));

	SaveSubsystem->SetWorldProgressState(EOutlierWorldProgressType::CollectedNode, TEXT("AfterCheckpoint"), true);
	TestTrue(TEXT("Live world progress receives later changes"),
		SaveSubsystem->HasWorldProgress(EOutlierWorldProgressType::CollectedNode, TEXT("AfterCheckpoint")));
	TestTrue(TEXT("The committed snapshot can be read again"), SaveSubsystem->GetRestoreSnapshot(RestoreTarget));
	TestFalse(TEXT("Live progress does not mutate the committed copy"),
		RestoreTarget.WorldProgress.CollectedNodeIds.Contains(TEXT("AfterCheckpoint")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierCheckpointWorldIdTest,
	"Outlier.Save.Checkpoint.WorldProgressIds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierCheckpointWorldIdTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	AddExpectedErrorPlain(
		TEXT("[Checkpoint] Invalid stable Id Kind=WorldProgress.0 Id=None"),
		EAutomationExpectedErrorFlags::Contains,
		1);
	AddExpectedErrorPlain(
		TEXT("[Checkpoint] Duplicate stable Id Kind=WorldProgress.0 Id=Node.A"),
		EAutomationExpectedErrorFlags::Contains,
		1);

	UGameInstance* EmptyIdGameInstance = NewObject<UGameInstance>();
	UOutlierSaveSubSystem* EmptyIdSubsystem =
		NewObject<UOutlierSaveSubSystem>(EmptyIdGameInstance);
	UObject* EmptyIdOwner = EmptyIdGameInstance;

	TestFalse(TEXT("An empty stable Id is rejected"),
		EmptyIdSubsystem->RegisterWorldProgressId(
			EOutlierWorldProgressType::CollectedNode,
			NAME_None,
			EmptyIdOwner));
	FOutlierCheckpointSnapshot InvalidCommit;
	InvalidCommit.CheckpointId = TEXT("Checkpoint.InvalidIds");
	TestFalse(TEXT("An invalid Id prevents direct checkpoint commit"),
		EmptyIdSubsystem->CommitCheckpointSnapshot(InvalidCommit));

	UGameInstance* DuplicateIdGameInstance = NewObject<UGameInstance>();
	UOutlierSaveSubSystem* DuplicateIdSubsystem =
		NewObject<UOutlierSaveSubSystem>(DuplicateIdGameInstance);
	UObject* FirstOwner = DuplicateIdGameInstance;
	UObject* SecondOwner = NewObject<UGameInstance>();
	TestTrue(TEXT("A stable Id can be registered"),
		DuplicateIdSubsystem->RegisterWorldProgressId(
			EOutlierWorldProgressType::CollectedNode,
			TEXT("Node.A"),
			FirstOwner));
	TestFalse(TEXT("A duplicate live stable Id is rejected"),
		DuplicateIdSubsystem->RegisterWorldProgressId(
			EOutlierWorldProgressType::CollectedNode,
			TEXT("Node.A"),
			SecondOwner));
	TestFalse(TEXT("A duplicate Id invalidates checkpoint commits"),
		DuplicateIdSubsystem->HasValidStableIds());

	DuplicateIdSubsystem->UnregisterWorldProgressId(
		EOutlierWorldProgressType::CollectedNode,
		TEXT("Node.A"),
		FirstOwner);
	TestTrue(TEXT("The same stable Id can be registered by its reloaded actor"),
		DuplicateIdSubsystem->RegisterWorldProgressId(
			EOutlierWorldProgressType::CollectedNode,
			TEXT("Node.A"),
			SecondOwner));

	return true;
}

#endif
