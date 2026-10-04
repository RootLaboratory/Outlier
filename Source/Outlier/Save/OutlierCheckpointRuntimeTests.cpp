#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "OutlierPlayerState.h"
#include "Save/OutlierSaveSubSystem.h"
#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterInventoryComponent.h"
#include "Weapon/RangedWeaponBase.h"
#include "Weapon/WeaponBase.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierCheckpointRuntimeSnapshotTest,
	"Outlier.Save.Checkpoint.RuntimeSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierCheckpointRuntimeSnapshotTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UClass* HammerClass = LoadClass<AWeaponBase>(nullptr,
		TEXT("/Game/Blueprints/Weapon/BP_Hammer.BP_Hammer_C"));
	UClass* RifleClass = LoadClass<AWeaponBase>(nullptr,
		TEXT("/Game/Blueprints/Weapon/BP_Rifle.BP_Rifle_C"));
	if (!TestNotNull(TEXT("Snapshot hammer Blueprint"), HammerClass)
		|| !TestNotNull(TEXT("Snapshot rifle Blueprint"), RifleClass))
	{
		return false;
	}
	const int32 MeleeIndex = static_cast<int32>(EWeaponSlot::Melee);
	const int32 PrimaryIndex = static_cast<int32>(EWeaponSlot::Primary);

	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UOutlierSaveSubSystem* SaveSubsystem = NewObject<UOutlierSaveSubSystem>(GameInstance);
	if (!TestNotNull(TEXT("Runtime save subsystem can be created"), SaveSubsystem))
	{
		return false;
	}

	FOutlierCheckpointSnapshot Initial;
	Initial.bInitialSnapshot = true;
	// 최초 지급 직후의 기록과 Rifle 획득 후 체크포인트 기록을 서로 다른 장착 상태로 검증한다.
	Initial.LoadoutSnapshot.SlotSnapshots.SetNum(static_cast<int32>(EWeaponSlot::Max));
	Initial.LoadoutSnapshot.SlotSnapshots[MeleeIndex].WeaponClass = HammerClass;
	Initial.LoadoutSnapshot.CurrentSlot = EWeaponSlot::Melee;
	Initial.ShooterSpawnTransform.SetLocation(FVector(100.0, 200.0, 300.0));
	Initial.WorldProgress.CollectedNodeIds.Add(TEXT("InitialNode"));
	Initial.WorldProgress.ExplodedPropIds.Add(TEXT("Explosive.Initial"));
	Initial.DestroyedTurretIds.Add(TEXT("Turret.Initial"));
	Initial.GunAdaptationStack = 3;
	const FGameplayTag PhaseRoom = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")));
	TestTrue(TEXT("The first initial snapshot is accepted"), SaveSubsystem->CaptureInitialSnapshot(Initial));

	FOutlierCheckpointSnapshot ReplacementInitial = Initial;
	ReplacementInitial.ShooterSpawnTransform.SetLocation(FVector(999.0));
	TestFalse(TEXT("The initial snapshot cannot be replaced"), SaveSubsystem->CaptureInitialSnapshot(ReplacementInitial));

	FOutlierCheckpointSnapshot RestoreTarget;
	TestTrue(TEXT("The initial snapshot is selected before a checkpoint commit"), SaveSubsystem->GetRestoreSnapshot(RestoreTarget));
	TestEqual(TEXT("Initial snapshot values are copied"), RestoreTarget.ShooterSpawnTransform.GetLocation(), FVector(100.0, 200.0, 300.0));
	TestTrue(TEXT("Initial restart preserves the hammer class"),
		RestoreTarget.LoadoutSnapshot.SlotSnapshots.IsValidIndex(MeleeIndex)
		&& RestoreTarget.LoadoutSnapshot.SlotSnapshots[MeleeIndex].WeaponClass.Get() == HammerClass);
	TestTrue(TEXT("Initial restart selects Melee"), RestoreTarget.LoadoutSnapshot.CurrentSlot == EWeaponSlot::Melee);
	TestTrue(TEXT("Initial exploded prop progress is copied"),
		RestoreTarget.WorldProgress.ExplodedPropIds.Contains(TEXT("Explosive.Initial")));
	TestTrue(TEXT("Initial destroyed turret progress is copied"),
		RestoreTarget.DestroyedTurretIds.Contains(TEXT("Turret.Initial")));
	TestEqual(TEXT("Initial enemy adaptation stack is copied"),
		RestoreTarget.GunAdaptationStack, 3);
	TestTrue(TEXT("Initial restart has no saved combat phase"), RestoreTarget.RoomPhaseProgress.IsEmpty());

	FOutlierCheckpointSnapshot Checkpoint;
	Checkpoint.CheckpointId = TEXT("Checkpoint.A");
	Checkpoint.LoadoutSnapshot = Initial.LoadoutSnapshot;
	Checkpoint.LoadoutSnapshot.SlotSnapshots[PrimaryIndex].WeaponClass = RifleClass;
	Checkpoint.LoadoutSnapshot.SlotSnapshots[PrimaryIndex].CurrentAmmo = 7;
	Checkpoint.LoadoutSnapshot.CurrentSlot = EWeaponSlot::Primary;
	Checkpoint.WorldProgress.CollectedNodeIds.Add(TEXT("SavedNode"));
	Checkpoint.WorldProgress.ExplodedPropIds.Add(TEXT("Explosive.Saved"));
	Checkpoint.DestroyedTurretIds.Add(TEXT("Turret.Saved"));
	Checkpoint.GunAdaptationStack = 8;
	FOutlierRoomPhaseProgress SavedPhase;
	SavedPhase.NextPhaseIndex = 1;
	SavedPhase.bExitBlockActive = true;
	Checkpoint.RoomPhaseProgress.Add(PhaseRoom, SavedPhase);
	TestTrue(TEXT("A valid checkpoint snapshot is committed"), SaveSubsystem->CommitCheckpointSnapshotForTesting(Checkpoint));

	FOutlierCheckpointSnapshot Duplicate = Checkpoint;
	Duplicate.WorldProgress.CollectedNodeIds.Add(TEXT("LateNode"));
	Duplicate.WorldProgress.ExplodedPropIds.Add(TEXT("Explosive.Late"));
	Duplicate.DestroyedTurretIds.Add(TEXT("Turret.Late"));
	Duplicate.GunAdaptationStack = 10;
	Duplicate.RoomPhaseProgress.FindChecked(PhaseRoom).NextPhaseIndex = 2;
	Duplicate.LoadoutSnapshot.SlotSnapshots[MeleeIndex].WeaponClass = nullptr;
	Duplicate.LoadoutSnapshot.CurrentSlot = EWeaponSlot::Melee;
	TestFalse(TEXT("The same checkpoint cannot be committed twice"), SaveSubsystem->CommitCheckpointSnapshotForTesting(Duplicate));
	TestTrue(TEXT("The latest checkpoint is selected after commit"), SaveSubsystem->GetRestoreSnapshot(RestoreTarget));
	TestTrue(TEXT("A rejected duplicate preserves the checkpoint hammer"),
		RestoreTarget.LoadoutSnapshot.SlotSnapshots.IsValidIndex(MeleeIndex)
		&& RestoreTarget.LoadoutSnapshot.SlotSnapshots[MeleeIndex].WeaponClass.Get() == HammerClass);
	TestTrue(TEXT("A rejected duplicate preserves the current Primary slot"),
		RestoreTarget.LoadoutSnapshot.CurrentSlot == EWeaponSlot::Primary);
	TestTrue(TEXT("Committed world progress is preserved"), RestoreTarget.WorldProgress.CollectedNodeIds.Contains(TEXT("SavedNode")));
	TestFalse(TEXT("A rejected duplicate cannot replace the saved snapshot"), RestoreTarget.WorldProgress.CollectedNodeIds.Contains(TEXT("LateNode")));
	TestTrue(TEXT("A checkpoint preserves exploded props"),
		RestoreTarget.WorldProgress.ExplodedPropIds.Contains(TEXT("Explosive.Saved")));
	TestFalse(TEXT("A rejected duplicate cannot add exploded props"),
		RestoreTarget.WorldProgress.ExplodedPropIds.Contains(TEXT("Explosive.Late")));
	TestTrue(TEXT("A checkpoint preserves destroyed turrets"),
		RestoreTarget.DestroyedTurretIds.Contains(TEXT("Turret.Saved")));
	TestFalse(TEXT("A rejected duplicate cannot add destroyed turrets"),
		RestoreTarget.DestroyedTurretIds.Contains(TEXT("Turret.Late")));
	TestEqual(TEXT("A rejected duplicate cannot replace the enemy adaptation stack"),
		RestoreTarget.GunAdaptationStack, 8);
	const FOutlierRoomPhaseProgress* RestoredPhase = RestoreTarget.RoomPhaseProgress.Find(PhaseRoom);
	TestTrue(TEXT("The saved room phase remains available"), RestoredPhase != nullptr);
	if (RestoredPhase)
	{
		TestEqual(TEXT("A rejected duplicate cannot advance the saved phase"),
			RestoredPhase->NextPhaseIndex, 1);
	}

	FOutlierCheckpointSnapshot LaterCheckpoint;
	LaterCheckpoint.CheckpointId = TEXT("Checkpoint.B");
	LaterCheckpoint.WorldProgress.OpenedDoorIds.Add(TEXT("Door.B"));
	LaterCheckpoint.DestroyedTurretIds.Add(TEXT("Turret.Newer"));
	LaterCheckpoint.GunAdaptationStack = 9;
	TestTrue(TEXT("A later checkpoint replaces the restore target"), SaveSubsystem->CommitCheckpointSnapshotForTesting(LaterCheckpoint));
	TestTrue(TEXT("The newer checkpoint is selected"), SaveSubsystem->GetRestoreSnapshot(RestoreTarget));
	TestEqual(TEXT("The newer checkpoint Id is preserved"), RestoreTarget.CheckpointId, FName(TEXT("Checkpoint.B")));
	TestTrue(TEXT("The newer checkpoint world state is copied"), RestoreTarget.WorldProgress.OpenedDoorIds.Contains(TEXT("Door.B")));
	TestFalse(TEXT("A later checkpoint replaces older exploded prop progress"),
		RestoreTarget.WorldProgress.ExplodedPropIds.Contains(TEXT("Explosive.Saved")));
	TestTrue(TEXT("A later checkpoint replaces the destroyed turret restore target"),
		RestoreTarget.DestroyedTurretIds.Contains(TEXT("Turret.Newer")));
	TestFalse(TEXT("A later checkpoint discards older destroyed turret progress"),
		RestoreTarget.DestroyedTurretIds.Contains(TEXT("Turret.Saved")));
	TestEqual(TEXT("A later checkpoint replaces the enemy adaptation stack"),
		RestoreTarget.GunAdaptationStack, 9);
	TestTrue(TEXT("A later checkpoint replaces older room phase progress"),
		RestoreTarget.RoomPhaseProgress.IsEmpty());

	SaveSubsystem->SetWorldProgressState(EOutlierWorldProgressType::CollectedNode, TEXT("AfterCheckpoint"), true);
	TestTrue(TEXT("Live world progress receives later changes"),
		SaveSubsystem->HasWorldProgress(EOutlierWorldProgressType::CollectedNode, TEXT("AfterCheckpoint")));
	TestTrue(TEXT("The committed snapshot can be read again"), SaveSubsystem->GetRestoreSnapshot(RestoreTarget));
	TestFalse(TEXT("Live progress does not mutate the committed copy"),
		RestoreTarget.WorldProgress.CollectedNodeIds.Contains(TEXT("AfterCheckpoint")));

	SaveSubsystem->RestoreCurrentDestroyedTurretIds(RestoreTarget.DestroyedTurretIds);
	TestTrue(TEXT("A live turret destruction is recorded"),
		SaveSubsystem->SetDestroyedTurretState(TEXT("Turret.AfterCheckpoint"), true));
	TestTrue(TEXT("The live destroyed turret can be queried"),
		SaveSubsystem->IsTurretDestroyed(TEXT("Turret.AfterCheckpoint")));
	TestFalse(TEXT("An empty turret stable Id is rejected"),
		SaveSubsystem->SetDestroyedTurretState(NAME_None, true));
	TestTrue(TEXT("The committed snapshot remains available after a live turret destruction"),
		SaveSubsystem->GetRestoreSnapshot(RestoreTarget));
	TestFalse(TEXT("Live turret progress does not mutate the committed copy"),
		RestoreTarget.DestroyedTurretIds.Contains(TEXT("Turret.AfterCheckpoint")));

	SaveSubsystem->RestoreCurrentDestroyedTurretIds(RestoreTarget.DestroyedTurretIds);
	TestFalse(TEXT("Checkpoint rollback removes turret deaths after the checkpoint"),
		SaveSubsystem->IsTurretDestroyed(TEXT("Turret.AfterCheckpoint")));
	TestTrue(TEXT("Checkpoint rollback preserves saved turret deaths"),
		SaveSubsystem->IsTurretDestroyed(TEXT("Turret.Newer")));
	SaveSubsystem->SetCurrentRoomPhaseProgress(PhaseRoom, SavedPhase);
	TestTrue(TEXT("Live combat phase may advance after the latest save"),
		SaveSubsystem->GetCurrentRoomPhaseProgress().Contains(PhaseRoom));
	SaveSubsystem->RestoreCurrentRoomPhaseProgress(RestoreTarget.RoomPhaseProgress);
	TestFalse(TEXT("Restart discards combat phase progress after the latest save"),
		SaveSubsystem->GetCurrentRoomPhaseProgress().Contains(PhaseRoom));

	SaveSubsystem->ResetRuntimeCheckpointState();
	TestFalse(TEXT("A new runtime session clears destroyed turret progress"),
		SaveSubsystem->IsTurretDestroyed(TEXT("Turret.Newer")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierCheckpointDurableCommitTest,
	"Outlier.Save.Checkpoint.DurableCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierCheckpointDurableCommitTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UClass* HammerClass = LoadClass<AWeaponBase>(nullptr,
		TEXT("/Game/Blueprints/Weapon/BP_Hammer.BP_Hammer_C"));
	UClass* RifleClass = LoadClass<AWeaponBase>(nullptr,
		TEXT("/Game/Blueprints/Weapon/BP_Rifle.BP_Rifle_C"));
	if (!TestNotNull(TEXT("Durable hammer Blueprint"), HammerClass)
		|| !TestNotNull(TEXT("Durable rifle Blueprint"), RifleClass))
	{
		return false;
	}
	const int32 MeleeIndex = static_cast<int32>(EWeaponSlot::Melee);
	const int32 PrimaryIndex = static_cast<int32>(EWeaponSlot::Primary);
	const FName WorldName = MakeUniqueObjectName(
		nullptr, UWorld::StaticClass(), NAME_None, EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Durable save world"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}
	World->AddToRoot();
	Context.SetCurrentWorld(World);
	UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
	World->SetGameInstance(GameInstance);
	GameInstance->OnWorldChanged(nullptr, World);
	GameInstance->Init();
	World->InitializeActorsForPlay(FURL());
	TestEqual(TEXT("Durable save game instance owns the test world"), GameInstance->GetWorld(), World);
	const FString Directory = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("Automation"), WorldName.ToString());
	IFileManager& Files = IFileManager::Get();
	UOutlierSaveSubSystem* Save = GameInstance->GetSubsystem<UOutlierSaveSubSystem>();
	if (TestNotNull(TEXT("Durable save subsystem"), Save))
	{
		Save->SetAutoSaveDirectoryForTesting(Directory);
		const FGuid OwnerId = FGuid::NewGuid();
		const FGuid SaveId = FGuid::NewGuid();
		const FGuid ResumeKey = FGuid::NewGuid();
		const FString Verifier = UOutlierSaveSubSystem::MakeKeyVerifier(ResumeKey);
		TestTrue(TEXT("New save identity is configured"),
			Save->ConfigureNewSave(OwnerId, SaveId, Verifier));
		FOutlierCheckpointSnapshot First;
		First.CheckpointId = TEXT("Checkpoint.Durable.First");
		// 이어하기는 메모리 복사만으로 충분하지 않다. 실제 파일 로드 후에도 클래스/슬롯/탄약을 확인한다.
		First.LoadoutSnapshot.SlotSnapshots.SetNum(static_cast<int32>(EWeaponSlot::Max));
		First.LoadoutSnapshot.SlotSnapshots[MeleeIndex].WeaponClass = HammerClass;
		First.LoadoutSnapshot.SlotSnapshots[PrimaryIndex].WeaponClass = RifleClass;
		First.LoadoutSnapshot.SlotSnapshots[PrimaryIndex].CurrentAmmo = 7;
		First.LoadoutSnapshot.CurrentSlot = EWeaponSlot::Primary;
		First.WorldProgress.OpenedDoorIds.Add(TEXT("Door.First"));
		const FGameplayTag PhaseRoom = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")));
		FOutlierRoomPhaseProgress PhaseProgress;
		PhaseProgress.NextPhaseIndex = 2;
		PhaseProgress.bExitBlockActive = true;
		First.RoomPhaseProgress.Add(PhaseRoom, PhaseProgress);
		TestTrue(TEXT("First disk commit succeeds"), Save->CommitDurableCheckpointSnapshot(First));
		const FString Latest = FPaths::Combine(Directory,
			SaveId.ToString(EGuidFormats::Digits), TEXT("LatestAutoSave.sav"));
		TArray<uint8> OriginalBytes;
		TestTrue(TEXT("Latest file exists"), FFileHelper::LoadFileToArray(OriginalBytes, *Latest));
		if (!OriginalBytes.IsEmpty())
		{
			FMemoryReader Reader(OriginalBytes);
			uint32 Magic = 0;
			int32 Version = 0;
			Reader << Magic;
			Reader << Version;
			TestEqual(TEXT("Disk header magic"), Magic, uint32(0x4F55544C));
			TestEqual(TEXT("Disk format version"), Version, int32(2));
		}
		FString ValidatedVerifier;
		TestTrue(TEXT("Owner can validate the resume key"),
			Save->ValidateResumeKey(OwnerId, SaveId, ResumeKey, ValidatedVerifier));
		TestEqual(TEXT("Validated key verifier matches"), ValidatedVerifier, Verifier);
		TestFalse(TEXT("Another owner cannot claim the save"),
			Save->ValidateResumeKey(FGuid::NewGuid(), SaveId, ResumeKey, ValidatedVerifier));
		TestFalse(TEXT("A wrong key cannot claim the save"),
			Save->ValidateResumeKey(OwnerId, SaveId, FGuid::NewGuid(), ValidatedVerifier));
		Save->ResetRuntimeCheckpointState();
		TestTrue(TEXT("Disk checkpoint loads after runtime reset"),
			Save->LoadLatestSave(OwnerId, SaveId, Verifier));
		FOutlierCheckpointSnapshot Restore;
		TestTrue(TEXT("Loaded disk snapshot is selected"), Save->GetRestoreSnapshot(Restore));
		TestEqual(TEXT("Disk checkpoint Id round trips"), Restore.CheckpointId, First.CheckpointId);
		TestTrue(TEXT("Disk load preserves the current Primary slot"),
			Restore.LoadoutSnapshot.CurrentSlot == EWeaponSlot::Primary);
		if (TestTrue(TEXT("Disk load preserves all weapon slots"),
			Restore.LoadoutSnapshot.SlotSnapshots.Num() == static_cast<int32>(EWeaponSlot::Max)))
		{
			TestTrue(TEXT("Disk hammer class round trips"),
				Restore.LoadoutSnapshot.SlotSnapshots[MeleeIndex].WeaponClass.Get() == HammerClass);
			TestEqual(TEXT("Disk hammer has no ammunition state"),
				Restore.LoadoutSnapshot.SlotSnapshots[MeleeIndex].CurrentAmmo, INDEX_NONE);
			TestTrue(TEXT("Disk rifle class round trips"),
				Restore.LoadoutSnapshot.SlotSnapshots[PrimaryIndex].WeaponClass.Get() == RifleClass);
			TestEqual(TEXT("Disk rifle ammunition round trips"),
				Restore.LoadoutSnapshot.SlotSnapshots[PrimaryIndex].CurrentAmmo, 7);
		}
		TestTrue(TEXT("Disk world progress round trips"),
			Restore.WorldProgress.OpenedDoorIds.Contains(TEXT("Door.First")));
		const FOutlierRoomPhaseProgress* RestoredPhase = Restore.RoomPhaseProgress.Find(PhaseRoom);
		TestTrue(TEXT("Disk room phase round trips"), RestoredPhase != nullptr);
		if (RestoredPhase)
		{
			TestEqual(TEXT("Disk next phase round trips"), RestoredPhase->NextPhaseIndex, 2);
			TestTrue(TEXT("Disk exit block state round trips"), RestoredPhase->bExitBlockActive);
		}
		TestFalse(TEXT("Another SaveId cannot load this file"),
			Save->LoadLatestSave(OwnerId, FGuid::NewGuid(), Verifier));
		FOutlierCheckpointSnapshot Later;
		Later.CheckpointId = TEXT("Checkpoint.Durable.Later");
		Save->SetAutoSaveDirectoryForTesting(Latest);
		TestFalse(TEXT("Disk failure rejects the later commit"),
			Save->CommitDurableCheckpointSnapshot(Later));
		TestTrue(TEXT("Previous runtime snapshot survives failed write"), Save->GetRestoreSnapshot(Restore));
		TestEqual(TEXT("Previous checkpoint remains selected"), Restore.CheckpointId, First.CheckpointId);
		TArray<uint8> CurrentBytes;
		TestTrue(TEXT("Previous disk file remains readable"), FFileHelper::LoadFileToArray(CurrentBytes, *Latest));
		TestTrue(TEXT("Failed write leaves disk contents untouched"), OriginalBytes == CurrentBytes);
		Save->SetAutoSaveDirectoryForTesting(Directory);
		const FString Backup = Latest + TEXT(".bak");
		TestTrue(TEXT("Valid backup is written for interruption test"),
			FFileHelper::SaveArrayToFile(OriginalBytes, *Backup));
		TArray<uint8> CorruptBytes = OriginalBytes;
		CorruptBytes.Last() ^= 0x7f;
		TestTrue(TEXT("Latest can be corrupted for test"),
			FFileHelper::SaveArrayToFile(CorruptBytes, *Latest));
		Save->ResetRuntimeCheckpointState();
		TestTrue(TEXT("Valid backup loads when latest is damaged"),
			Save->LoadLatestSave(OwnerId, SaveId, Verifier));
		TestTrue(TEXT("Latest commit repairs damaged latest without losing backup"),
			Save->CommitDurableCheckpointSnapshot(Later));
		Save->ResetRuntimeCheckpointState();
		TestTrue(TEXT("Repaired latest loads"), Save->LoadLatestSave(OwnerId, SaveId, Verifier));
		TestTrue(TEXT("Repaired snapshot can be read"), Save->GetRestoreSnapshot(Restore));
		TestEqual(TEXT("Repaired latest checkpoint"), Restore.CheckpointId, Later.CheckpointId);
		TArray<uint8> VersionBytes;
		TestTrue(TEXT("Latest exists before version test"),
			FFileHelper::LoadFileToArray(VersionBytes, *Latest));
		if (VersionBytes.Num() > 4)
		{
			VersionBytes[4] = 1;
			TestTrue(TEXT("Old-version file is written"),
				FFileHelper::SaveArrayToFile(VersionBytes, *Latest));
			Save->ResetRuntimeCheckpointState();
			TestFalse(TEXT("Old-version save is rejected"),
				Save->LoadLatestSave(OwnerId, SaveId, Verifier));
		}
	}
	Files.DeleteDirectory(*Directory, false, true);
	GEngine->ShutdownWorldNetDriver(World);
	World->DestroyWorld(true);
	GameInstance->Shutdown();
	World->SetPhysicsScene(nullptr);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();
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
		EmptyIdSubsystem->CommitCheckpointSnapshotForTesting(InvalidCommit));

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
	UClass* HammerClass = LoadClass<AWeaponBase>(nullptr,
		TEXT("/Game/Blueprints/Weapon/BP_Hammer.BP_Hammer_C"));
	AShooterCharacter* CheckpointShooter = ShooterClass
		? World->SpawnActor<AShooterCharacter>(ShooterClass)
		: nullptr;
	AShooterCharacter* PresetShooter = ShooterClass
		? World->SpawnActor<AShooterCharacter>(ShooterClass)
		: nullptr;
	if (!TestNotNull(TEXT("Checkpoint Shooter is spawned"), CheckpointShooter)
		|| !TestNotNull(TEXT("Preset Shooter is spawned"), PresetShooter)
		|| !TestNotNull(TEXT("Rifle Blueprint is loadable"), RifleClass)
		|| !TestNotNull(TEXT("Hammer Blueprint is loadable"), HammerClass))
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
	SavedLoadout.SlotSnapshots[static_cast<int32>(EWeaponSlot::Melee)].WeaponClass = HammerClass;

	UShooterInventoryComponent* CheckpointInventory = CheckpointShooter->GetInventoryComponent();
	UShooterInventoryComponent* PresetInventory = PresetShooter->GetInventoryComponent();
	if (!TestNotNull(TEXT("Checkpoint inventory"), CheckpointInventory)
		|| !TestNotNull(TEXT("Preset inventory"), PresetInventory))
	{
		CleanupWorld();
		return false;
	}
	// 같은 저장 내용을 두 경로에 전달한다. 복원 모드가 달라도 망치와 현재 슬롯은 같아야 한다.
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
	for (AShooterCharacter* RestoredShooter : { CheckpointShooter, PresetShooter })
	{
		UShooterInventoryComponent* RestoredInventory = RestoredShooter->GetInventoryComponent();
		AWeaponBase* RestoredHammer = RestoredInventory->GetWeaponInSlot(EWeaponSlot::Melee);
		if (TestNotNull(TEXT("Both restore modes retain the hammer"), RestoredHammer))
		{
			TestTrue(TEXT("Both restore modes retain the saved hammer class"), RestoredHammer->GetClass() == HammerClass);
			TestFalse(TEXT("The restored hammer is not a pickup"), RestoredHammer->CanBePickedUpBy(RestoredShooter));
		}
	}
	TestTrue(TEXT("Checkpoint restore leaves Rifle current"), CheckpointShooter->GetCurrentWeapon() == CheckpointRifle);
	TestTrue(TEXT("Preset restore leaves Rifle current"), PresetShooter->GetCurrentWeapon() == PresetRifle);
	TestFalse(TEXT("Checkpoint restore has no equip action lock"), CheckpointShooter->IsActionLocked());
	TestFalse(TEXT("Preset restore has no equip action lock"), PresetShooter->IsActionLocked());

	FOutlierLoadoutSnapshot CapturedCheckpointLoadout;
	CheckpointInventory->BuildLoadoutSnapshot(
		CapturedCheckpointLoadout,
		/*bCaptureAmmo=*/true);
	TestEqual(
		TEXT("Checkpoint capture reads live weapon ammo"),
		CapturedCheckpointLoadout.SlotSnapshots[static_cast<int32>(EWeaponSlot::Primary)].CurrentAmmo,
		7);
	TestTrue(TEXT("Checkpoint capture preserves Melee class"),
		CapturedCheckpointLoadout.SlotSnapshots[static_cast<int32>(EWeaponSlot::Melee)].WeaponClass.Get() == HammerClass);
	TestTrue(TEXT("Checkpoint capture preserves current Primary"), CapturedCheckpointLoadout.CurrentSlot == EWeaponSlot::Primary);
	TestEqual(TEXT("Checkpoint hammer has no ammunition state"),
		CapturedCheckpointLoadout.SlotSnapshots[static_cast<int32>(EWeaponSlot::Melee)].CurrentAmmo, INDEX_NONE);

	FOutlierLoadoutSnapshot CapturedPresetLoadout;
	PresetInventory->BuildLoadoutSnapshot(
		CapturedPresetLoadout,
		/*bCaptureAmmo=*/false);
	TestEqual(
		TEXT("Ordinary loadout capture omits ammo"),
		CapturedPresetLoadout.SlotSnapshots[static_cast<int32>(EWeaponSlot::Primary)].CurrentAmmo,
		INDEX_NONE);
	TestTrue(TEXT("Preset capture preserves Melee class"),
		CapturedPresetLoadout.SlotSnapshots[static_cast<int32>(EWeaponSlot::Melee)].WeaponClass.Get() == HammerClass);
	TestTrue(TEXT("Preset capture preserves current Primary"), CapturedPresetLoadout.CurrentSlot == EWeaponSlot::Primary);

	CleanupWorld();
	return true;
}

#endif
