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

	const uint32 NextGeneration = Arena->ReserveGameplayGeneration();
	if (TestTrue(TEXT("A new gameplay generation is reserved"), NextGeneration != 0))
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
