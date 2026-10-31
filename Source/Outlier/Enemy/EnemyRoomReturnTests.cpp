#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Enemy/EnemyBase.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomTagComponent.h"
#include "Room/RoomVolume.h"

namespace
{
UE_DEFINE_GAMEPLAY_TAG_STATIC(ReturnHomeTag, "Room.ReturnTest01.1.1");

struct FRoomReturnWorld
{
	UWorld* World;
	UGameInstance* GameInstance;
	URoomCombatSubsystem* Combat;
	ARoomVolume* Home;
	AEnemyBase* Enemy;

	FRoomReturnWorld()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("RoomReturnTest")));
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		GameInstance = NewObject<UGameInstance>(GEngine);
		World->SetGameInstance(GameInstance);
		GameInstance->Init();
		World->SetGameMode(FURL());
		World->InitializeActorsForPlay(FURL());
		Combat = World->GetSubsystem<URoomCombatSubsystem>();
		URoomCombatDefinition* Definition = NewObject<URoomCombatDefinition>(World);
		FRoomCombatRoomDefinition& Entry = Definition->RoomDefinitions.AddDefaulted_GetRef();
		Entry.RoomTag = ReturnHomeTag;
		Entry.CombatPhases.AddDefaulted_GetRef().Waves.AddDefaulted();
		Combat->SetCombatDefinitionForTesting(Definition);
		Home = World->SpawnActor<ARoomVolume>();
		CastChecked<UBoxComponent>(Home->GetRootComponent())->SetBoxExtent(FVector(400));
		Combat->RegisterRoom(Home, ReturnHomeTag);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Enemy = World->SpawnActor<AEnemyBase>(AEnemyBase::StaticClass(), FTransform(FVector(800, 0, 0)), Params);
		Enemy->GetRoomTagComp()->AssignDefaultRoomTag(ReturnHomeTag);
	}

	~FRoomReturnWorld()
	{
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(false);
		GameInstance->Shutdown();
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}

	bool StartReturn() const
	{
		Enemy->EnterAlertFromPerception(FVector(900, 0, 0));
		return Enemy->CommitAlertToNonCombat();
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyRoomReturnArrivalTest, "Outlier.Enemy.RoomReturn.ActualHomeArrival",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyRoomReturnArrivalTest::RunTest(const FString& Parameters)
{
	FRoomReturnWorld Fixture;
	AEnemyBase* Enemy = Fixture.Enemy;
	TestTrue(TEXT("Alert clear is accepted outside home"), Fixture.StartReturn());
	TestEqual(TEXT("Outside home enters Returning"), Enemy->GetCombatState(), EEnemyCombatState::Returning);
	TestEqual(TEXT("Fallback current tag is still home"), Enemy->GetCurrentRoomTag(), ReturnHomeTag.GetTag());
	TestFalse(TEXT("Home tag alone cannot complete return"), Enemy->CompleteReturnHome());
	FVector Destination;
	TestTrue(TEXT("Return has an actual home destination"), Enemy->GetReturnHomeDestination(Destination));
	TestEqual(TEXT("Destination selection never teleports"), Enemy->GetActorLocation(), FVector(800, 0, 0));
	Enemy->SetActorLocation(FVector(399, 0, 0));
	TestFalse(TEXT("Capsule crossing home boundary is not arrival"), Enemy->CompleteReturnHome());
	Enemy->SetActorLocation(Destination);
	TestTrue(TEXT("Actual capsule arrival completes return"), Enemy->CompleteReturnHome());
	TestEqual(TEXT("Patrol resumes only after arrival"), Enemy->GetCombatState(), EEnemyCombatState::NonCombat);
	TestFalse(TEXT("Completion cannot repeat"), Enemy->CompleteReturnHome());
	TestEqual(TEXT("Movement never changes original ownership"), Enemy->GetDefaultRoomTag(), ReturnHomeTag.GetTag());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyRoomReturnInterruptionTest, "Outlier.Enemy.RoomReturn.InterruptionAndMissingHome",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyRoomReturnInterruptionTest::RunTest(const FString& Parameters)
{
	FRoomReturnWorld Fixture;
	AEnemyBase* Enemy = Fixture.Enemy;
	TestTrue(TEXT("Start return"), Fixture.StartReturn());
	Enemy->EnterStun();
	Enemy->RestoreStateAfterStun();
	TestEqual(TEXT("Stun restores unfinished return"), Enemy->GetCombatState(), EEnemyCombatState::Returning);
	Enemy->EnterAlertFromPerception(FVector(1000, 0, 0));
	TestEqual(TEXT("Rediscovery interrupts return through existing alert flow"), Enemy->GetCombatState(), EEnemyCombatState::Alert);
	TestFalse(TEXT("Old arrival cannot clear a fresh alert"), Enemy->CompleteReturnHome());
	TestTrue(TEXT("Alert timeout starts another return"), Enemy->CommitAlertToNonCombat());
	Enemy->EnterStun();
	Enemy->EnterAlertFromPerception(FVector(1100, 0, 0));
	Enemy->RestoreStateAfterStun();
	TestEqual(TEXT("Rediscovery during stun supersedes return"), Enemy->GetCombatState(), EEnemyCombatState::Alert);
	Fixture.Combat->UnregisterRoom(Fixture.Home);
	TestTrue(TEXT("Missing home does not block alert clear"), Enemy->CommitAlertToNonCombat());
	TestEqual(TEXT("Missing home waits in Returning"), Enemy->GetCombatState(), EEnemyCombatState::Returning);
	FVector Destination;
	TestFalse(TEXT("Missing home has no unsafe fallback destination"), Enemy->GetReturnHomeDestination(Destination));
	Enemy->SetActorLocation(FVector::ZeroVector);
	TestFalse(TEXT("Missing home cannot complete return"), Enemy->CompleteReturnHome());
	Fixture.Combat->RegisterRoom(Fixture.Home, ReturnHomeTag);
	TestTrue(TEXT("Registration recovery restores destination"), Enemy->GetReturnHomeDestination(Destination));
	Enemy->SetEnemyPossessed(true);
	TestFalse(TEXT("Possession suppresses stale return completion"), Enemy->CompleteReturnHome());
	TestFalse(TEXT("Possession suppresses destination selection"), Enemy->GetReturnHomeDestination(Destination));
	return true;
}

#endif
