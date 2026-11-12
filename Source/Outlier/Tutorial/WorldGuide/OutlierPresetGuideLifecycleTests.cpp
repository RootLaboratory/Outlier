#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "CoreGlobals.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "Network/OutlierArenaSubsystem.h"
#include "OutlierGameMode.h"
#include "Save/OutlierSaveSubSystem.h"
#include "TimerManager.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideManager.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideTargetComponent.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierPresetGuideLifecycleTest,
	"Outlier.Tutorial.PresetGuide.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierPresetGuideLifecycleTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("PresetGuideLifecycle")));
	if (!TestNotNull(TEXT("Isolated world"), World)) return false;
	World->AddToRoot();
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	ON_SCOPE_EXIT
	{
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	UClass* ModeClass = LoadClass<AOutlierGameMode>(nullptr,
		TEXT("/Game/Blueprints/Outlier/BP_OutlierGM.BP_OutlierGM_C"));
	AOutlierGameMode* Mode = ModeClass ? World->SpawnActor<AOutlierGameMode>(ModeClass) : nullptr;
	UOutlierArenaSubsystem* Arena = World->GetSubsystem<UOutlierArenaSubsystem>();
	AOutlierWorldGuideManager* Guides = World->SpawnActor<AOutlierWorldGuideManager>();
	if (!TestNotNull(TEXT("Production mode"), Mode) || !TestNotNull(TEXT("Arena"), Arena)
		|| !TestNotNull(TEXT("Guide manager"), Guides)) return false;

	// 스트리밍 I/O만 격리한다. 서버 Ready 진입과 실제 FIFO/취소 통보는 생산 함수를 사용한다.
	ULevelStreamingDynamic* Base = NewObject<ULevelStreamingDynamic>(World);
	ULevelStreamingDynamic* Gameplay = NewObject<ULevelStreamingDynamic>(World);
	ULevel* GameplayLevel = NewObject<ULevel>(World);
	GameplayLevel->bIsVisible = true;
	FObjectPropertyBase* Loaded = FindFProperty<FObjectPropertyBase>(ULevelStreaming::StaticClass(), TEXT("LoadedLevel"));
	Loaded->SetObjectPropertyValue_InContainer(Base, World->PersistentLevel);
	Loaded->SetObjectPropertyValue_InContainer(Gameplay, GameplayLevel);
	Arena->Arena.StreamingLevel = Base;
	Arena->GameplayStreamingLevels = { Gameplay };
	Arena->GameplaySublevels = { TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Tests/GuideGameplay.GuideGameplay"))) };
	Arena->bGameplayLevelsResolved = true;
	Arena->bInitialGameplayLevelsRequested = true;

	const FGameplayTag A = FGameplayTag::RequestGameplayTag(TEXT("UI.Info.World.LOC_TUT_010"));
	const FGameplayTag B = FGameplayTag::RequestGameplayTag(TEXT("UI.Info.World.LOC_TUT_011_1"));
	const FGameplayTag C = FGameplayTag::RequestGameplayTag(TEXT("UI.Info.World.LOC_TUT_011_4"));
	const FGameplayTag Done = FGameplayTag::RequestGameplayTag(TEXT("UI.Info.World.LOC_TUT_011_5"));
	Guides->GuideTable = NewObject<UDataTable>(Guides);
	Guides->GuideTable->RowStruct = FOutlierWorldGuideDataRow::StaticStruct();
	Guides->EventTables.Add(NewObject<UDataTable>(Guides));
	Guides->EventTables[0]->RowStruct = FOutlierEventDataRow::StaticStruct();
	for (FGameplayTag Tag : {A, B, C, Done})
	{
		FOutlierWorldGuideDataRow Row;
		Row.WidgetID = Tag;
		Guides->GuideTable->AddRow(Tag.GetTagName(), Row);
	}
	const auto Enqueue = [Guides](FGameplayTag Tag)
	{
		FOutlierWorldGuideContext Context;
		Context.WidgetID = Tag;
		return Guides->EnqueueGuide(Context);
	};
	Enqueue(Done);
	Guides->CompleteWidget(Done);
	Guides->CompletedTargets.FindOrAdd(Done).Add(TEXT("Completed.Target.Path"));
	AActor* TargetOwner = World->SpawnActor<AActor>();
	UOutlierWorldGuideTargetComponent* Target = NewObject<UOutlierWorldGuideTargetComponent>(TargetOwner);
	Target->WidgetID = A;
	TargetOwner->AddInstanceComponent(Target);
	Target->RegisterComponent();
	Guides->RegisterTarget(Target);
	int32 Cancelled = 0;
	Guides->OnGuideFinished.AddLambda(
		[&](const FOutlierWorldGuideContext&, bool bCompleted)
		{
			TestFalse(TEXT("Preset clear is cancellation, never completion"), bCompleted);
			++Cancelled;
			TestFalse(TEXT("Cancellation cannot enqueue a chain"), Enqueue(C));
		});
	Enqueue(A);
	Enqueue(B);
	Enqueue(C);
	TestEqual(TEXT("Old active guide has two pending successors"), Guides->GetPendingCount(), 2);
	TestTrue(TEXT("Old guide is presented locally"), Target->IsGuideVisible());
	TestFalse(TEXT("Old guide owns an outline"), Guides->OutlinedTargets.IsEmpty());
	TestFalse(TEXT("Failed preset preflight is rejected"), Mode->RequestPresetRespawn(nullptr, TEXT("Level2")));
	TestEqual(TEXT("Failed preflight preserves the active guide"), Guides->ActiveContext.WidgetID, A);

	const auto PrepareReady = [&](bool bPreset, uint32 Generation)
	{
		Mode->bArenaReloadInProgress = true;
		Mode->bServerArenaReloadReady = false;
		Mode->PendingGameplayGeneration = Generation;
		Mode->bClearPresetGuidesAfterReload = bPreset;
		// 실제 원격 페어 준비는 별도 회귀가 담당한다. 여기서는 Possess 경계 직전에 멈춘다.
		Mode->bListenGuestDisconnected = true;
	};
	PrepareReady(false, 5);
	Mode->HandleServerArenaGameplayReady(5);
	TestEqual(TEXT("Normal Continue does not clear guides"), Cancelled, 0);
	PrepareReady(true, 6);
	Mode->HandleServerArenaGameplayReady(5);
	TestEqual(TEXT("Stale Ready cannot clear the next preset"), Cancelled, 0);
	Mode->HandleServerArenaGameplayReady(6);
	TestEqual(TEXT("Preset cancels active and all pending guides"), Cancelled, 3);
	TestEqual(TEXT("Preset clears active guide"), Guides->ActiveContext.RequestID, 0);
	TestEqual(TEXT("Preset clears pending guides"), Guides->GetPendingCount(), 0);
	TestFalse(TEXT("Preset hides the local guide"), Target->IsGuideVisible());
	TestTrue(TEXT("Preset releases its outlines"), Guides->OutlinedTargets.IsEmpty());
	TestTrue(TEXT("Completed widgets survive"), Guides->CompletedWidgetIDs.Contains(Done));
	TestTrue(TEXT("Completed target history survives"), Guides->CompletedTargets.FindRef(Done).Contains(TEXT("Completed.Target.Path")));
	Enqueue(B);
	Mode->HandleServerArenaGameplayReady(6);
	TestEqual(TEXT("New selected-level guide survives duplicate Ready"), Guides->ActiveContext.WidgetID, B);

	PrepareReady(true, 7);
	Enqueue(C); // 리로드 초기화 중 이전 레벨 안내가 다시 등록되는 경우도 마지막 경계에서 함께 정리한다.
	Guides->bChangingQueue = true;
	Mode->HandleServerArenaGameplayReady(7);
	TestFalse(TEXT("Ignored clear must not publish server readiness"), Mode->bServerArenaReloadReady);
	Guides->bChangingQueue = false;
	World->GetTimerManager().Tick(0.01f);
	TestEqual(TEXT("Deferred clear runs after queue mutation ends"), Guides->ActiveContext.RequestID, 0);
	TestTrue(TEXT("Deferred clear unlocks readiness"), Mode->bServerArenaReloadReady);

	// 취소 콜백이 전환을 종료해도 Ready/Possess로 되돌아가지 않는다.
	Enqueue(A);
	Guides->OnGuideFinished.AddLambda([&](const FOutlierWorldGuideContext&, bool)
	{
		Mode->ClearArenaGameplayReloadDelegates();
		Mode->bArenaReloadInProgress = false;
	});
	PrepareReady(true, 8);
	Mode->HandleServerArenaGameplayReady(8);
	TestFalse(TEXT("Cancelled reload cannot become server-ready"), Mode->bServerArenaReloadReady);
	Guides->OnGuideFinished.Clear();
	Enqueue(B);
	PrepareReady(true, 9);
	Guides->bChangingQueue = true;
	Mode->HandleServerArenaGameplayReady(9);
	Mode->ClearArenaGameplayReloadDelegates();
	Mode->bArenaReloadInProgress = false;
	Guides->bChangingQueue = false;
	{
		TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
		++GFrameCounter;
		World->GetTimerManager().Tick(0.01f);
	}
	TestEqual(TEXT("Terminated reload cancels its deferred clear"), Guides->ActiveContext.WidgetID, B);
	Guides->ClearGuides();
	Guides->Destroy();
	PrepareReady(true, 10);
	Mode->HandleServerArenaGameplayReady(10);
	TestTrue(TEXT("Empty first entry without a manager is safe"), Mode->bServerArenaReloadReady);
	Mode->ClearArenaGameplayReloadDelegates();
	Mode->bArenaReloadInProgress = false;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierTutorialFiredUnionTest,
	"Outlier.Tutorial.Checkpoint.FiredUnion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierTutorialFiredUnionTest::RunTest(const FString& Parameters)
{
	UOutlierSaveSubSystem* Saves = NewObject<UOutlierSaveSubSystem>(NewObject<UGameInstance>(GEngine));
	const FGameplayTag A = FGameplayTag::RequestGameplayTag(TEXT("Event.Tutorial.Shooter.Move"));
	const FGameplayTag B = FGameplayTag::RequestGameplayTag(TEXT("Event.Tutorial.Shooter.Look"));
	const FGameplayTag C = FGameplayTag::RequestGameplayTag(TEXT("Event.Tutorial.Partner.Move"));
	Saves->RestoreFiredTutorialEvents({A, B});
	Saves->MarkTutorialEventFired(C);
	Saves->RestoreFiredTutorialEvents({A, B});
	TestEqual(TEXT("Restore retains post-checkpoint issuance"), Saves->GetFiredTutorialEvents().Num(), 3);
	TestFalse(TEXT("Already-issued event cannot be issued twice"), Saves->MarkTutorialEventFired(C));
	UOutlierSaveSubSystem* FreshProcess = NewObject<UOutlierSaveSubSystem>(NewObject<UGameInstance>(GEngine));
	FreshProcess->RestoreFiredTutorialEvents({A, B});
	TestFalse(TEXT("A fresh process has only persisted issuance"), FreshProcess->HasFiredTutorialEvent(C));
	return true;
}

#endif
