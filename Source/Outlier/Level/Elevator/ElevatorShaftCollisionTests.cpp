#if WITH_DEV_AUTOMATION_TESTS

#include "Level/Elevator/ElevatorShaftController.h"
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
