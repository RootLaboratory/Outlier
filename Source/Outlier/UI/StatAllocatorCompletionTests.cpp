#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Network/OutlierArenaSubsystem.h"
#include "OutlierPlayerState.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FStatAllocatorCompletionTest,
	"Outlier.UI.StatAllocator.Completion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStatAllocatorCompletionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FName WorldName = MakeUniqueObjectName(
		nullptr, UWorld::StaticClass(), NAME_None, EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Stat Allocator test world is created"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}

	World->AddToRoot();
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	const auto CleanupWorld = [World]()
	{
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};

	UOutlierArenaSubsystem* Arena = World->GetSubsystem<UOutlierArenaSubsystem>();
	AOutlierPlayerState* Shooter = World->SpawnActor<AOutlierPlayerState>();
	AOutlierPlayerState* Partner = World->SpawnActor<AOutlierPlayerState>();
	if (!TestNotNull(TEXT("Arena subsystem is available"), Arena)
		|| !TestNotNull(TEXT("Shooter PlayerState is spawned"), Shooter)
		|| !TestNotNull(TEXT("Partner PlayerState is spawned"), Partner))
	{
		CleanupWorld();
		return false;
	}

	Shooter->SetPairId(1);
	Partner->SetPairId(1);
	Shooter->SetPlayerRole(EOutlierPlayerRole::Shooter);
	Partner->SetPlayerRole(EOutlierPlayerRole::Partner);
	const uint32 Generation = Arena->GetGameplayGeneration();
	int32 ShooterCompletionCount = 0;
	Shooter->OnStatAllocatorUICompleted.AddLambda(
		[&ShooterCompletionCount](AOutlierPlayerState*, uint32)
		{
			++ShooterCompletionCount;
		});

	Shooter->ReportStatAllocatorUIOpened(Generation);
	Shooter->ReportStatAllocatorUIClosed(Generation);
	TestFalse(TEXT("UI signals before suit acquisition are rejected"),
		Shooter->IsStatAllocatorUICompletedForGeneration(Generation));

	Shooter->SetAcquiredSuit(true);
	Partner->SetAcquiredSuit(true);
	// 준비 취소는 구매 상태나 실제 UI 완료와 독립적이다.
	Shooter->AddNode(7);
	Shooter->AddActivatedUpgradeNode(EOutlierUpgradeRole::Shooter, TEXT("PurchasedBeforeWaiting"));
	Shooter->BeginStatAllocatorSession(Partner);
	const uint32 Session = Shooter->GetStatAllocatorSessionId();
	Shooter->RequestStatAllocatorExitPending(Session, Generation, true);
	Shooter->RequestStatAllocatorExitPending(Session, Generation, false);
	TestFalse(TEXT("Cancel clears only the requester's readiness"), Shooter->IsStatAllocatorExitPending());
	TestEqual(TEXT("Cancel preserves remaining currency"), Shooter->GetNodeCount(), 7);
	TestTrue(TEXT("Cancel preserves purchased upgrades"), Shooter->GetActivatedUpgradeNodeIds(EOutlierUpgradeRole::Shooter).Contains(TEXT("PurchasedBeforeWaiting")));
	TestFalse(TEXT("Cancel does not complete the UI"), Shooter->IsStatAllocatorUICompletedForGeneration(Generation));
	Shooter->BeginStatAllocatorSession(Partner);
	TestEqual(TEXT("Duplicate open preserves the current session"), Shooter->GetStatAllocatorSessionId(), Session);
	Partner->RequestStatAllocatorExitPending(Session, Generation, true);
	TestTrue(TEXT("Partner readiness remains set"), Partner->IsStatAllocatorExitPending());
	TestFalse(TEXT("Cancel before partner readiness prevents finalization"), Shooter->IsStatAllocatorExitFinalized());
	Shooter->RequestStatAllocatorExitPending(Session, Generation, true);
	TestTrue(TEXT("Both ready finalizes Shooter"), Shooter->IsStatAllocatorExitFinalized());
	TestTrue(TEXT("Both ready finalizes Partner"), Partner->IsStatAllocatorExitFinalized());
	Shooter->RequestStatAllocatorExitPending(Session, Generation, false);
	TestTrue(TEXT("Late cancel cannot undo finalization"), Shooter->IsStatAllocatorExitPending());
	Shooter->SetStatAllocatorExitPending(false);
	TestTrue(TEXT("Unversioned host setter cannot undo finalization"), Shooter->IsStatAllocatorExitPending());
	Shooter->BeginStatAllocatorSession(Partner);
	const uint32 NextSession = Shooter->GetStatAllocatorSessionId();
	TestTrue(TEXT("Reopening creates a distinct session"), NextSession != Session);
	Shooter->RequestStatAllocatorExitPending(Session, Generation, true);
	TestFalse(TEXT("Old session request is rejected"), Shooter->IsStatAllocatorExitPending());
	Shooter->RequestStatAllocatorExitPending(NextSession, Generation + 1, true);
	TestFalse(TEXT("Wrong generation request is rejected"), Shooter->IsStatAllocatorExitPending());
	Shooter->ReportStatAllocatorUIClosed(Generation);
	TestFalse(TEXT("Closed without a matching Opened is rejected"),
		Shooter->IsStatAllocatorUICompletedForGeneration(Generation));

	Shooter->ReportStatAllocatorUIOpened(Generation);
	TestFalse(TEXT("Opened and exit-pending alone do not complete the UI"),
		Shooter->IsStatAllocatorUICompletedForGeneration(Generation));
	Shooter->SetStatAllocatorExitPending(true);
	TestFalse(TEXT("Exit-pending remains separate from actual UI completion"),
		Shooter->IsStatAllocatorUICompletedForGeneration(Generation));

	Shooter->ReportStatAllocatorUIClosed(Generation);
	TestTrue(TEXT("Shooter completes after Opened then Closed"),
		Shooter->IsStatAllocatorUICompletedForGeneration(Generation));
	TestFalse(TEXT("Partner remains incomplete"),
		Partner->IsStatAllocatorUICompletedForGeneration(Generation));
	Shooter->ReportStatAllocatorUIOpened(Generation);
	Shooter->ReportStatAllocatorUIClosed(Generation);
	TestEqual(TEXT("Duplicate UI signals notify only once"), ShooterCompletionCount, 1);

	Partner->ReportStatAllocatorUIOpened(Generation);
	Partner->ReportStatAllocatorUIClosed(Generation);
	TestTrue(TEXT("Partner completes independently"),
		Partner->IsStatAllocatorUICompletedForGeneration(Generation));
	Shooter->SetAcquiredSuit(false);
	TestFalse(TEXT("Losing the suit clears the completed condition"),
		Shooter->IsStatAllocatorUICompletedForGeneration(Generation));
	Shooter->SetAcquiredSuit(true);

	// UI 격리 검사는 현재 세대만 주입한다. 실제 예약/확정은 Arena 전환 자동화에서 검증한다.
	const uint32 NextGeneration = ++Arena->GameplayGeneration;
	if (TestTrue(TEXT("A new gameplay generation is supplied"), NextGeneration != 0))
	{
		TestFalse(TEXT("Previous completion is not valid in the next generation"),
			Shooter->IsStatAllocatorUICompletedForGeneration(NextGeneration));
		Shooter->ReportStatAllocatorUIOpened(Generation);
		Shooter->ReportStatAllocatorUIClosed(Generation);
		TestFalse(TEXT("Late signals from the previous generation are rejected"),
			Shooter->IsStatAllocatorUICompletedForGeneration(NextGeneration));
		Shooter->ReportStatAllocatorUIClosed(NextGeneration);
		TestFalse(TEXT("The new generation still requires Opened first"),
			Shooter->IsStatAllocatorUICompletedForGeneration(NextGeneration));
		Shooter->ReportStatAllocatorUIOpened(NextGeneration);
		Shooter->ReportStatAllocatorUIClosed(NextGeneration);
		TestTrue(TEXT("A fresh UI cycle completes in the new generation"),
			Shooter->IsStatAllocatorUICompletedForGeneration(NextGeneration));
		TestEqual(TEXT("Each generation notifies once"), ShooterCompletionCount, 2);
	}

	CleanupWorld();
	return true;
}

#endif
