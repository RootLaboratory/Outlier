#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Drone/FlightPathfinding.h"
#include "Enemy/EnemyBase.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomTagComponent.h"
#include "Room/RoomVolume.h"
#include "UObject/UnrealType.h"

namespace
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(MovementHome, "Room.MovementBounds01.1.1");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(MovementSibling, "Room.MovementBounds01.1.2");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(MovementOther, "Room.MovementBounds01.2.1");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(MovementLegacy, "Room.MovementBoundsLegacy.1");

	struct FMovementBoundsWorld
	{
		UWorld* World;
		UGameInstance* GameInstance;
		URoomCombatSubsystem* Combat;
		URoomCombatDefinition* Definition;

		FMovementBoundsWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false,
				MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("MovementBoundsTest")));
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			GameInstance = NewObject<UGameInstance>(GEngine);
			World->SetGameInstance(GameInstance);
			GameInstance->Init();
			World->SetGameMode(FURL());
			World->InitializeActorsForPlay(FURL());
			Combat = World->GetSubsystem<URoomCombatSubsystem>();
			Definition = NewObject<URoomCombatDefinition>(World);
			Combat->SetCombatDefinitionForTesting(Definition);
		}

		~FMovementBoundsWorld()
		{
			GEngine->ShutdownWorldNetDriver(World);
			World->DestroyWorld(false);
			GameInstance->Shutdown();
			World->SetPhysicsScene(nullptr);
			GEngine->DestroyWorldContext(World);
			World->RemoveFromRoot();
		}

		ARoomVolume* AddRoom(FGameplayTag Tag, FVector Center, FVector Extent)
		{
			FRoomCombatRoomDefinition& Entry = Definition->RoomDefinitions.AddDefaulted_GetRef();
			Entry.RoomTag = Tag;
			Entry.CombatPhases.AddDefaulted_GetRef().Waves.AddDefaulted();
			ARoomVolume* Room = World->SpawnActor<ARoomVolume>();
			*FindFProperty<FStructProperty>(ARoomVolume::StaticClass(), TEXT("RoomTag"))
				->ContainerPtrToValuePtr<FGameplayTag>(Room) = Tag;
			Room->SetActorLocation(Center);
			CastChecked<UBoxComponent>(Room->GetRootComponent())->SetBoxExtent(Extent);
			Combat->RegisterRoom(Room, Tag);
			return Room;
		}

		AEnemyBase* AddEnemy(FGameplayTag Tag)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AEnemyBase* Enemy = World->SpawnActor<AEnemyBase>(AEnemyBase::StaticClass(), FTransform::Identity, Params);
			Enemy->GetRoomTagComp()->AssignDefaultRoomTag(Tag);
			return Enemy;
		}
	};

	void SetMovementCombatState(AEnemyBase* Enemy, EEnemyCombatState State)
	{
		// 공간 문맥의 상태별 정책만 분리해서 검사한다. 전투 진입은 별도 통합 테스트가 담당한다.
		*FindFProperty<FEnumProperty>(AEnemyBase::StaticClass(), TEXT("CombatState"))
			->ContainerPtrToValuePtr<EEnemyCombatState>(Enemy) = State;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomMovementUnionTest, "Outlier.Room.MovementBounds.UnionGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomMovementUnionTest::RunTest(const FString& Parameters)
{
	FMovementBoundsWorld Fixture;
	ARoomVolume* A = Fixture.AddRoom(MovementHome, FVector(-100, 0, 0), FVector(100));
	ARoomVolume* B = Fixture.AddRoom(MovementSibling, FVector(100, 0, 0), FVector(100));
	FFlightPathRequest Request;
	Request.bRequireRoom = true;
	Request.Room = A;
	Request.AllowedRooms = {A, B};
	const FVector Extent(10, 10, 20);
	TestTrue(TEXT("Capsule box can straddle touching rooms"),
		FlightPathfinding::IsInsideMovementRooms(Request, FVector::ZeroVector, Extent));
	TestTrue(TEXT("Whole segment crosses touching room boundary"),
		FlightPathfinding::IsSegmentInsideMovementRooms(Request, FVector(-150, 0, 0), FVector(150, 0, 0), Extent));
	FVector Clamped;
	TestTrue(TEXT("Union-contained candidate clamps successfully"),
		FlightPathfinding::ClampToMovementRooms(Request, FVector::ZeroVector, Extent, Clamped));
	TestEqual(TEXT("Clamp preserves union-contained straddling position"), Clamped, FVector::ZeroVector);
	Request.AllowedRooms.Reset();
	TestFalse(TEXT("Legacy single-room request still excludes boundary straddling"),
		FlightPathfinding::IsInsideMovementRooms(Request, FVector::ZeroVector, Extent));
	Request.AllowedRooms = {A, B};
	// MoveComponent는 극소 이동을 생략한다. 먼 위치를 거쳐 실제 틈이 있는 변환을 만든다.
	B->SetActorLocation(FVector(110, 0, 0));
	B->SetActorLocation(FVector(100.00001, 0, 0));
	TestTrue(TEXT("Thin-gap fixture really leaves a positive physical gap"), B->GetActorLocation().X > 100.0);
	TestFalse(TEXT("Thin gap is not bridged by samples or tolerance"),
		FlightPathfinding::IsSegmentInsideMovementRooms(Request, FVector(-150, 0, 0), FVector(150, 0, 0), Extent));
	B->SetActorLocation(FVector(110, 0, 0));
	B->SetActorLocation(FVector(100, 0, 0));
	// 회전 변환의 부동소수점 틈과 분리하여 OBB 합집합 계산을 검사한다.
	// 맞닿음 자체는 위의 축 정렬 Volume 두 개로 별도 검증한다.
	A->SetActorLocation(FVector(-99, 0, 0));
	B->SetActorLocation(FVector(99, 0, 0));
	A->SetActorRotation(FRotator(0, 90, 0));
	B->SetActorRotation(FRotator(0, 90, 0));
	TestTrue(TEXT("Rotated boxes cover the segment through their union"),
		FlightPathfinding::IsSegmentInsideMovementRooms(Request, FVector(-150, 0, 0), FVector(150, 0, 0), Extent));
	B->SetActorLocation(FVector(102, 0, 0));
	TestFalse(TEXT("Rotated boxes do not bridge an actual gap"),
		FlightPathfinding::IsSegmentInsideMovementRooms(Request, FVector(-150, 0, 0), FVector(150, 0, 0), Extent));
	A->SetActorRotation(FRotator::ZeroRotator);
	B->SetActorRotation(FRotator::ZeroRotator);
	A->SetActorLocation(FVector(-100, 0, 0));
	B->SetActorLocation(FVector(100, 0, 0));
	AEnemyBase* Enemy = Fixture.AddEnemy(MovementHome);
	UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent();
	Capsule->SetCapsuleSize(10, 20);
	Enemy->SetActorLocation(FVector(-150, 0, 0));
	Request.Start = Capsule->GetComponentLocation();
	Request.Goal = FVector(150, 0, 0);
	Request.Clearance = 0;
	Request.SearchBounds = FBox(FVector(-250), FVector(250));
	Request.MaxSearchMilliseconds = 1000;
	TestEqual(TEXT("Actual capsule sweep can cross registered touching rooms"),
		FlightPathfinding::CheckSegment(*Fixture.World, *Capsule, Request), EFlightPathStatus::Success);
	TestEqual(TEXT("Sibling EQS destination uses union boundary"),
		FlightPathfinding::CheckDestination(*Fixture.World, *Capsule, Request), EFlightPathStatus::Success);
	TestEqual(TEXT("Synchronous world search crosses the same touching union"),
		FlightPathfinding::FindPath(*Fixture.World, *Capsule, Request).Status, EFlightPathStatus::Success);
	B->SetActorLocation(FVector(110, 0, 0));
	B->SetActorLocation(FVector(100.00001, 0, 0));
	TestEqual(TEXT("Actual segment sweep cannot bridge even a thin unregistered gap"),
		FlightPathfinding::CheckSegment(*Fixture.World, *Capsule, Request), EFlightPathStatus::BlockedSegment);
	B->SetActorLocation(FVector(110, 0, 0));
	B->SetActorLocation(FVector(100, 0, 0));
	AActor* Wall = Fixture.World->SpawnActor<AActor>();
	UBoxComponent* WallBox = NewObject<UBoxComponent>(Wall);
	Wall->SetRootComponent(WallBox);
	WallBox->SetBoxExtent(FVector(5, 100, 100));
	WallBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	WallBox->SetCollisionResponseToAllChannels(ECR_Block);
	WallBox->RegisterComponent();
	TestEqual(TEXT("Room union does not bypass actual wall sweep"),
		FlightPathfinding::CheckSegment(*Fixture.World, *Capsule, Request), EFlightPathStatus::BlockedSegment);
	Wall->Destroy();
	TSharedPtr<FFlightPathSearch> Search = FlightPathfinding::BeginSearch(*Fixture.World, *Capsule, Request);
	if (!TestTrue(TEXT("Incremental world search can be created"), Search.IsValid())) return false;
	for (int32 Slice = 0; Slice < 1024 && !Search->Advance(512, 1000); ++Slice) {}
	TestEqual(TEXT("Incremental world search uses same touching union geometry"), Search->GetResult().Status, EFlightPathStatus::Success);
	// L자 합집합의 양 끝점/여덟 모서리가 들어가도 가운데 빈 공간은 통과할 수 없다.
	A->SetActorRotation(FRotator::ZeroRotator);
	B->SetActorRotation(FRotator::ZeroRotator);
	A->SetActorLocation(FVector(-100, 0, 0));
	B->SetActorLocation(FVector(0, 100, 0));
	CastChecked<UBoxComponent>(A->GetRootComponent())->SetBoxExtent(FVector(100, 50, 100));
	CastChecked<UBoxComponent>(B->GetRootComponent())->SetBoxExtent(FVector(50, 100, 100));
	TestTrue(TEXT("L-shaped union start fits"), FlightPathfinding::IsInsideMovementRooms(Request, FVector(-150, 0, 0), Extent));
	TestTrue(TEXT("L-shaped union goal fits"), FlightPathfinding::IsInsideMovementRooms(Request, FVector(0, 150, 0), Extent));
	TestFalse(TEXT("Endpoint checks cannot admit shortcut outside nonconvex union"),
		FlightPathfinding::IsSegmentInsideMovementRooms(Request, FVector(-150, 0, 0), FVector(0, 150, 0), Extent));
	B->Destroy();
	TestFalse(TEXT("Invalid allowed room fails closed even when remaining member contains point"),
		FlightPathfinding::IsInsideMovementRooms(Request, FVector(-150, 0, 0), Extent));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomMovementContextTest, "Outlier.Room.MovementBounds.RegisteredLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomMovementContextTest::RunTest(const FString& Parameters)
{
	FMovementBoundsWorld Fixture;
	ARoomVolume* A = Fixture.AddRoom(MovementHome, FVector(-100, 0, 0), FVector(100));
	ARoomVolume* B = Fixture.AddRoom(MovementSibling, FVector(100, 0, 0), FVector(100));
	Fixture.AddRoom(MovementOther, FVector(0, 300, 0), FVector(100));
	AEnemyBase* Enemy = Fixture.AddEnemy(MovementHome);
	FEnemyMovementRoomContext Context;
	TestTrue(TEXT("Patrol builds registered home context"), Fixture.Combat->BuildEnemyMovementRoomContext(Enemy, Context));
	TestEqual(TEXT("Patrol only allows home"), Context.Rooms.Num(), 1);
	TestTrue(TEXT("Fresh context is current"), Fixture.Combat->IsEnemyMovementRoomContextCurrent(Enemy, Context));
	SetMovementCombatState(Enemy, EEnemyCombatState::Combat);
	TestFalse(TEXT("Combat invalidates patrol permission snapshot"), Fixture.Combat->IsEnemyMovementRoomContextCurrent(Enemy, Context));
	TestTrue(TEXT("Combat rebuilds context"), Fixture.Combat->BuildEnemyMovementRoomContext(Enemy, Context));
	TestEqual(TEXT("Combat includes only registered same-parent volumes"), Context.Rooms.Num(), 2);
	TestTrue(TEXT("Sibling is included"), Context.Rooms.Contains(B));
	SetMovementCombatState(Enemy, EEnemyCombatState::Returning);
	TestTrue(TEXT("Returning preserves group permission for actual homeward path"), Fixture.Combat->IsEnemyMovementRoomContextCurrent(Enemy, Context));
	Fixture.Combat->UnregisterRoom(B);
	TestFalse(TEXT("Unregister invalidates existing spatial revision"), Fixture.Combat->IsEnemyMovementRoomContextCurrent(Enemy, Context));
	TestTrue(TEXT("Rebuild excludes removed sibling"), Fixture.Combat->BuildEnemyMovementRoomContext(Enemy, Context));
	TestEqual(TEXT("Only home remains"), Context.Rooms.Num(), 1);
	Fixture.Combat->RegisterRoom(B, MovementSibling);
	TestFalse(TEXT("Late registration also invalidates snapshot"), Fixture.Combat->IsEnemyMovementRoomContextCurrent(Enemy, Context));
	Fixture.Combat->BuildEnemyMovementRoomContext(Enemy, Context);
	Fixture.Combat->UnregisterRoom(A);
	TestFalse(TEXT("Missing source never falls back to sibling"), Fixture.Combat->BuildEnemyMovementRoomContext(Enemy, Context));
	TestTrue(TEXT("Failed build clears output rooms"), Context.Rooms.IsEmpty());
	Fixture.AddRoom(MovementLegacy, FVector(1000, 0, 0), FVector(100));
	Enemy->GetRoomTagComp()->AssignDefaultRoomTag(MovementLegacy);
	TestTrue(TEXT("Legacy home can still build its own context"), Fixture.Combat->BuildEnemyMovementRoomContext(Enemy, Context));
	TestEqual(TEXT("Legacy tag never gains group movement"), Context.Rooms.Num(), 1);
	Fixture.Combat->ResetRuntimeCombatState();
	TestFalse(TEXT("Reset invalidates context before old callbacks can move"), Fixture.Combat->IsEnemyMovementRoomContextCurrent(Enemy, Context));
	return true;
}

#endif
