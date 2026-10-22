#if WITH_DEV_AUTOMATION_TESTS

#include "Components/ChildActorComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Interaction/InteractableDoor.h"
#include "Level/Elevator/ElevatorShaftController.h"
#include "Level/Elevator/ElevatorShaftSection.h"
#include "Level/Elevator/ElevatorShaftWall.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Room/RoomCombatBarrier.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElevatorShaftChildActorEntranceTest,
	"Outlier.Elevator.Hierarchy.ChildActorEntrance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElevatorShaftChildActorEntranceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("ElevatorHierarchyTest")));
	if (!TestNotNull(TEXT("Hierarchy test world exists"), World))
	{
		return false;
	}
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};

	// BP와 같은 소유 계층을 만든다. BeginPlay 없이 Construction의 자동 연결을 검증한다.
	const auto AddChild = [](AActor* Owner, UClass* Class)
	{
		UChildActorComponent* Component = NewObject<UChildActorComponent>(Owner);
		Owner->AddInstanceComponent(Component);
		Component->SetupAttachment(Owner->GetRootComponent());
		Component->SetChildActorClass(Class);
		Component->RegisterComponent();
		return Component;
	};
	AElevatorShaftController* Controller = World->SpawnActor<AElevatorShaftController>();
	if (!TestNotNull(TEXT("Controller exists"), Controller))
	{
		return false;
	}
	UChildActorComponent* SectionComponent = AddChild(Controller, AElevatorShaftSection::StaticClass());
	AElevatorShaftSection* Section = Cast<AElevatorShaftSection>(SectionComponent->GetChildActor());
	if (!TestNotNull(TEXT("Entrance Section child exists"), Section))
	{
		return false;
	}
	UChildActorComponent* WallComponent = AddChild(Section, AElevatorShaftWall::StaticClass());
	if (!TestNotNull(TEXT("Nested entrance wall exists"), WallComponent->GetChildActor()))
	{
		return false;
	}
	UChildActorComponent* DoorComponent = AddChild(WallComponent->GetChildActor(), AInteractableDoor::StaticClass());
	AInteractableDoor* Door = Cast<AInteractableDoor>(DoorComponent->GetChildActor());
	if (!TestNotNull(TEXT("Nested gameplay door exists"), Door))
	{
		return false;
	}
	Controller->OnConstruction(Controller->GetActorTransform());
	TestTrue(TEXT("Controller resolves its own Section"), Controller->EntranceSection == Section);
	TestTrue(TEXT("Controller resolves a nested wall door without Section BeginPlay"), Controller->EntranceDoor == Door);

	Controller->EntranceDoorId = TEXT("ElevatorHierarchy.Entrance");
	Door->DispatchBeginPlay();
	TestEqual(TEXT("Nested door inherits the Controller instance save ID before registration"),
		Door->DoorId, Controller->EntranceDoorId);
	AInteractableDoor* StandaloneDoor = World->SpawnActor<AInteractableDoor>();
	if (!TestNotNull(TEXT("Standalone door exists"), StandaloneDoor)) return false;
	StandaloneDoor->DoorId = TEXT("Standalone.Entrance");
	StandaloneDoor->DispatchBeginPlay();
	TestEqual(TEXT("Standalone door retains its manually assigned save ID"),
		StandaloneDoor->DoorId, FName(TEXT("Standalone.Entrance")));

	// 배치된 Controller마다 다른 태그를 쓰며, 차단막은 중간 Section/Wall의 BeginPlay를 기다리지 않는다.
	Controller->CombatRoomTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")));
	UChildActorComponent* BarrierComponent = AddChild(WallComponent->GetChildActor(), ARoomCombatBarrier::StaticClass());
	ARoomCombatBarrier* Barrier = Cast<ARoomCombatBarrier>(BarrierComponent->GetChildActor());
	if (!TestNotNull(TEXT("Nested barrier exists"), Barrier)) return false;
	Barrier->DispatchBeginPlay();
	TestTrue(TEXT("Nested barrier inherits the Controller instance combat tag"),
		Barrier->ServesRoom(Controller->CombatRoomTag));
	ARoomCombatBarrier* StandaloneBarrier = World->SpawnActor<ARoomCombatBarrier>();
	if (!TestNotNull(TEXT("Standalone barrier exists"), StandaloneBarrier)) return false;
	StandaloneBarrier->DispatchBeginPlay();
	TestFalse(TEXT("Standalone barrier does not inherit another Controller tag"),
		StandaloneBarrier->ServesRoom(Controller->CombatRoomTag));

	UChildActorComponent* ExtraDoor = AddChild(Section, AInteractableDoor::StaticClass());
	Controller->OnConstruction(Controller->GetActorTransform());
	TestNull(TEXT("Multiple doors are rejected"), Controller->EntranceDoor.Get());
	ExtraDoor->DestroyComponent();
	Controller->OnConstruction(Controller->GetActorTransform());
	TestTrue(TEXT("Reconstruction restores the unique door"), Controller->EntranceDoor == Door);

	UChildActorComponent* ExtraSection = AddChild(Controller, AElevatorShaftSection::StaticClass());
	Controller->OnConstruction(Controller->GetActorTransform());
	TestNull(TEXT("Multiple Sections are rejected"), Controller->EntranceSection.Get());
	TestNull(TEXT("Ambiguous Section clears the door"), Controller->EntranceDoor.Get());
	ExtraSection->DestroyComponent();
	Controller->OnConstruction(Controller->GetActorTransform());
	TestTrue(TEXT("Reconstruction restores the unique Section"), Controller->EntranceSection == Section);

	DoorComponent->DestroyComponent();
	Controller->OnConstruction(Controller->GetActorTransform());
	TestNull(TEXT("Removing the door clears the cached reference"), Controller->EntranceDoor.Get());
	SectionComponent->DestroyComponent();
	Controller->OnConstruction(Controller->GetActorTransform());
	TestNull(TEXT("Removing the Section clears the cached reference"), Controller->EntranceSection.Get());
	return true;
}

#endif
