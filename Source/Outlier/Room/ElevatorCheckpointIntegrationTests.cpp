#if WITH_DEV_AUTOMATION_TESTS

#include "CoreGlobals.h"
#include "OutlierGameMode.h"
#include "OutlierPlayerState.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Enemy/AutoTurret.h"
#include "Enemy/EnemyAdaptationSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Network/OutlierArenaSubsystem.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomVolume.h"
#include "Save/OutlierSaveSubSystem.h"
#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterInventoryComponent.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElevatorCheckpointIntegrationTest,
	"Outlier.Elevator.Combat.DurablePhaseCheckpoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElevatorCheckpointIntegrationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UClass* ModeClass = LoadClass<AOutlierGameMode>(nullptr,
		TEXT("/Game/Blueprints/Outlier/BP_OutlierGM.BP_OutlierGM_C"));
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	if (!TestNotNull(TEXT("Concrete GameMode BP"), ModeClass)
		|| !TestNotNull(TEXT("Concrete Shooter BP"), ShooterClass)) return false;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("ElevatorCheckpointTest")));
	if (!TestNotNull(TEXT("Save integration world"), World)) return false;
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
	World->SetGameInstance(GameInstance);
	GameInstance->OnWorldChanged(nullptr, World);
	GameInstance->Init();
	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("Automation"), World->GetName()));
	ON_SCOPE_EXIT
	{
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(false);
		GameInstance->Shutdown();
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		// 이 테스트가 만든 고유한 프로젝트 Saved/Automation 하위 경로만 정리한다.
		IFileManager::Get().DeleteDirectory(*Directory, false, true);
	};
	World->GetWorldSettings()->DefaultGameMode = ModeClass;
	if (!TestTrue(TEXT("Authority GameMode installs"), World->SetGameMode(FURL()))) return false;
	World->InitializeActorsForPlay(FURL());
	AOutlierGameMode* Mode = World->GetAuthGameMode<AOutlierGameMode>();
	URoomCombatSubsystem* Combat = World->GetSubsystem<URoomCombatSubsystem>();
	UOutlierSaveSubSystem* Saves = GameInstance->GetSubsystem<UOutlierSaveSubSystem>();
	UOutlierArenaSubsystem* Arena = World->GetSubsystem<UOutlierArenaSubsystem>();
	if (!TestNotNull(TEXT("Authority GameMode"), Mode) || !TestNotNull(TEXT("Combat"), Combat)
		|| !TestNotNull(TEXT("Save subsystem"), Saves) || !TestNotNull(TEXT("Arena lifetime"), Arena)) return false;
	TestFalse(TEXT("Storage test never uses the DirectPair skip"), AOutlierGameMode::IsDirectPairTestWorld(World));
	if (UEnemyAdaptationSubsystem* Adaptation = World->GetSubsystem<UEnemyAdaptationSubsystem>())
	{
		Adaptation->OnWorldBeginPlay(*World);
	}

	// 정상 저장 사전 검증을 그대로 통과하도록 실제 페어의 역할/소유 PC/현재 Pawn을 구성한다.
	AShooterCharacter* Shooter = World->SpawnActor<AShooterCharacter>(ShooterClass,
		FTransform(FVector(3000.0, 0.0, 300.0)));
	APartnerCharacter* Partner = World->SpawnActor<APartnerCharacter>(APartnerCharacter::StaticClass(),
		FTransform(FVector(4000.0, 0.0, 300.0)));
	APlayerController* ShooterPC = World->SpawnActor<APlayerController>();
	APlayerController* PartnerPC = World->SpawnActor<APlayerController>();
	AOutlierPlayerState* ShooterPS = World->SpawnActor<AOutlierPlayerState>();
	AOutlierPlayerState* PartnerPS = World->SpawnActor<AOutlierPlayerState>();
	FObjectProperty* PawnProperty = FindFProperty<FObjectProperty>(AController::StaticClass(), TEXT("Pawn"));
	if (!Mode->GameState) Mode->GameState = World->SpawnActor<AGameStateBase>();
	if (!TestNotNull(TEXT("Shooter"), Shooter) || !TestNotNull(TEXT("Partner"), Partner)
		|| !TestNotNull(TEXT("Shooter PC"), ShooterPC) || !TestNotNull(TEXT("Partner PC"), PartnerPC)
		|| !TestNotNull(TEXT("Shooter PS"), ShooterPS) || !TestNotNull(TEXT("Partner PS"), PartnerPS)
		|| !TestNotNull(TEXT("Pawn property"), PawnProperty) || !TestNotNull(TEXT("GameState"), Mode->GameState.Get())) return false;
	if (!TestNotNull(TEXT("Live inventory required by checkpoint capture"), Shooter->GetInventoryComponent())) return false;
	Mode->ActiveMatchPairId = 0;
	ShooterPS->SetPairId(0);
	PartnerPS->SetPairId(0);
	ShooterPS->SetPlayerRole(EOutlierPlayerRole::Shooter);
	PartnerPS->SetPlayerRole(EOutlierPlayerRole::Partner);
	ShooterPS->SetOwner(ShooterPC);
	PartnerPS->SetOwner(PartnerPC);
	ShooterPS->SetShooterCharacter(Shooter);
	PartnerPS->SetPartnerCharacter(Partner);
	Mode->GameState->PlayerArray.AddUnique(ShooterPS);
	Mode->GameState->PlayerArray.AddUnique(PartnerPS);
	PawnProperty->SetObjectPropertyValue_InContainer(ShooterPC, Shooter);
	PawnProperty->SetObjectPropertyValue_InContainer(PartnerPC, Partner);
	ON_SCOPE_EXIT
	{
		PawnProperty->SetObjectPropertyValue_InContainer(ShooterPC, nullptr);
		PawnProperty->SetObjectPropertyValue_InContainer(PartnerPC, nullptr);
	};

	Saves->SetAutoSaveDirectoryForTesting(Directory);
	const FGuid SaveOwnerId = FGuid::NewGuid();
	const FGuid SaveId = FGuid::NewGuid();
	const FString Verifier = UOutlierSaveSubSystem::MakeKeyVerifier(FGuid::NewGuid());
	if (!TestTrue(TEXT("Durable save identity"), Saves->ConfigureNewSave(SaveOwnerId, SaveId, Verifier))) return false;
	FOutlierCheckpointSnapshot Baseline;
	// 무기 복원은 별도 저장 테스트가 담당한다. 이 테스트는 정상 허용되는 빈 로드아웃으로 저장 경계만 검증한다.
	Baseline.CheckpointId = TEXT("Elevator.BeforeEntry");
	Baseline.ShooterSpawnTransform.SetLocation(FVector(100.0, 0.0, 100.0));
	Baseline.PartnerSpawnTransform.SetLocation(FVector(200.0, 0.0, 100.0));
	if (!TestTrue(TEXT("Before-entry checkpoint commits to disk"), Saves->CommitDurableCheckpointSnapshot(Baseline))) return false;
	const FString LatestFile = FPaths::Combine(Directory, SaveId.ToString(EGuidFormats::Digits), TEXT("LatestAutoSave.sav"));
	TArray<uint8> BeforeEntryBytes;
	if (!TestTrue(TEXT("Before-entry file is readable"), FFileHelper::LoadFileToArray(BeforeEntryBytes, *LatestFile))) return false;

	const FGameplayTag ElevatorTag = FGameplayTag::RequestGameplayTag(TEXT("Room.Level01.1"));
	const FGameplayTag OrdinaryTag = FGameplayTag::RequestGameplayTag(TEXT("Room.Level01.2"));
	URoomCombatDefinition* Definition = NewObject<URoomCombatDefinition>(World);
	for (FGameplayTag RoomTag : {ElevatorTag, OrdinaryTag})
	{
		FRoomCombatRoomDefinition& RoomDefinition = Definition->RoomDefinitions.AddDefaulted_GetRef();
		RoomDefinition.RoomTag = RoomTag;
		for (int32 PhaseIndex = 0; PhaseIndex < 2; ++PhaseIndex)
		{
			FRoomCombatPhaseDefinition& Phase = RoomDefinition.CombatPhases.AddDefaulted_GetRef();
			Phase.StartPolicy = PhaseIndex == 0 ? ERoomCombatPhaseStartPolicy::ExternalTrigger : ERoomCombatPhaseStartPolicy::Automatic;
			Phase.DelayBeforeStartSeconds = PhaseIndex == 0 ? 0.0f : 0.5f;
			FRoomCombatWaveDefinition& Wave = Phase.Waves.AddDefaulted_GetRef();
			Wave.SpawnMode = ERoomCombatWaveSpawnMode::SpawnFromObjects;
			Wave.ExpectedWaveTurretCount = 1;
		}
	}
	Combat->SetCombatDefinitionForTesting(Definition);
	ARoomVolume* Room = World->SpawnActor<ARoomVolume>();
	if (!TestNotNull(TEXT("Elevator Room"), Room)) return false;
	auto SpawnTurret = [&](FGameplayTag RoomTag, int32 PhaseIndex)
	{
		AAutoTurret* Turret = World->SpawnActorDeferred<AAutoTurret>(AAutoTurret::StaticClass(), FTransform::Identity);
		if (Turret)
		{
			const FName StableId(*FString::Printf(TEXT("ElevatorSave.%s.%d"), *RoomTag.ToString(), PhaseIndex));
			Turret->ConfigureWaveRegistrationForTesting(RoomTag, PhaseIndex, 0, StableId);
			Turret->FinishSpawning(FTransform::Identity);
			if (!Turret->HasActorBegunPlay()) Turret->DispatchBeginPlay();
		}
		return Turret;
	};
	AAutoTurret* First = SpawnTurret(ElevatorTag, 0);
	AAutoTurret* Last = SpawnTurret(ElevatorTag, 1);
	if (!TestNotNull(TEXT("First phase turret"), First) || !TestNotNull(TEXT("Last phase turret"), Last)
		|| !TestTrue(TEXT("Elevator registers"), Combat->RegisterRoom(Room, ElevatorTag))) return false;
	FRoomCombatTriggerContext Context;
	if (!TestTrue(TEXT("Elevator context"), Combat->CreateTriggerContext(Room, ElevatorTag, FGameplayTag(), Context))) return false;
	const FGuid ExecutionId = FGuid::NewGuid();
	if (!TestTrue(TEXT("Execution owns intermediate-save deferral"), Combat->AcquireEncounterCheckpointHold(Context, ExecutionId))
		|| !TestTrue(TEXT("Elevator starts"), Combat->StartTriggeredSequence(Room, Context))) return false;
	First->NotifyDeploySequenceFinished(First->GetRoomWaveGameplayGeneration(), First->GetRoomWaveActivationSerial());
	Combat->NotifyEnemyDefeated(First);
	TestEqual(TEXT("Real boundary advances runtime phase"), Combat->GetCurrentCombatPhaseIndex(ElevatorTag), 1);
	TestTrue(TEXT("Real boundary records transient phase progress"), Saves->GetCurrentRoomPhaseProgress().Contains(ElevatorTag));
	FOutlierCheckpointSnapshot Snapshot;
	TestTrue(TEXT("Confirmed snapshot remains available"), Saves->GetRestoreSnapshot(Snapshot));
	TestEqual(TEXT("Intermediate boundary keeps before-entry restore point"), Snapshot.CheckpointId, Baseline.CheckpointId);
	TArray<uint8> IntermediateBytes;
	TestTrue(TEXT("Intermediate file is readable"), FFileHelper::LoadFileToArray(IntermediateBytes, *LatestFile));
	TestTrue(TEXT("Intermediate boundary does not rewrite disk checkpoint"), IntermediateBytes == BeforeEntryBytes);
	TestTrue(TEXT("Disk restore during later-phase wait succeeds"), Saves->LoadLatestSave(SaveOwnerId, SaveId, Verifier));
	TestTrue(TEXT("Reloaded baseline exists"), Saves->GetRestoreSnapshot(Snapshot));
	TestEqual(TEXT("Incomplete disk restore starts before entry"), Snapshot.CheckpointId, Baseline.CheckpointId);
	TestFalse(TEXT("Incomplete snapshot has no elevator phase resume"), Snapshot.RoomPhaseProgress.Contains(ElevatorTag));

	// 프레임 밖에서 예약한 타이머를 먼저 활성 목록에 반영한 뒤 실제 대기 시간을 진행한다.
	++GFrameCounter;
	World->GetTimerManager().Tick(0.0f);
	++GFrameCounter;
	World->GetTimerManager().Tick(0.6f);
	Last->NotifyDeploySequenceFinished(Last->GetRoomWaveGameplayGeneration(), Last->GetRoomWaveActivationSerial());
	Combat->NotifyEnemyDefeated(Last);
	TestEqual(TEXT("Real final phase clears Room"), Combat->GetRoomState(ElevatorTag), ERoomCombatState::Cleared);
	TestTrue(TEXT("Final confirmed snapshot exists"), Saves->GetRestoreSnapshot(Snapshot));
	TestEqual(TEXT("Final save is EncounterCleared"), Snapshot.SaveReason, EOutlierCheckpointSaveReason::EncounterCleared);
	TestEqual(TEXT("Final save identifies elevator"), Snapshot.RoomTag, ElevatorTag);
	TestTrue(TEXT("Final save records completed encounter"), Snapshot.WorldProgress.CompletedEncounterIds.Contains(ElevatorTag.GetTagName()));
	TestTrue(TEXT("Final checkpoint reloads from disk"), Saves->LoadLatestSave(SaveOwnerId, SaveId, Verifier));
	TestTrue(TEXT("Reloaded final snapshot exists"), Saves->GetRestoreSnapshot(Snapshot));
	TestEqual(TEXT("Disk restore preserves completion reason"), Snapshot.SaveReason, EOutlierCheckpointSaveReason::EncounterCleared);
	TestTrue(TEXT("Disk restore preserves elevator completion"), Snapshot.WorldProgress.CompletedEncounterIds.Contains(ElevatorTag.GetTagName()));

	// 저장 제한은 다른 Room에 전파되지 않는다. 동일한 실제 차수 경계가 정상 저장되어야 한다.
	TestTrue(TEXT("Elevator still owns its isolated hold"),
		Combat->IsCombatPhaseCheckpointDeferred(ElevatorTag, Context.GameplayGeneration));
	TestFalse(TEXT("Elevator hold does not defer another Room"),
		Combat->IsCombatPhaseCheckpointDeferred(OrdinaryTag, Context.GameplayGeneration));
	ARoomVolume* OrdinaryRoom = World->SpawnActor<ARoomVolume>();
	AAutoTurret* OrdinaryFirst = SpawnTurret(OrdinaryTag, 0);
	AAutoTurret* OrdinaryLast = SpawnTurret(OrdinaryTag, 1);
	if (!TestNotNull(TEXT("Ordinary Room"), OrdinaryRoom) || !TestNotNull(TEXT("Ordinary first turret"), OrdinaryFirst)
		|| !TestNotNull(TEXT("Ordinary final turret"), OrdinaryLast)
		|| !TestTrue(TEXT("Ordinary Room registers"), Combat->RegisterRoom(OrdinaryRoom, OrdinaryTag))) return false;
	FRoomCombatTriggerContext OrdinaryContext;
	if (!TestTrue(TEXT("Ordinary context"), Combat->CreateTriggerContext(OrdinaryRoom, OrdinaryTag, FGameplayTag(), OrdinaryContext))
		|| !TestTrue(TEXT("Ordinary Room starts"), Combat->StartTriggeredSequence(OrdinaryRoom, OrdinaryContext))) return false;
	OrdinaryFirst->NotifyDeploySequenceFinished(OrdinaryFirst->GetRoomWaveGameplayGeneration(), OrdinaryFirst->GetRoomWaveActivationSerial());
	Combat->NotifyEnemyDefeated(OrdinaryFirst);
	TestTrue(TEXT("Ordinary phase checkpoint exists"), Saves->GetRestoreSnapshot(Snapshot));
	TestEqual(TEXT("Ordinary Room still commits PhaseTransition"), Snapshot.SaveReason, EOutlierCheckpointSaveReason::PhaseTransition);
	TestEqual(TEXT("Ordinary checkpoint identifies correct Room"), Snapshot.RoomTag, OrdinaryTag);
	TestEqual(TEXT("Ordinary checkpoint preserves next phase"), Snapshot.NextPhaseIndex, 1);
	TestTrue(TEXT("Ordinary phase checkpoint reloads from disk"), Saves->LoadLatestSave(SaveOwnerId, SaveId, Verifier));
	TestTrue(TEXT("Reloaded ordinary snapshot exists"), Saves->GetRestoreSnapshot(Snapshot));
	TestEqual(TEXT("Ordinary phase boundary is durable"), Snapshot.RoomTag, OrdinaryTag);
	Combat->ReleaseEncounterCheckpointHold(ElevatorTag, ExecutionId);
	Combat->ResetRuntimeCombatState();
	return true;
}

#endif
