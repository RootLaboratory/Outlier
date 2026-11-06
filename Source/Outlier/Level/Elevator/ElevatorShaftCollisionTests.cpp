#if WITH_DEV_AUTOMATION_TESTS

#include "Level/Elevator/ElevatorShaftController.h"
#include "Level/Elevator/ElevatorShaftSection.h"
#include "Interaction/InteractableDoor.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Enemy/SelfDestructDrone.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "PhysicsEngine/BodyInstance.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace
{
	struct FElevatorCollisionTestWorld
	{
		UWorld* World = nullptr;

		FElevatorCollisionTestWorld()
		{
			if (!GEngine) return;
			World = UWorld::CreateWorld(EWorldType::Game, false,
				MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("ElevatorCollisionTest")),
				GetTransientPackage());
			if (!World) return;
			World->AddToRoot();
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}

		~FElevatorCollisionTestWorld()
		{
			if (!World) return;
			World->DestroyWorld(true);
			World->SetPhysicsScene(nullptr);
			GEngine->DestroyWorldContext(World);
			World->RemoveFromRoot();
		}

		void FlushPhysics() const
		{
			World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
			World->GetPhysicsScene()->Flush();
		}

		UBoxComponent* AddWall(FVector Location, FVector Extent) const
		{
			AActor* Actor = World->SpawnActor<AActor>();
			if (!Actor) return nullptr;
			UBoxComponent* Wall = NewObject<UBoxComponent>(Actor);
			Actor->SetRootComponent(Wall);
			Wall->SetBoxExtent(Extent);
			Wall->SetCollisionProfileName(TEXT("BlockAll"));
			Wall->SetCollisionObjectType(ECC_WorldStatic);
			Wall->RegisterComponent();
			Wall->SetWorldLocation(Location);
			FlushPhysics();
			return Wall;
		}

		ACharacter* SpawnCharacter(UClass* CharacterClass) const
		{
			FActorSpawnParameters Parameters;
			Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ACharacter* Character = World->SpawnActor<ACharacter>(CharacterClass,
				FVector(0.0f, 0.0f, 300.0f), FRotator::ZeroRotator, Parameters);
			if (!Character) return nullptr;
			Character->GetCharacterMovement()->SetUpdatedComponent(Character->GetCapsuleComponent());
			Character->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
			FlushPhysics();
			return Character;
		}
	};

	FHitResult MoveAndSlide(ACharacter& Character, const FVector& Delta)
	{
		// 공개된 기반 API로 호출하되 실제 처리는 CharacterMovement의 override를 거친다.
		UMovementComponent* Movement = Character.GetCharacterMovement();
		FHitResult Hit;
		Movement->SafeMoveUpdatedComponent(Delta, Character.GetActorQuat(), true, Hit);
		if (Hit.IsValidBlockingHit())
		{
			Movement->SlideAlongSurface(Delta, 1.0f - Hit.Time, Hit.Normal, Hit, false);
		}
		return Hit;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElevatorExistingControllerGeometryTest,
	"Outlier.Elevator.Collision.ExistingControllerGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElevatorExistingControllerGeometryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FElevatorCollisionTestWorld Fixture;
	if (!TestNotNull(TEXT("Collision asset world"), Fixture.World)) return false;
	UClass* ControllerClass = LoadClass<AElevatorShaftController>(nullptr,
		TEXT("/Game/Blueprints/Elevator/BP_ElevatorController.BP_ElevatorController_C"));
	if (!TestNotNull(TEXT("Actual elevator Blueprint"), ControllerClass)) return false;
	// BeginPlay는 운행/Room 설정을 요구하므로 생성된 기존 컴포넌트의 충돌만 검증한다.
	AElevatorShaftController* Controller = Fixture.World->SpawnActor<AElevatorShaftController>(ControllerClass);
	if (!TestNotNull(TEXT("Actual elevator actor"), Controller)) return false;
	TInlineComponentArray<UStaticMeshComponent*> Meshes;
	Controller->GetComponents(Meshes, false);
	UStaticMeshComponent* FixedWall = nullptr;
	for (UStaticMeshComponent* Mesh : Meshes)
	{
		if (Mesh->GetStaticMesh() && Mesh->GetStaticMesh()->GetName() == TEXT("SM_Stage3_collision"))
		{
			FixedWall = Mesh;
			break;
		}
	}
	if (!TestNotNull(TEXT("Existing fixed collision mesh is owned by Controller"), FixedWall)) return false;
#if WITH_EDITOR
	// 에디터 비동기 메시 컴파일 중에는 물리 생성도 보류된다. 준비 전의 빈 형상을 결함으로 판정하지 않는다.
	FStaticMeshCompilingManager::Get().FinishCompilation({ FixedWall->GetStaticMesh() });
#endif
	Fixture.FlushPhysics();
	Controller->StartCollisionDiagnostics(0.1f);
	TestTrue(TEXT("Controller actor collision is enabled"), Controller->GetActorEnableCollision());
	TestTrue(TEXT("Fixed collision component is registered"), FixedWall->IsRegistered());
	TestTrue(TEXT("Fixed collision component participates in queries"), FixedWall->IsQueryCollisionEnabled());
	TestEqual(TEXT("Fixed wall is WorldStatic"), FixedWall->GetCollisionObjectType(), ECC_WorldStatic);
	TestEqual(TEXT("Fixed wall blocks Pawn movement"), FixedWall->GetCollisionResponseToChannel(ECC_Pawn), ECR_Block);
	FBodyInstance* Body = FixedWall->GetBodyInstance();
	if (!TestNotNull(TEXT("Fixed mesh has a body instance"), Body)) return false;
	TestTrue(TEXT("Actual fixed mesh creates a physics body"), Body->IsValidBodyInstance());

	// 알려지지 않은 맵 좌표 대신 실제 메시 Bounds를 기준으로 충돌 형상이 조회되는지 확인한다.
	// 이 검사는 모든 출입구/벽면이 닫혔다는 보장이 아니라, 기존 형상의 로드·쿼리 smoke test다.
	const FBox Bounds = FixedWall->Bounds.GetBox();
	const FVector Center = Bounds.GetCenter();
	const FVector Extent = Bounds.GetExtent();
	int32 BlockingSweepCount = 0;
	const UCapsuleComponent* DroneCapsule = GetDefault<ASelfDestructDrone>()->GetCapsuleComponent();
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(DroneCapsule->GetScaledCapsuleRadius(),
		DroneCapsule->GetScaledCapsuleHalfHeight());
	FCollisionQueryParams Query(SCENE_QUERY_STAT(ElevatorExistingGeometryTest), false);
	const FCollisionResponseParams Responses(DroneCapsule->GetCollisionResponseToChannels());
	for (const FVector Axis : { FVector::ForwardVector, FVector::RightVector })
	{
		for (const float Direction : { -1.0f, 1.0f })
		{
			for (const float HeightFraction : { -0.6f, 0.0f, 0.6f })
			{
				const FVector End = Center + FVector(0.0f, 0.0f, Extent.Z * HeightFraction);
				const float Reach = FVector::DotProduct(Extent, Axis) + Shape.GetCapsuleRadius() + 100.0f;
				TArray<FHitResult> Hits;
				// 컴포넌트 단독 Sweep 대신 실제 이동과 같은 World 쿼리/캡슐 응답으로 검사한다.
				Fixture.World->SweepMultiByChannel(Hits, End + Axis * Reach * Direction, End,
					FQuat::Identity, DroneCapsule->GetCollisionObjectType(), Shape, Query, Responses);
				for (const FHitResult& Hit : Hits)
				{
					if (Hit.GetComponent() == FixedWall && Hit.bBlockingHit) ++BlockingSweepCount;
				}
			}
		}
	}
	TestTrue(TEXT("Loaded fixed geometry blocks at least one bounds-derived capsule sweep"), BlockingSweepCount > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElevatorCollisionStateAndPlacementTest,
	"Outlier.Elevator.Collision.StateAndPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElevatorCollisionStateAndPlacementTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FElevatorCollisionTestWorld Fixture;
	if (!TestNotNull(TEXT("Placement world"), Fixture.World)) return false;
	UClass* ControllerClass = LoadClass<AElevatorShaftController>(nullptr,
		TEXT("/Game/Blueprints/Elevator/BP_ElevatorController.BP_ElevatorController_C"));
	if (!TestNotNull(TEXT("Actual controller Blueprint"), ControllerClass)) return false;
	AElevatorShaftController* Controller = Fixture.World->SpawnActor<AElevatorShaftController>(ControllerClass);
	if (!TestNotNull(TEXT("Actual controller"), Controller)) return false;
	UStaticMeshComponent* Wall = nullptr;
	TInlineComponentArray<UStaticMeshComponent*> Meshes;
	Controller->GetComponents(Meshes, false);
	for (UStaticMeshComponent* Mesh : Meshes)
	{
		if (Mesh->GetStaticMesh() && Mesh->GetStaticMesh()->GetName() == TEXT("SM_Stage3_collision")) Wall = Mesh;
	}
	if (!TestNotNull(TEXT("Authored collision mesh"), Wall)) return false;
#if WITH_EDITOR
	FStaticMeshCompilingManager::Get().FinishCompilation({ Wall->GetStaticMesh() });
#endif
	TInlineComponentArray<USceneComponent*> Components;
	Controller->GetComponents(Components, true);
	for (USceneComponent* Component : Components) Component->SetMobility(EComponentMobility::Movable);
	Controller->SetActorTransform(FTransform(FRotator(0, -90, 0), FVector(9996.59, -7657.06, 998.75)));
	Controller->ResolveEntranceActors();
	if (!TestNotNull(TEXT("Authored entrance reference"), Controller->EntranceSection.Get())) return false;
	Controller->DepartureControllerTransform = Controller->GetActorTransform();
	Controller->Motion.EntranceTransform = Controller->EntranceSection->GetActorTransform();
	Controller->ControllerSectionOffset = Controller->DepartureControllerTransform.GetRelativeTransform(Controller->Motion.EntranceTransform);
	Controller->LoopSectionClass = nullptr;
	Controller->bInitialized = true;
	Controller->Motion.ExecutionId = FGuid::NewGuid();
	const FTransform DepartureWall = Wall->GetComponentTransform();
	const FTransform DepartureEntranceBlocker = Controller->EntranceBlocker->GetComponentTransform();
	const FTransform DepartureArrivalBlocker = Controller->ArrivalBlocker->GetComponentTransform();
	const FTransform RelativeWall = Wall->GetRelativeTransform();
	const FName Profile = Wall->GetCollisionProfileName();
	const ECollisionEnabled::Type Collision = Wall->GetCollisionEnabled();
	const FCollisionResponseContainer Responses = Wall->GetCollisionResponseToChannels();
	struct FStateCase { EElevatorShaftState State; bool bArrival; bool bBlocked; };
	// 출구는 완료 전 모든 상태에서 막고, 완료 뒤 재시도 대기로 복귀해도 다시 막아야 한다.
	const FStateCase Cases[] = {
		{ EElevatorShaftState::WaitingForPair, false, true },
		{ EElevatorShaftState::ClosingEntrance, false, true },
		{ EElevatorShaftState::Intro, false, true },
		{ EElevatorShaftState::WaitingForCombatStart, false, true },
		{ EElevatorShaftState::AwaitingSpawn, false, true },
		{ EElevatorShaftState::RisingCombat, false, true },
		{ EElevatorShaftState::FinishingCurrentSection, false, true },
		{ EElevatorShaftState::TransitionOut, false, true },
		{ EElevatorShaftState::PreparingArrival, true, true },
		{ EElevatorShaftState::Arriving, true, true },
		{ EElevatorShaftState::Decelerating, true, true },
		{ EElevatorShaftState::Completed, true, false },
		{ EElevatorShaftState::WaitingForPair, false, true }
	};
	for (float ArrivalYaw : { 0.0f, 90.0f, 180.0f })
	{
		Controller->Motion.ArrivalTransform = FTransform(FRotator(0, ArrivalYaw, 0), FVector(11000, -7757.06, -1994.25));
		for (const FStateCase& Case : Cases)
		{
			Controller->Motion.State = Case.State;
			Controller->Motion.bArrival = Case.bArrival;
			Controller->Motion.bLanded = Case.State == EElevatorShaftState::Completed;
			Controller->OnRep_Motion();
			// 같은 상태의 재적용에서도 위치/회전이 누적되지 않아야 한다.
			Controller->OnRep_Motion();
			Fixture.FlushPhysics();
			const FString Label = FString::Printf(TEXT("Yaw=%.0f State=%s"), ArrivalYaw, *UEnum::GetValueAsString(Case.State));
			FTransform ExpectedWall = DepartureWall;
			ExpectedWall.AddToTranslation(Controller->GetActorLocation() - Controller->DepartureControllerTransform.GetLocation());
			TestTrue(Label + TEXT(" wall translates without changing its authored world orientation"), Wall->GetComponentTransform().Equals(ExpectedWall, 0.01));
			TestTrue(Label + TEXT(" wall stays attached to the controller"), Wall->GetAttachParent() == Controller->GetRootComponent());
			TestEqual(Label + TEXT(" wall collision enabled setting is unchanged"), Wall->GetCollisionEnabled(), Collision);
			TestEqual(Label + TEXT(" wall collision profile is unchanged"), Wall->GetCollisionProfileName(), Profile);
			TestTrue(Label + TEXT(" all wall channel responses are unchanged"), Wall->GetCollisionResponseToChannels() == Responses);
			const FBodyInstance* Body = Wall->GetBodyInstance();
			if (!TestTrue(Label + TEXT(" wall physics body remains valid"), Body && Body->IsValidBodyInstance())) return false;
			const FTransform Physics = Body->GetUnrealWorldTransform();
			TestTrue(Label + TEXT(" physics position matches the component"), Physics.GetLocation().Equals(ExpectedWall.GetLocation(), 0.01));
			TestTrue(Label + TEXT(" physics rotation matches the component"), Physics.GetRotation().Equals(ExpectedWall.GetRotation(), 0.001));
			// 현재 위치를 기준으로 검사하면 잘못 회전한 박스도 통과한다. 출발 배치에서 이동량만 더한다.
			for (UBoxComponent* Blocker : { Controller->EntranceBlocker.Get(), Controller->ArrivalBlocker.Get() })
			{
				FTransform ExpectedBlocker = Blocker == Controller->EntranceBlocker
					? DepartureEntranceBlocker : DepartureArrivalBlocker;
				ExpectedBlocker.AddToTranslation(Controller->GetActorLocation() - Controller->DepartureControllerTransform.GetLocation());
				const FString BlockerLabel = Label + TEXT(" ") + Blocker->GetName();
				TestTrue(BlockerLabel + TEXT(" translates with the wall without rotating or scaling"),
					Blocker->GetComponentTransform().Equals(ExpectedBlocker, 0.01));
				const bool bExpectedBlocked = Blocker == Controller->ArrivalBlocker ? Case.bBlocked
					: Case.State != EElevatorShaftState::WaitingForPair && Case.State != EElevatorShaftState::ClosingEntrance;
				FCollisionQueryParams Query(SCENE_QUERY_STAT(ElevatorFixedBlockerPlacement), false);
				TInlineComponentArray<UPrimitiveComponent*> Primitives;
				Controller->GetComponents(Primitives, true);
				for (UPrimitiveComponent* Other : Primitives)
				{
					if (Other != Blocker) Query.AddIgnoredComponent(Other);
				}
				const FVector Offset(Blocker->GetUnscaledBoxExtent().X + 50, 0, 0);
				FHitResult Hit;
				Fixture.World->SweepSingleByChannel(Hit, ExpectedBlocker.TransformPosition(-Offset),
					ExpectedBlocker.TransformPosition(Offset), FQuat::Identity, ECC_Pawn,
					FCollisionShape::MakeSphere(4), Query);
				TestEqual(BlockerLabel + TEXT(" blocks at its authored translated position when enabled"),
					Hit.bBlockingHit && Hit.GetComponent() == Blocker, bExpectedBlocked);
			}
			TestEqual(Label + TEXT(" arrival blocker enabled state"), Controller->ArrivalBlocker->IsQueryCollisionEnabled(), Case.bBlocked);
			TestEqual(Label + TEXT(" arrival blocker retains Pawn blocking"), Controller->ArrivalBlocker->GetCollisionResponseToChannel(ECC_Pawn), ECR_Block);
			FCollisionQueryParams BlockerQuery(SCENE_QUERY_STAT(ElevatorArrivalBlockerState), false);
			TInlineComponentArray<UPrimitiveComponent*> OtherComponents;
			Controller->GetComponents(OtherComponents, true);
			for (UPrimitiveComponent* Other : OtherComponents)
			{
				if (Other != Controller->ArrivalBlocker) BlockerQuery.AddIgnoredComponent(Other);
			}
			const FTransform BlockerTransform = Controller->ArrivalBlocker->GetComponentTransform();
			const FVector ProbeOffset(Controller->ArrivalBlocker->GetUnscaledBoxExtent().X + 50, 0, 0);
			FHitResult BlockerHit;
			Fixture.World->SweepSingleByChannel(BlockerHit, BlockerTransform.TransformPosition(-ProbeOffset),
				BlockerTransform.TransformPosition(ProbeOffset), FQuat::Identity, ECC_Pawn,
				FCollisionShape::MakeSphere(4), BlockerQuery);
			TestEqual(Label + TEXT(" physical query agrees with blocker enabled state"),
				BlockerHit.bBlockingHit && BlockerHit.GetComponent() == Controller->ArrivalBlocker, Case.bBlocked);
			TestTrue(Label + TEXT(" blocker stays attached to the controller frame"), Controller->ArrivalBlocker->GetComponentTransform().Equals(
				Controller->ArrivalBlocker->GetRelativeTransform() * Controller->GetActorTransform(), 0.01));
			AddInfo(Label + (Case.bBlocked ? TEXT(" ArrivalBlocker=ON") : TEXT(" ArrivalBlocker=OFF")));
		}
	}
	// 90도 잘못 돌아간 외벽도 단순히 '무언가를 막는' 검사는 통과할 수 있다.
	// 독립 기준인 출발 배치와 비교하면 이 오류를 구분하는지 확인한다.
	Wall->AddLocalRotation(FRotator(0, 90, 0));
	TestFalse(TEXT("A deliberately rotated wall is rejected by placement comparison"), Wall->GetComponentTransform().Equals(DepartureWall, 0.01));
	Wall->SetRelativeTransform(RelativeWall);
	AddInfo(TEXT("ArrivalBlocker blocks every pre-completion state and rollback; only Completed opens the exit."));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElevatorOuterWallArrivalRubbingTest,
	"Outlier.Elevator.Collision.OuterWallArrivalRubbing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElevatorOuterWallArrivalRubbingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UClass* ControllerClass = LoadClass<AElevatorShaftController>(nullptr,
		TEXT("/Game/Blueprints/Elevator/BP_ElevatorController.BP_ElevatorController_C"));
	if (!TestNotNull(TEXT("Actual controller Blueprint"), ControllerClass)) return false;
	UClass* ArrivalClass = LoadClass<AElevatorShaftSection>(nullptr,
		TEXT("/Game/Blueprints/Elevator/BP_ElevatorSection_Arrival.BP_ElevatorSection_Arrival_C"));
	if (!TestNotNull(TEXT("Actual arrival Blueprint"), ArrivalClass)) return false;
	for (UClass* CharacterClass : { ASelfDestructDrone::StaticClass(), APartnerCharacter::StaticClass() })
	{
		FElevatorCollisionTestWorld Fixture;
		if (!TestNotNull(TEXT("Arrival rubbing world"), Fixture.World)) return false;
		AElevatorShaftController* Controller = Fixture.World->SpawnActor<AElevatorShaftController>(ControllerClass);
		if (!TestNotNull(TEXT("Actual controller"), Controller)) return false;
		UStaticMeshComponent* Wall = nullptr;
		TInlineComponentArray<UStaticMeshComponent*> Meshes;
		Controller->GetComponents(Meshes, false);
		for (UStaticMeshComponent* Mesh : Meshes)
		{
			if (Mesh->GetStaticMesh() && Mesh->GetStaticMesh()->GetName() == TEXT("SM_Stage3_collision")) Wall = Mesh;
		}
		if (!TestNotNull(TEXT("Authored outer wall collision"), Wall)) return false;
#if WITH_EDITOR
		FStaticMeshCompilingManager::Get().FinishCompilation({ Wall->GetStaticMesh() });
#endif
		// 운행/전투/페이드 준비 대신 실제 운송 함수와 상태 적용 경계를 실행한다.
		// BeginPlay와 동일하게 Controller의 자식 충돌을 Movable로 준비한다.
		TInlineComponentArray<USceneComponent*> SceneComponents;
		Controller->GetComponents(SceneComponents, true);
		for (USceneComponent* Component : SceneComponents) Component->SetMobility(EComponentMobility::Movable);
		Controller->SetActorTransform(FTransform(FRotator(0, -90, 0), FVector(9996.59, -7657.06, 998.75)));
		Controller->ResolveEntranceActors();
		Controller->DepartureControllerTransform = Controller->GetActorTransform();
		Controller->Motion.EntranceTransform = Controller->EntranceSection
			? Controller->EntranceSection->GetActorTransform() : Controller->GetActorTransform();
		Controller->ControllerSectionOffset = Controller->DepartureControllerTransform.GetRelativeTransform(Controller->Motion.EntranceTransform);
		Controller->Motion.ArrivalTransform = FTransform(FRotator(0, 90, 0), FVector(11000, -7757.06, -1994.25));
		Controller->ArrivalSection = Fixture.World->SpawnActor<AElevatorShaftSection>(ArrivalClass, Controller->Motion.ArrivalTransform);
		Controller->ResolveArrivalActors();
		if (!TestNotNull(TEXT("Actual arrival door"), Controller->ArrivalDoor.Get())) return false;
		Controller->ArrivalDoor->DispatchBeginPlay();
		Controller->Motion.ArrivalDistance = 2000;
		Controller->Motion.ExecutionId = FGuid::NewGuid();
		Controller->LoopSectionClass = nullptr;
		Controller->bInitialized = true;
		ACharacter* Character = Fixture.SpawnCharacter(CharacterClass);
		if (!TestNotNull(TEXT("Rubbing character"), Character)) return false;
		UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		// 문/차단막이 외벽 결함을 대신 막아 테스트를 통과시키지 못하게 한다.
		TInlineComponentArray<UPrimitiveComponent*> Primitives;
		Controller->GetComponents(Primitives, true);
		TInlineComponentArray<UPrimitiveComponent*> ArrivalPrimitives;
		Controller->ArrivalSection->GetComponents(ArrivalPrimitives, true);
		Primitives.Append(ArrivalPrimitives);
		for (UPrimitiveComponent* Component : Primitives)
		{
			if (Component != Wall) Capsule->IgnoreComponentWhenMoving(Component, true);
		}
		Fixture.FlushPhysics();
		struct FWallContact { FVector Start; FVector Contact; FVector Normal; };
		TArray<FWallContact> Contacts;
		const FTransform OriginalWall = Wall->GetComponentTransform();
		const FBox Bounds = Wall->Bounds.GetBox();
		FCollisionQueryParams Query(SCENE_QUERY_STAT(ElevatorArrivalRubbing), false, Character);
		Query.AddIgnoredComponents(Capsule->GetMoveIgnoreComponents());
		const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight());
		const FCollisionResponseParams Responses(Capsule->GetCollisionResponseToChannels());
		// 의도된 출입구를 막힌 벽으로 가정하지 않는다. 출발 위치에서 실제로 막히는
		// 방위/높이를 고정한 뒤, 같은 접촉점을 도착 후에도 빠짐없이 검사한다.
		for (float Height : { -0.5f, 0.0f, 0.5f })
		{
			const FVector Start = Bounds.GetCenter() + FVector(0, 0, Bounds.GetExtent().Z * Height);
			for (int32 Direction = 0; Direction < 16; ++Direction)
			{
				const FVector Axis = FRotator(0, Direction * 22.5f, 0).Vector();
				FHitResult Hit;
				Fixture.World->SweepSingleByChannel(Hit, Start, Start + Axis * Bounds.GetExtent().Size() * 2,
					FQuat::Identity, Capsule->GetCollisionObjectType(), Shape, Query, Responses);
				if (Hit.GetComponent() == Wall && Hit.IsValidBlockingHit())
				{
					Contacts.Add({ OriginalWall.InverseTransformPosition(Start), OriginalWall.InverseTransformPosition(Hit.Location),
						OriginalWall.InverseTransformVectorNoScale(Hit.Normal) });
				}
			}
		}
		if (!TestTrue(TEXT("Actual wall supplies multiple rubbing contacts"), Contacts.Num() >= 8)) return false;
		for (int32 Stage = 0; Stage < 4; ++Stage)
		{
			const bool bArrival = Stage == 1 || Stage == 2;
			Controller->Motion.bArrival = bArrival;
			Controller->Motion.bLanded = Stage == 2;
			Controller->Motion.State = Stage == 0 || Stage == 3 ? EElevatorShaftState::WaitingForPair
				: Stage == 1 ? EElevatorShaftState::PreparingArrival : EElevatorShaftState::Completed;
			TestTrue(TEXT("Arrival door switches presentation mode"), Controller->ArrivalDoor->SetPresentationOnly(Stage == 1));
			Controller->OnRep_Motion();
			Fixture.FlushPhysics();
			const FString Label = FString::Printf(TEXT("%s Stage=%d"), *CharacterClass->GetName(), Stage);
			if (Stage == 1) TestFalse(Label + TEXT(" arrival visuals are below the final landing"),
				Controller->ArrivalSection->GetActorTransform().Equals(Controller->Motion.ArrivalTransform, 0.01));
			if (Stage == 2) TestTrue(Label + TEXT(" arrival visuals reach their authored landing"),
				Controller->ArrivalSection->GetActorTransform().Equals(Controller->Motion.ArrivalTransform, 0.01));
			bool bAllBlocked = true;
			bool bAllStayedInside = true;
			for (const FWallContact& Contact : Contacts)
			{
				const FTransform CurrentWall = Wall->GetComponentTransform();
				const FVector Start = CurrentWall.TransformPosition(Contact.Start);
				const FVector Limit = CurrentWall.TransformPosition(Contact.Contact);
				const FVector Normal = CurrentWall.TransformVectorNoScale(Contact.Normal).GetSafeNormal();
				const FVector Tangent = FVector::CrossProduct(Normal, FVector::UpVector).GetSafeNormal();
				Character->SetActorLocation(Start, false, nullptr, ETeleportType::TeleportPhysics);
				FHitResult Hit;
				Character->GetCharacterMovement()->SafeMoveUpdatedComponent((Limit - Start) * 2,
					Character->GetActorQuat(), true, Hit);
				bAllBlocked &= Hit.bBlockingHit && Hit.GetComponent() == Wall;
				for (int32 Step = 0; Step < 60; ++Step)
				{
					MoveAndSlide(*Character, -Normal * 20 + Tangent * (Step % 2 == 0 ? 4 : -4));
					bAllStayedInside &= FVector::DotProduct(Character->GetActorLocation() - Limit, Normal) >= -0.75;
				}
			}
			TestTrue(Label + TEXT(" every original wall contact still blocks a large move"), bAllBlocked);
			TestTrue(Label + TEXT(" 60 rubbing moves per contact never cross the wall"), bAllStayedInside);
			// 물리 쿼리가 죽어 있어도 통과하는 빈 테스트를 방지한다. 외벽만 끄면
			// 같은 시작점/이동으로 기존 접촉면을 넘어가야 하며 다음 단계 전에 복구한다.
			const FWallContact& Control = Contacts[0];
			const FVector Start = Wall->GetComponentTransform().TransformPosition(Control.Start);
			const FVector Limit = Wall->GetComponentTransform().TransformPosition(Control.Contact);
			const FVector Normal = Wall->GetComponentTransform().TransformVectorNoScale(Control.Normal).GetSafeNormal();
			const ECollisionEnabled::Type PreviousCollision = Wall->GetCollisionEnabled();
			Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Fixture.FlushPhysics();
			Character->SetActorLocation(Start, false, nullptr, ETeleportType::TeleportPhysics);
			FHitResult ControlHit;
			Character->GetCharacterMovement()->SafeMoveUpdatedComponent((Limit - Start) * 2, Character->GetActorQuat(), true, ControlHit);
			TestTrue(Label + TEXT(" disabled-wall control crosses the original contact plane"),
				FVector::DotProduct(Character->GetActorLocation() - Limit, Normal) < -1);
			Wall->SetCollisionEnabled(PreviousCollision);
			Fixture.FlushPhysics();
			AddInfo(FString::Printf(TEXT("%s Contacts=%d RubbingMoves=%d"), *Label, Contacts.Num(), Contacts.Num() * 60));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElevatorCapsuleWallAndCornerTest,
	"Outlier.Elevator.Collision.CapsuleWallAndCorner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElevatorCapsuleWallAndCornerTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	for (UClass* CharacterClass : { ASelfDestructDrone::StaticClass(), APartnerCharacter::StaticClass() })
	{
		FElevatorCollisionTestWorld Fixture;
		if (!TestNotNull(TEXT("Movement world"), Fixture.World)) return false;
		UBoxComponent* XWall = Fixture.AddWall(FVector(200.0f, 0.0f, 300.0f), FVector(20.0f, 1000.0f, 500.0f));
		UBoxComponent* YWall = Fixture.AddWall(FVector(0.0f, 200.0f, 300.0f), FVector(1000.0f, 20.0f, 500.0f));
		ACharacter* Character = Fixture.SpawnCharacter(CharacterClass);
		if (!TestNotNull(TEXT("X wall"), XWall) || !TestNotNull(TEXT("Y wall"), YWall)
			|| !TestNotNull(TEXT("Character"), Character)) return false;
		const FString Label = CharacterClass->GetName();
		UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		const float Limit = 180.0f - Capsule->GetScaledCapsuleRadius();
		TestTrue(Label + TEXT(" capsule queries are enabled"), Capsule->IsQueryCollisionEnabled());
		TestEqual(Label + TEXT(" capsule blocks WorldStatic"),
			Capsule->GetCollisionResponseToChannel(ECC_WorldStatic), ECR_Block);

		FHitResult Hit;
		Character->GetCharacterMovement()->SafeMoveUpdatedComponent(FVector(5000.0f, 0.0f, 0.0f),
			Character->GetActorQuat(), true, Hit);
		TestTrue(Label + TEXT(" frontal high-displacement move is blocked"), Hit.bBlockingHit);
		TestTrue(Label + TEXT(" cannot cross the frontal wall"), Character->GetActorLocation().X <= Limit + 0.5f);
		Character->SetActorLocation(FVector(0.0f, 0.0f, 300.0f), false, nullptr, ETeleportType::TeleportPhysics);
		MoveAndSlide(*Character, FVector(800.0f, 300.0f, 0.0f));
		TestTrue(Label + TEXT(" diagonal slide stays inside both walls"),
			Character->GetActorLocation().X <= Limit + 0.5f && Character->GetActorLocation().Y <= Limit + 0.5f);
		bool bStayedInside = true;
		for (int32 Index = 0; Index < 80; ++Index)
		{
			MoveAndSlide(*Character, FVector(20.0f, 20.0f, 0.0f));
			bStayedInside &= Character->GetActorLocation().X <= Limit + 0.5f
				&& Character->GetActorLocation().Y <= Limit + 0.5f;
		}
		TestTrue(Label + TEXT(" repeated corner rubbing cannot escape"), bStayedInside);

		// 벽의 실내 쪽 표면에 3cm만 겹쳐 시작해, MTD 해소가 반대편으로 밀어내지 않는지 검사한다.
		Character->SetActorLocation(FVector(Limit + 3.0f, 0.0f, 300.0f), false, nullptr, ETeleportType::TeleportPhysics);
		FCollisionQueryParams Query(SCENE_QUERY_STAT(ElevatorInitialOverlapTest), false, Character);
		FHitResult InitialOverlap;
		Fixture.World->SweepSingleByChannel(InitialOverlap, Capsule->GetComponentLocation(),
			Capsule->GetComponentLocation() + FVector(10.0f, 0.0f, 0.0f), Capsule->GetComponentQuat(),
			Capsule->GetCollisionObjectType(), FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),
				Capsule->GetScaledCapsuleHalfHeight()), Query,
			FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()));
		TestTrue(Label + TEXT(" initial overlap fixture really penetrates the wall"), InitialOverlap.bStartPenetrating);
		MoveAndSlide(*Character, FVector(80.0f, 60.0f, 0.0f));
		TestTrue(Label + TEXT(" initial penetration resolves toward the interior"),
			Character->GetActorLocation().X <= Limit + 0.5f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElevatorPartnerCameraSeparationTest,
	"Outlier.Elevator.Collision.PartnerCameraSeparation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElevatorPartnerCameraSeparationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FElevatorCollisionTestWorld Fixture;
	if (!TestNotNull(TEXT("Camera diagnostic world"), Fixture.World)) return false;
	UBoxComponent* Wall = Fixture.AddWall(FVector(200.0f, 0.0f, 300.0f), FVector(20.0f, 1000.0f, 500.0f));
	APartnerCharacter* Partner = Cast<APartnerCharacter>(Fixture.SpawnCharacter(APartnerCharacter::StaticClass()));
	if (!TestNotNull(TEXT("Camera test wall"), Wall) || !TestNotNull(TEXT("Partner"), Partner)) return false;
	FHitResult MovementHit;
	Partner->GetCharacterMovement()->SafeMoveUpdatedComponent(FVector(1000.0f, 0.0f, 0.0f),
		Partner->GetActorQuat(), true, MovementHit);
	TestTrue(TEXT("Partner body is blocked by the wall"), MovementHit.bBlockingHit);
	const FVector BodyLocation = Partner->GetActorLocation();
	FHitResult ProbeHit;
	TestFalse(TEXT("Camera at the body center has no separating wall"),
		AElevatorShaftController::ProbeDiagnosticCameraPath(*Partner, BodyLocation, false, ProbeHit));

	// 고정 카메라 설정의 정상/오류를 가정하지 않는다. 의도적으로 카메라만 벽 밖에 두어
	// 본체 차단과 카메라 경로 관통을 실제 런타임 진단 함수가 구분하는지 확인한다.
	UCameraComponent* Camera = Partner->GetFirstPersonCameraComponent();
	if (!TestNotNull(TEXT("Partner first-person camera"), Camera)) return false;
	Camera->SetWorldLocation(BodyLocation + FVector(150.0f, 0.0f, 0.0f));
	TestTrue(TEXT("Camera channel detects a wall between capsule and camera"),
		AElevatorShaftController::ProbeDiagnosticCameraPath(*Partner, Camera->GetComponentLocation(), false, ProbeHit));
	TestTrue(TEXT("Camera probe identifies the actual blocking component"), ProbeHit.GetComponent() == Wall);
	TestTrue(TEXT("Diagnostic camera probe does not move the Partner body"), Partner->GetActorLocation().Equals(BodyLocation));

	Wall->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	TestFalse(TEXT("Camera channel ignores a wall configured to ignore Camera"),
		AElevatorShaftController::ProbeDiagnosticCameraPath(*Partner, Camera->GetComponentLocation(), false, ProbeHit));
	TestTrue(TEXT("Body-channel comparison still finds the same physical wall"),
		AElevatorShaftController::ProbeDiagnosticCameraPath(*Partner, Camera->GetComponentLocation(), true, ProbeHit));
	TestTrue(TEXT("Body-channel comparison identifies the wall"), ProbeHit.GetComponent() == Wall);
	Partner->GetCapsuleComponent()->IgnoreComponentWhenMoving(Wall, true);
	TestFalse(TEXT("Body-channel probe respects actual movement component ignores"),
		AElevatorShaftController::ProbeDiagnosticCameraPath(*Partner, Camera->GetComponentLocation(), true, ProbeHit));
	Partner->GetCapsuleComponent()->IgnoreComponentWhenMoving(Wall, false);

	Partner->SetActorLocation(FVector(200.0f, 0.0f, 300.0f), false, nullptr, ETeleportType::TeleportPhysics);
	TestTrue(TEXT("Camera body-channel probe detects an initial body overlap"),
		AElevatorShaftController::ProbeDiagnosticCameraPath(*Partner,
			Partner->GetActorLocation() + FVector(150.0f, 0.0f, 0.0f), true, ProbeHit));
	TestTrue(TEXT("Initial camera path overlap reports StartPenetrating"), ProbeHit.bStartPenetrating);
	return true;
}

#endif
