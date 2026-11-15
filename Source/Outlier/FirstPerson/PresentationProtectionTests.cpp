#if WITH_DEV_AUTOMATION_TESTS

#include "FirstPerson/FirstPersonCharacter.h"
#include "Enemy/EnemyTargetRules.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Network/OutlierArenaSubsystem.h"

namespace
{
struct FPresentationProtectionTestWorld
{
	UWorld* World = nullptr;
	UGameInstance* Instance = nullptr;
	FPresentationProtectionTestWorld()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("PresentationProtection")));
		if (!World) return;
		World->AddToRoot();
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		Instance = NewObject<UGameInstance>(GEngine);
		World->SetGameInstance(Instance);
		Instance->Init();
		World->CreateAISystem();
		FURL URL;
		URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
		World->SetGameMode(URL);
		World->InitializeActorsForPlay(URL);
		World->BeginPlay();
	}
	~FPresentationProtectionTestWorld()
	{
		if (!World) return;
		World->BeginTearingDown();
		if (Instance) Instance->Shutdown();
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		World->RemoveFromRoot();
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPresentationProtectionOwnershipTest,
	"Outlier.Presentation.Protection.OwnershipAndOverlap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPresentationProtectionOwnershipTest::RunTest(const FString& Parameters)
{
	FPresentationProtectionTestWorld Scope;
	if (!TestNotNull(TEXT("World"), Scope.World)) return false;
	auto* Pawn = Scope.World->SpawnActor<AFirstPersonCharacter>();
	auto* OwnerA = Scope.World->SpawnActor<AActor>();
	auto* OwnerB = Scope.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Pawn"), Pawn)) return false;
	const FGuid First = FGuid::NewGuid();
	const FGuid Second = FGuid::NewGuid();
	TestFalse(TEXT("Invalid execution is rejected"), Pawn->AcquirePresentationProtection(OwnerA, FGuid()));
	TestTrue(TEXT("First presentation acquires protection"), Pawn->AcquirePresentationProtection(OwnerA, First));
	TestTrue(TEXT("Duplicate acquisition is idempotent"), Pawn->AcquirePresentationProtection(OwnerA, First));
	TestFalse(TEXT("Another owner cannot steal the same execution"), Pawn->AcquirePresentationProtection(OwnerB, First));
	TestTrue(TEXT("Overlapping presentation acquires independently"), Pawn->AcquirePresentationProtection(OwnerB, Second));
	TestTrue(TEXT("AI excludes protected target"), OutlierEnemyTargetRules::IsUnavailable(Pawn));
	Pawn->ReleasePresentationProtection(OwnerB, First);
	TestTrue(TEXT("Foreign release preserves protection"), Pawn->IsPresentationProtected());
	Pawn->ReleasePresentationProtection(OwnerA, First);
	Pawn->ReleasePresentationProtection(OwnerA, First);
	TestTrue(TEXT("Remaining presentation keeps target protected"), Pawn->IsPresentationProtected());
	Pawn->ReleasePresentationProtection(OwnerB, Second);
	TestFalse(TEXT("Final release restores target eligibility"), Pawn->IsPresentationProtected());
	TestFalse(TEXT("No unrelated availability restriction was injected"), OutlierEnemyTargetRules::IsUnavailable(Pawn));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPresentationProtectionLifetimeTest,
	"Outlier.Presentation.Protection.OwnerAndGenerationLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPresentationProtectionLifetimeTest::RunTest(const FString& Parameters)
{
	FPresentationProtectionTestWorld Scope;
	if (!Scope.World) return false;
	auto* Pawn = Scope.World->SpawnActor<AFirstPersonCharacter>();
	auto* Owner = Scope.World->SpawnActor<AActor>();
	if (!Pawn || !Owner) return false;
	const FGuid Old = FGuid::NewGuid();
	Pawn->AcquirePresentationProtection(Owner, Old);
	Owner->Destroy();
	TestFalse(TEXT("Ended owner cannot leave an orphaned protection"), Pawn->IsPresentationProtected());
	Owner = Scope.World->SpawnActor<AActor>();
	Pawn->AcquirePresentationProtection(Owner, Old);
	Scope.World->GetSubsystem<UOutlierArenaSubsystem>()->OnArenaGameplayReloadStarted.Broadcast(1);
	TestFalse(TEXT("Reload ends all old presentation ownership"), Pawn->IsPresentationProtected());
	const FGuid Current = FGuid::NewGuid();
	APlayerController* Controller = Scope.World->SpawnActor<APlayerController>();
	if (!Controller) return false;
	Controller->Possess(Pawn);
	Pawn->AcquirePresentationProtection(Owner, Current);
	Pawn->ReleasePresentationProtection(Owner, Old);
	TestTrue(TEXT("Late old release cannot end the new presentation"), Pawn->IsPresentationProtected());
	Controller->UnPossess();
	TestFalse(TEXT("A pawn leaving its participant context drops old ownership"), Pawn->IsPresentationProtected());
	Pawn->ReleasePresentationProtection(Owner, Current);
	TestFalse(TEXT("Delayed cleanup after pawn replacement is idempotent"), Pawn->IsPresentationProtected());
	return true;
}

#endif
