#if WITH_DEV_AUTOMATION_TESTS

#include "OutlierGameMode.h"

#include "Drone/Partner/PartnerPlayerController.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Network/OutlierArenaSubsystem.h"
#include "Save/PresetPlayerStart.h"
#include "Save/OutlierSaveSubSystem.h"
#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterPlayerController.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierDirectPairTest,
	"Outlier.Network.DirectPairTest.MapAndSpawns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierDirectPairTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UClass* ModeClass = LoadClass<AOutlierGameMode>(nullptr,
		TEXT("/Game/Blueprints/Outlier/BP_OutlierGM.BP_OutlierGM_C"));
	if (!TestNotNull(TEXT("Gameplay GameMode BP exists"), ModeClass)) return false;
	AOutlierGameMode* Defaults = ModeClass->GetDefaultObject<AOutlierGameMode>();
	const bool bPreviousDirectTest = Defaults->bDirectPairTest;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("DirectPairTest")));
	if (!TestNotNull(TEXT("Test world exists"), World)) return false;
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		// 실제 에셋은 저장하지 않는다. CDO와 임시 월드 종류도 모든 조기 반환에서 복구한다.
		Defaults->bDirectPairTest = bPreviousDirectTest;
		World->WorldType = EWorldType::Game;
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	World->GetWorldSettings()->DefaultGameMode = ModeClass;
	Defaults->bDirectPairTest = true;
	TestFalse(TEXT("Enabled option cannot affect packaged/ordinary Game worlds"),
		AOutlierGameMode::IsDirectPairTestWorld(World));
	World->WorldType = EWorldType::PIE;
	Defaults->bDirectPairTest = false;
	TestFalse(TEXT("PIE without opt-in uses the existing path"),
		AOutlierGameMode::IsDirectPairTestWorld(World));
	Defaults->bDirectPairTest = true;
	TestTrue(TEXT("Map override is sufficient before GameState replication"),
		AOutlierGameMode::IsDirectPairTestWorld(World));
	TestFalse(TEXT("Missing world is never a direct pair test"),
		AOutlierGameMode::IsDirectPairTestWorld(nullptr));

	UOutlierArenaSubsystem* Arena = World->GetSubsystem<UOutlierArenaSubsystem>();
	if (!TestNotNull(TEXT("Arena subsystem exists"), Arena)) return false;
	// Pool/전투를 실제 시작하지 않고 준비 이벤트와 현재 맵 선택만 검증한다.
	Arena->OnArenaShown.Clear();
	int32 ShownCount = 0;
	Arena->OnArenaShown.AddLambda([&ShownCount]() { ++ShownCount; });
	Arena->EnsureArenaLoaded();
	Arena->EnsureArenaLoaded();
	TestTrue(TEXT("Current map is gameplay ready without ArenaLevel"), Arena->IsArenaContentReady());
	TestTrue(TEXT("Current persistent level owns the test"), Arena->GetArenaLoadedLevel() == World->PersistentLevel);
	TestTrue(TEXT("Arena world is the test world"), Arena->GetArenaWorld() == World);
	TestEqual(TEXT("No other Arena is streamed"), World->GetStreamingLevels().Num(), 0);
	TestEqual(TEXT("Content ready is broadcast once"), ShownCount, 1);
	AddExpectedMessage(TEXT("[DirectPairTest] Gameplay reload is unavailable"),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
	TestEqual(TEXT("Persistent test map cannot reserve a destructive gameplay reload"),
		Arena->ReserveGameplayGeneration(), uint32(0));
	Arena->OnArenaShown.Clear();

	AOutlierGameMode* Mode = World->SpawnActor<AOutlierGameMode>(ModeClass);
	if (!TestNotNull(TEXT("GameMode instance exists"), Mode)) return false;
	FTransform ShooterSpawn;
	FTransform PartnerSpawn;
	AddExpectedError(TEXT("[DirectPairTest] Place exactly one ordinary PlayerStart"),
		EAutomationExpectedErrorFlags::Contains, 3, false);
	TestFalse(TEXT("No starts fails instead of spawning at world origin"),
		Mode->ResolveDirectPairTestSpawns(ShooterSpawn, PartnerSpawn));
	APresetPlayerStart* Preset = World->SpawnActor<APresetPlayerStart>();
	APlayerStart* Shooter = World->SpawnActor<APlayerStart>();
	APlayerStart* Partner = World->SpawnActor<APlayerStart>();
	if (!TestNotNull(TEXT("Preset"), Preset) || !TestNotNull(TEXT("Shooter start"), Shooter)
		|| !TestNotNull(TEXT("Partner start"), Partner)) return false;
	Preset->PlayerStartTag = TEXT("Shooter");
	Partner->PlayerStartTag = TEXT("Partner");
	TestFalse(TEXT("Preset start cannot satisfy the ordinary Shooter start"),
		Mode->ResolveDirectPairTestSpawns(ShooterSpawn, PartnerSpawn));
	Shooter->PlayerStartTag = TEXT("Shooter");
	Shooter->SetActorLocation(FVector(1000.0, 2000.0, 300.0));
	Partner->SetActorLocation(FVector(1500.0, 2000.0, 350.0));
	Partner->SetActorRotation(FRotator(0.0, 90.0, 0.0));
	TestTrue(TEXT("Unique role tags resolve"), Mode->ResolveDirectPairTestSpawns(ShooterSpawn, PartnerSpawn));
	TestTrue(TEXT("Shooter transform is preserved"), ShooterSpawn.Equals(Shooter->GetActorTransform()));
	TestTrue(TEXT("Partner transform is preserved"), PartnerSpawn.Equals(Partner->GetActorTransform()));
	TestTrue(TEXT("Placed actors belong to the current map"), Arena->IsActorOwnedByArena(Shooter));
	APlayerStart* Duplicate = World->SpawnActor<APlayerStart>();
	if (!TestNotNull(TEXT("Duplicate start"), Duplicate)) return false;
	Duplicate->PlayerStartTag = TEXT("Partner");
	TestFalse(TEXT("Duplicate role tags fail deterministically"), Mode->ResolveDirectPairTestSpawns(ShooterSpawn, PartnerSpawn));
	Duplicate->Destroy();

	// 실제 로그인과 같은 순서로 두 역할 PC를 선택한다. Pawn은 페어 시작 전 생성하지 않는다.
	Mode->ShooterControllerClass = LoadClass<AShooterPlayerController>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterPlayerController.BP_ShooterPlayerController_C"));
	Mode->PartnerControllerClass = APartnerPlayerController::StaticClass();
	if (!TestNotNull(TEXT("Concrete Shooter controller BP exists"), Mode->ShooterControllerClass.Get())) return false;
	APlayerController* First = Mode->SpawnPlayerController(ROLE_AutonomousProxy, FString());
	if (!TestNotNull(TEXT("First controller"), First)) return false;
	TestTrue(TEXT("First login selects Shooter controller"), First->IsA<AShooterPlayerController>());
	Mode->DirectPairTestPlayers.Add(First);
	Mode->TryStartDirectPairTest();
	TestFalse(TEXT("One player cannot start the pair"), Mode->bDirectPairTestStarted);
	APlayerController* Second = Mode->SpawnPlayerController(ROLE_AutonomousProxy, FString());
	if (!TestNotNull(TEXT("Second controller"), Second)) return false;
	TestTrue(TEXT("Second login selects Partner controller"), Second->IsA<APartnerPlayerController>());
	Mode->HandleStartingNewPlayer_Implementation(First);
	Mode->HandleStartingNewPlayer_Implementation(Second);
	TestNull(TEXT("No default Shooter pawn is spawned"), First->GetPawn());
	TestNull(TEXT("No default Partner pawn is spawned"), Second->GetPawn());

	// 저장 소유권 없는 PIE에서도 정상 페어의 완료만 통과시킨다. 실제 로그인/전투는 실행하지 않는다.
	UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
	World->SetGameInstance(GameInstance);
	GameInstance->OnWorldChanged(nullptr, World);
	GameInstance->Init();
	ON_SCOPE_EXIT { GameInstance->Shutdown(); };
	UOutlierSaveSubSystem* Saves = GameInstance->GetSubsystem<UOutlierSaveSubSystem>();
	if (!TestNotNull(TEXT("Test save subsystem"), Saves)) return false;
	const FGameplayTag RoomTag = FGameplayTag::RequestGameplayTag(TEXT("Room.Level01.1"));
	const int32 Generation = static_cast<int32>(Arena->GetGameplayGeneration());
	AddExpectedMessage(TEXT("[Checkpoint] Phase save preflight failed"),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 3, false);
	TestFalse(TEXT("Direct test cannot bypass a missing pair"),
		Mode->CommitCombatPhaseCheckpoint(RoomTag, INDEX_NONE, true, Generation));

	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	AShooterCharacter* ShooterPawn = ShooterClass
		? World->SpawnActor<AShooterCharacter>(ShooterClass, FTransform(FVector(3000.0, 0.0, 300.0))) : nullptr;
	APartnerCharacter* PartnerPawn = World->SpawnActor<APartnerCharacter>(
		APartnerCharacter::StaticClass(), FTransform(FVector(4000.0, 0.0, 300.0)));
	AOutlierPlayerState* ShooterState = World->SpawnActor<AOutlierPlayerState>();
	AOutlierPlayerState* PartnerState = World->SpawnActor<AOutlierPlayerState>();
	FObjectProperty* PawnProperty = FindFProperty<FObjectProperty>(AController::StaticClass(), TEXT("Pawn"));
	Mode->GameState = World->SpawnActor<AGameStateBase>();
	if (!TestNotNull(TEXT("Shooter pawn"), ShooterPawn) || !TestNotNull(TEXT("Partner pawn"), PartnerPawn)
		|| !TestNotNull(TEXT("Shooter state"), ShooterState) || !TestNotNull(TEXT("Partner state"), PartnerState)
		|| !TestNotNull(TEXT("Controller pawn property"), PawnProperty)
		|| !TestNotNull(TEXT("Test GameState"), Mode->GameState.Get())) return false;
	Mode->ActiveMatchPairId = 0;
	ShooterState->SetPairId(0);
	PartnerState->SetPairId(0);
	ShooterState->SetPlayerRole(EOutlierPlayerRole::Shooter);
	PartnerState->SetPlayerRole(EOutlierPlayerRole::Partner);
	ShooterState->SetOwner(First);
	PartnerState->SetOwner(Second);
	ShooterState->SetShooterCharacter(ShooterPawn);
	PartnerState->SetPartnerCharacter(PartnerPawn);
	Mode->GameState->PlayerArray.Add(ShooterState);
	Mode->GameState->PlayerArray.Add(PartnerState);
	PawnProperty->SetObjectPropertyValue_InContainer(First, ShooterPawn);
	PawnProperty->SetObjectPropertyValue_InContainer(Second, PartnerPawn);
	TestFalse(TEXT("Direct test cannot mark unfinished combat complete"),
		Mode->CommitCombatPhaseCheckpoint(RoomTag, INDEX_NONE, true, Generation));
	TestTrue(TEXT("Direct test can advance a phase without a disk save identity"),
		Mode->CommitCombatPhaseCheckpoint(RoomTag, 1, false, Generation));
	Saves->RecordCompletedEncounter(RoomTag.GetTagName());
	TestFalse(TEXT("Direct test rejects a stale gameplay generation"),
		Mode->CommitCombatPhaseCheckpoint(RoomTag, INDEX_NONE, true, Generation + 1));
	TestTrue(TEXT("Completed combat can transition without a disk save identity"),
		Mode->CommitCombatPhaseCheckpoint(RoomTag, INDEX_NONE, true, Generation));
	TestFalse(TEXT("Direct test does not create a persistent checkpoint snapshot"), Saves->HasLatestCheckpointSnapshot());
	PawnProperty->SetObjectPropertyValue_InContainer(First, nullptr);
	PawnProperty->SetObjectPropertyValue_InContainer(Second, nullptr);
	return true;
}

#endif
