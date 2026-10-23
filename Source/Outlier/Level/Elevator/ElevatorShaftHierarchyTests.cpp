#if WITH_DEV_AUTOMATION_TESTS

#include "Components/ChildActorComponent.h"
#include "Components/StaticMeshComponent.h"
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

	// 실제 BP처럼 시각 메시를 일반 Actor의 Child Actor 안에 넣어 설정 누락을 재현한다.
	const auto AddBlockingMesh = [](AActor* Owner)
	{
		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Owner);
		Owner->AddInstanceComponent(Mesh);
		if (Owner->GetRootComponent()) Mesh->SetupAttachment(Owner->GetRootComponent());
		else Owner->SetRootComponent(Mesh);
		Mesh->SetMobility(EComponentMobility::Static);
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Mesh->RegisterComponent();
		return Mesh;
	};
	UChildActorComponent* VisualActor = AddChild(WallComponent->GetChildActor(), AActor::StaticClass());
	if (!TestNotNull(TEXT("Visual child actor exists"), VisualActor->GetChildActor())) return false;
	UStaticMeshComponent* VisualMesh = AddBlockingMesh(VisualActor->GetChildActor());
	UChildActorComponent* DoorVisualActor = AddChild(Door, AActor::StaticClass());
	if (!TestNotNull(TEXT("Nested door visual actor exists"), DoorVisualActor->GetChildActor())) return false;
	UStaticMeshComponent* DoorMesh = AddBlockingMesh(DoorVisualActor->GetChildActor());
	UChildActorComponent* ProtectedBarrier = AddChild(WallComponent->GetChildActor(), ARoomCombatBarrier::StaticClass());
	if (!TestNotNull(TEXT("Protected barrier exists"), ProtectedBarrier->GetChildActor())) return false;
	UStaticMeshComponent* BarrierMesh = AddBlockingMesh(ProtectedBarrier->GetChildActor());
	WallComponent->GetChildActor()->OnConstruction(WallComponent->GetChildActor()->GetActorTransform());
	TestTrue(TEXT("Nested visual mesh has no gameplay collision"), VisualMesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
	TestTrue(TEXT("Nested visual mesh can move with its wall"), VisualMesh->Mobility == EComponentMobility::Movable);
	TestTrue(TEXT("Nested door mesh retains collision"), DoorMesh->GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics);
	TestTrue(TEXT("Barrier mesh retains collision"), BarrierMesh->GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics);
	Section->RefreshChildActors();
	Section->SetSectionVisible(false);
	TestTrue(TEXT("Hidden visual mesh still has no collision"), VisualMesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
	Section->SetSectionVisible(true);
	ProtectedBarrier->DestroyComponent();
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

	// 도착층은 레벨 Actor이며, 중첩 문이 Controller보다 먼저 BeginPlay해도 저장 ID를 받아야 한다.
	AElevatorShaftSection* ArrivalSection = World->SpawnActor<AElevatorShaftSection>();
	if (!TestNotNull(TEXT("Arrival Section exists"), ArrivalSection)) return false;
	UChildActorComponent* ArrivalWall = AddChild(ArrivalSection, AElevatorShaftWall::StaticClass());
	if (!TestNotNull(TEXT("Arrival wall exists"), ArrivalWall->GetChildActor())) return false;
	UChildActorComponent* ArrivalDoorComponent = AddChild(ArrivalWall->GetChildActor(), AInteractableDoor::StaticClass());
	AInteractableDoor* ArrivalDoor = Cast<AInteractableDoor>(ArrivalDoorComponent->GetChildActor());
	if (!TestNotNull(TEXT("Arrival door exists"), ArrivalDoor)) return false;
	Controller->ArrivalSection = ArrivalSection;
	Controller->ArrivalDoorId = TEXT("ElevatorHierarchy.Arrival");
	Controller->OnConstruction(Controller->GetActorTransform());
	TestTrue(TEXT("Controller resolves the arrival door through its level Section"), Controller->ArrivalDoor == ArrivalDoor);
	ArrivalDoor->DispatchBeginPlay();
	TestEqual(TEXT("Arrival door reads the Controller ID before save registration"), ArrivalDoor->DoorId, Controller->ArrivalDoorId);

	UChildActorComponent* ExtraArrivalDoor = AddChild(ArrivalSection, AInteractableDoor::StaticClass());
	Controller->OnConstruction(Controller->GetActorTransform());
	TestNull(TEXT("Multiple arrival doors clear the cached reference"), Controller->ArrivalDoor.Get());
	ExtraArrivalDoor->DestroyComponent();
	Controller->OnConstruction(Controller->GetActorTransform());
	TestTrue(TEXT("Reconstruction restores the unique arrival door"), Controller->ArrivalDoor == ArrivalDoor);

	ArrivalDoorComponent->DestroyComponent();
	ArrivalDoorComponent = AddChild(ArrivalWall->GetChildActor(), AInteractableDoor::StaticClass());
	ArrivalDoor = Cast<AInteractableDoor>(ArrivalDoorComponent->GetChildActor());
	if (!TestNotNull(TEXT("Replacement arrival door exists"), ArrivalDoor)) return false;
	ArrivalDoor->DoorId = TEXT("ElevatorHierarchy.ManualArrival");
	Controller->ArrivalDoorId = NAME_None;
	ArrivalDoor->DispatchBeginPlay();
	TestEqual(TEXT("Empty Controller override preserves the arrival door's own ID"),
		ArrivalDoor->DoorId, FName(TEXT("ElevatorHierarchy.ManualArrival")));
	Controller->ArrivalSection = nullptr;
	Controller->OnConstruction(Controller->GetActorTransform());
	TestNull(TEXT("Clearing the arrival Section clears its cached door"), Controller->ArrivalDoor.Get());

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
