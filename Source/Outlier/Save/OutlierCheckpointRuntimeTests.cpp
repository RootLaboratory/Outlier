#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "OutlierPlayerState.h"
#include "Save/OutlierSaveSubSystem.h"
#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterInventoryComponent.h"
#include "Weapon/RangedWeaponBase.h"

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
	Initial.WorldProgress.ExplodedPropIds.Add(TEXT("Explosive.Initial"));
	TestTrue(TEXT("The first initial snapshot is accepted"), SaveSubsystem->CaptureInitialSnapshot(Initial));

	FOutlierCheckpointSnapshot ReplacementInitial = Initial;
	ReplacementInitial.ShooterSpawnTransform.SetLocation(FVector(999.0));
	TestFalse(TEXT("The initial snapshot cannot be replaced"), SaveSubsystem->CaptureInitialSnapshot(ReplacementInitial));

	FOutlierCheckpointSnapshot RestoreTarget;
	TestTrue(TEXT("The initial snapshot is selected before a checkpoint commit"), SaveSubsystem->GetRestoreSnapshot(RestoreTarget));
	TestEqual(TEXT("Initial snapshot values are copied"), RestoreTarget.ShooterSpawnTransform.GetLocation(), FVector(100.0, 200.0, 300.0));
	TestTrue(TEXT("Initial exploded prop progress is copied"),
		RestoreTarget.WorldProgress.ExplodedPropIds.Contains(TEXT("Explosive.Initial")));

	FOutlierCheckpointSnapshot Checkpoint;
	Checkpoint.CheckpointId = TEXT("Checkpoint.A");
	Checkpoint.WorldProgress.CollectedNodeIds.Add(TEXT("SavedNode"));
	Checkpoint.WorldProgress.ExplodedPropIds.Add(TEXT("Explosive.Saved"));
	TestTrue(TEXT("A valid checkpoint snapshot is committed"), SaveSubsystem->CommitCheckpointSnapshot(Checkpoint));

	FOutlierCheckpointSnapshot Duplicate = Checkpoint;
	Duplicate.WorldProgress.CollectedNodeIds.Add(TEXT("LateNode"));
	Duplicate.WorldProgress.ExplodedPropIds.Add(TEXT("Explosive.Late"));
	TestFalse(TEXT("The same checkpoint cannot be committed twice"), SaveSubsystem->CommitCheckpointSnapshot(Duplicate));
	TestTrue(TEXT("The latest checkpoint is selected after commit"), SaveSubsystem->GetRestoreSnapshot(RestoreTarget));
	TestTrue(TEXT("Committed world progress is preserved"), RestoreTarget.WorldProgress.CollectedNodeIds.Contains(TEXT("SavedNode")));
	TestFalse(TEXT("A rejected duplicate cannot replace the saved snapshot"), RestoreTarget.WorldProgress.CollectedNodeIds.Contains(TEXT("LateNode")));
	TestTrue(TEXT("A checkpoint preserves exploded props"),
		RestoreTarget.WorldProgress.ExplodedPropIds.Contains(TEXT("Explosive.Saved")));
	TestFalse(TEXT("A rejected duplicate cannot add exploded props"),
		RestoreTarget.WorldProgress.ExplodedPropIds.Contains(TEXT("Explosive.Late")));

	FOutlierCheckpointSnapshot LaterCheckpoint;
	LaterCheckpoint.CheckpointId = TEXT("Checkpoint.B");
	LaterCheckpoint.WorldProgress.OpenedDoorIds.Add(TEXT("Door.B"));
	TestTrue(TEXT("A later checkpoint replaces the restore target"), SaveSubsystem->CommitCheckpointSnapshot(LaterCheckpoint));
	TestTrue(TEXT("The newer checkpoint is selected"), SaveSubsystem->GetRestoreSnapshot(RestoreTarget));
	TestEqual(TEXT("The newer checkpoint Id is preserved"), RestoreTarget.CheckpointId, FName(TEXT("Checkpoint.B")));
	TestTrue(TEXT("The newer checkpoint world state is copied"), RestoreTarget.WorldProgress.OpenedDoorIds.Contains(TEXT("Door.B")));
	TestFalse(TEXT("A later checkpoint replaces older exploded prop progress"),
		RestoreTarget.WorldProgress.ExplodedPropIds.Contains(TEXT("Explosive.Saved")));

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierCheckpointPlayerProgressTest,
	"Outlier.Save.Checkpoint.PlayerProgress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierCheckpointPlayerProgressTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FName WorldName = MakeUniqueObjectName(
		nullptr,
		UWorld::StaticClass(),
		NAME_None,
		EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Checkpoint player progress world is created"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}

	World->AddToRoot();
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	auto CleanupWorld = [World]()
	{
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};

	AOutlierPlayerState* PlayerState = World->SpawnActor<AOutlierPlayerState>();
	if (!TestNotNull(TEXT("Checkpoint PlayerState is spawned"), PlayerState))
	{
		CleanupWorld();
		return false;
	}

	PlayerState->SetPairId(1);
	PlayerState->AddNode(9);
	PlayerState->AddActivatedUpgradeNode(EOutlierUpgradeRole::Shooter, TEXT("Shooter.Saved"));
	PlayerState->AddActivatedUpgradeNode(EOutlierUpgradeRole::Shooter, TEXT("Shooter.AfterCheckpoint"));
	PlayerState->AddActivatedUpgradeNode(EOutlierUpgradeRole::Partner, TEXT("Partner.Unchanged"));

	const TArray<FName> SavedShooterNodes = { TEXT("Shooter.Saved") };
	PlayerState->RestoreCheckpointProgress(
		3,
		EOutlierUpgradeRole::Shooter,
		SavedShooterNodes);

	TestEqual(TEXT("Node count rolls back to the checkpoint value"), PlayerState->GetNodeCount(), 3);
	const TArray<FName>& RestoredShooterNodes =
		PlayerState->GetActivatedUpgradeNodeIds(EOutlierUpgradeRole::Shooter);
	TestEqual(TEXT("Only checkpoint Shooter upgrades remain"), RestoredShooterNodes.Num(), 1);
	TestTrue(
		TEXT("The checkpoint Shooter upgrade remains active"),
		RestoredShooterNodes.Contains(TEXT("Shooter.Saved")));
	TestFalse(
		TEXT("A post-checkpoint Shooter upgrade is removed"),
		RestoredShooterNodes.Contains(TEXT("Shooter.AfterCheckpoint")));
	TestTrue(
		TEXT("Restoring Shooter progress does not overwrite Partner upgrades"),
		PlayerState->GetActivatedUpgradeNodeIds(EOutlierUpgradeRole::Partner).Contains(
			TEXT("Partner.Unchanged")));

	UClass* ShooterClass = LoadClass<AShooterCharacter>(
		nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	UClass* RifleClass = LoadClass<ARangedWeaponBase>(
		nullptr,
		TEXT("/Game/Blueprints/Weapon/BP_Rifle.BP_Rifle_C"));
	AShooterCharacter* CheckpointShooter = ShooterClass
		? World->SpawnActor<AShooterCharacter>(ShooterClass)
		: nullptr;
	AShooterCharacter* PresetShooter = ShooterClass
		? World->SpawnActor<AShooterCharacter>(ShooterClass)
		: nullptr;
	if (!TestNotNull(TEXT("Checkpoint Shooter is spawned"), CheckpointShooter)
		|| !TestNotNull(TEXT("Preset Shooter is spawned"), PresetShooter)
		|| !TestNotNull(TEXT("Rifle Blueprint is loadable"), RifleClass))
	{
		CleanupWorld();
		return false;
	}
	if (!CheckpointShooter->HasActorBegunPlay())
	{
		CheckpointShooter->DispatchBeginPlay();
	}
	if (!PresetShooter->HasActorBegunPlay())
	{
		PresetShooter->DispatchBeginPlay();
	}

	FOutlierLoadoutSnapshot SavedLoadout;
	SavedLoadout.SlotSnapshots.SetNum(static_cast<int32>(EWeaponSlot::Max));
	SavedLoadout.CurrentSlot = EWeaponSlot::Primary;
	SavedLoadout.SlotSnapshots[static_cast<int32>(EWeaponSlot::Primary)].WeaponClass = RifleClass;
	SavedLoadout.SlotSnapshots[static_cast<int32>(EWeaponSlot::Primary)].CurrentAmmo = 7;

	UShooterInventoryComponent* CheckpointInventory = CheckpointShooter->GetInventoryComponent();
	UShooterInventoryComponent* PresetInventory = PresetShooter->GetInventoryComponent();
	CheckpointInventory->RestoreLoadout(SavedLoadout, /*bRestoreAmmo=*/true);
	PresetInventory->RestoreLoadout(SavedLoadout, /*bRestoreAmmo=*/false);

	const ARangedWeaponBase* CheckpointRifle = Cast<ARangedWeaponBase>(
		CheckpointInventory->GetWeaponInSlot(EWeaponSlot::Primary));
	const ARangedWeaponBase* PresetRifle = Cast<ARangedWeaponBase>(
		PresetInventory->GetWeaponInSlot(EWeaponSlot::Primary));
	if (!TestNotNull(TEXT("Checkpoint rifle is restored"), CheckpointRifle)
		|| !TestNotNull(TEXT("Preset rifle is restored"), PresetRifle))
	{
		CleanupWorld();
		return false;
	}

	TestEqual(TEXT("Checkpoint restore applies saved magazine ammo"), CheckpointRifle->GetCurrentAmmo(), 7);
	TestEqual(
		TEXT("Preset restore keeps the new weapon default magazine"),
		PresetRifle->GetCurrentAmmo(),
		PresetRifle->GetMagazineSize());

	FOutlierLoadoutSnapshot CapturedCheckpointLoadout;
	CheckpointInventory->BuildLoadoutSnapshot(
		CapturedCheckpointLoadout,
		/*bCaptureAmmo=*/true);
	TestEqual(
		TEXT("Checkpoint capture reads live weapon ammo"),
		CapturedCheckpointLoadout.SlotSnapshots[static_cast<int32>(EWeaponSlot::Primary)].CurrentAmmo,
		7);

	FOutlierLoadoutSnapshot CapturedPresetLoadout;
	CheckpointInventory->BuildLoadoutSnapshot(
		CapturedPresetLoadout,
		/*bCaptureAmmo=*/false);
	TestEqual(
		TEXT("Ordinary loadout capture omits ammo"),
		CapturedPresetLoadout.SlotSnapshots[static_cast<int32>(EWeaponSlot::Primary)].CurrentAmmo,
		INDEX_NONE);

	CleanupWorld();
	return true;
}

#endif
