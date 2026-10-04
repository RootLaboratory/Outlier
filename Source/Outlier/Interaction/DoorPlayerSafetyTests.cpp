#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Enemy/EnemyBase.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Interaction/InteractableDoor.h"
#include "Interaction/InteractableSwitch.h"
#include "Misc/AutomationTest.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Shooter/ShooterCharacter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDoorPlayerSafetyTest,
	"Outlier.Interaction.DoorPlayerSafety.CloseRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDoorPlayerSafetyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("DoorPlayerSafetyTest")));
	if (!TestNotNull(TEXT("Safety test world exists"), World))
	{
		return false;
	}
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	const auto Cleanup = [World]()
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	if (!TestNotNull(TEXT("Bounds fixture mesh exists"), Cube)
		|| !TestNotNull(TEXT("Concrete Shooter class exists"), ShooterClass))
	{
		Cleanup();
		return false;
	}

	// BeginPlay는 문에만 전달한다. 캐릭터의 전투/UI 초기화 없이 실제 Capsule과
	// 서버 Controller의 빙의를 사용해 물리 조회 및 요청 경계를 검증한다.
	AInteractableDoor* Door = World->SpawnActor<AInteractableDoor>();
	AInteractableSwitch* Switch = World->SpawnActor<AInteractableSwitch>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	AShooterCharacter* Shooter = World->SpawnActor<AShooterCharacter>(
		ShooterClass, FVector(2000.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	APartnerCharacter* Partner = World->SpawnActor<APartnerCharacter>(
		APartnerCharacter::StaticClass(), FVector(2500.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	AEnemyBase* Enemy = World->SpawnActor<AEnemyBase>(
		AEnemyBase::StaticClass(), FVector(3000.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Door exists"), Door) || !TestNotNull(TEXT("Switch exists"), Switch)
		|| !TestNotNull(TEXT("Controller exists"), Controller)
		|| !TestNotNull(TEXT("Shooter exists"), Shooter)
		|| !TestNotNull(TEXT("Partner exists"), Partner)
		|| !TestNotNull(TEXT("Enemy exists"), Enemy))
	{
		Cleanup();
		return false;
	}
	Door->DoorMeshLeft->SetStaticMesh(Cube);
	Door->DoorMeshRight->SetStaticMesh(Cube);
	Door->DoorMeshLeft->SetRelativeLocation(FVector(-60.0f, 0.0f, 0.0f));
	Door->DoorMeshRight->SetRelativeLocation(FVector(60.0f, 0.0f, 0.0f));
	Door->bInitiallyOpen = true;
	Door->DispatchBeginPlay();
	Switch->TargetDoor = Door;
	for (ACharacter* Character : { static_cast<ACharacter*>(Shooter),
		static_cast<ACharacter*>(Partner), static_cast<ACharacter*>(Enemy) })
	{
		Character->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Character->GetCapsuleComponent()->SetGenerateOverlapEvents(false);
	}
	FPhysScene* PhysicsScene = World->GetPhysicsScene();
	if (!TestNotNull(TEXT("Safety fixture has a physics scene"), PhysicsScene))
	{
		Cleanup();
		return false;
	}
	const auto SyncPhysics = [PhysicsScene]()
	{
		// 이 World는 프레임을 진행하지 않는다. 지연 생성된 몸체와 Chaos 조회 공간을
		// 먼저 반영해, 감지 실패와 테스트 물리 준비 실패를 구별한다.
		PhysicsScene->ProcessDeferredCreatePhysicsState();
		PhysicsScene->Flush();
	};
	const auto MoveCharacter = [&SyncPhysics](ACharacter* Character, const FVector& Location)
	{
		Character->SetActorLocation(Location);
		SyncPhysics();
	};
	SyncPhysics();
	for (ACharacter* Character : { static_cast<ACharacter*>(Shooter),
		static_cast<ACharacter*>(Partner), static_cast<ACharacter*>(Enemy) })
	{
		if (!TestTrue(TEXT("Fixture Capsule is registered"), Character->GetCapsuleComponent()->IsRegistered())
			|| !TestTrue(TEXT("Fixture Capsule has physics state"), Character->GetCapsuleComponent()->IsPhysicsStateCreated()))
		{
			Cleanup();
			return false;
		}
	}

	TestTrue(TEXT("Safety box includes the closed left leaf"),
		Door->SafetyRegionLeft->Bounds.GetBox().IsInside(FVector(-60.0f, 0.0f, 0.0f)));
	TestTrue(TEXT("Safety box includes the open left leaf"),
		Door->SafetyRegionLeft->Bounds.GetBox().IsInside(FVector(-180.0f, 0.0f, 0.0f)));
	TestTrue(TEXT("Safety box includes the open right leaf"),
		Door->SafetyRegionRight->Bounds.GetBox().IsInside(FVector(180.0f, 0.0f, 0.0f)));
	TestTrue(TEXT("Safety box includes space above the leaf"),
		Door->SafetyRegionLeft->Bounds.GetBox().IsInside(FVector(-60.0f, 0.0f, 200.0f)));
	TestFalse(TEXT("Empty regions do not block"), Door->HasBlockingPlayer());
	Controller->Possess(Shooter);
	// PlayerState가 없는 테스트 Controller도 현재 Pawn을 실제로 조종한다.
	// 런타임 보호 판정과 동일하게 양방향 빙의 연결을 확인한다.
	if (!TestTrue(TEXT("Shooter possession establishes player control"),
		Shooter->GetController() == Controller && Controller->GetPawn() == Shooter))
	{
		Cleanup();
		return false;
	}
	MoveCharacter(Shooter, FVector(-60.0f, 0.0f, 150.0f));
	if (!TestTrue(TEXT("Shooter above the left leaf blocks without foot contact"), Door->HasBlockingPlayer()))
	{
		// 최초 감지가 실패하면 이후 스위치/상태 검사는 연쇄 실패만 만든다.
		// 몸체와 범위 좌표를 남기고 이 시나리오를 종료한다.
		AddInfo(FString::Printf(TEXT("Capsule=%s LeftRegion=%s Extent=%s ActorCollision=%d"),
			*Shooter->GetCapsuleComponent()->GetComponentLocation().ToString(),
			*Door->SafetyRegionLeft->GetComponentLocation().ToString(),
			*Door->SafetyRegionLeft->GetScaledBoxExtent().ToString(), Shooter->GetActorEnableCollision()));
		Cleanup();
		return false;
	}
	TestFalse(TEXT("Occupied close returns failure"), Door->TrySetDoorOpen(false));
	Door->SetDoorOpen(false);
	Door->ToggleDoor();
	TestTrue(TEXT("Legacy close and toggle paths retain the open state"), Door->IsDoorOpen());
	TestFalse(TEXT("Switch rejects the occupied close"), Switch->Interact(Shooter));
	TestFalse(TEXT("Rejected switch does not activate"), Switch->bIsActivated);
	TestFalse(TEXT("Rejected request does not enable motion Tick"), Door->IsActorTickEnabled());
	TestTrue(TEXT("Open requests remain accepted"), Door->TrySetDoorOpen(true));
	APlayerController* SecondController = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("Second player controller exists"), SecondController))
	{
		Cleanup();
		return false;
	}
	SecondController->Possess(Partner);
	MoveCharacter(Partner, FVector(60.0f, 0.0f, 150.0f));
	MoveCharacter(Shooter, FVector(2000.0f, 0.0f, 0.0f));
	TestFalse(TEXT("One remaining player still rejects closing"), Door->TrySetDoorOpen(false));
	MoveCharacter(Partner, FVector(2500.0f, 0.0f, 0.0f));
	SecondController->UnPossess();
	SecondController->Destroy();

	MoveCharacter(Shooter, FVector(60.0f, 0.0f, 150.0f));
	TestTrue(TEXT("Shooter above the right leaf also blocks"), Door->HasBlockingPlayer());
	MoveCharacter(Shooter, FVector(2000.0f, 0.0f, 0.0f));
	TestTrue(TEXT("Clearance alone leaves the door open"), Door->IsDoorOpen());
	TestFalse(TEXT("Cleared regions stop blocking"), Door->HasBlockingPlayer());

	MoveCharacter(Partner, FVector(-60.0f, 0.0f, 230.0f));
	Controller->Possess(Partner);
	TestTrue(TEXT("Flying Partner is protected"), Door->HasBlockingPlayer());
	MoveCharacter(Partner, FVector(2500.0f, 0.0f, 0.0f));
	MoveCharacter(Enemy, FVector(0.0f, 0.0f, 0.0f));
	TestFalse(TEXT("AI or unpossessed enemy does not block"), Door->HasBlockingPlayer());
	APlayerState* StalePlayerState = World->SpawnActor<APlayerState>();
	if (!TestNotNull(TEXT("PlayerState-only regression fixture exists"), StalePlayerState))
	{
		Cleanup();
		return false;
	}
	// PlayerState만 남아 있어도 현재 PlayerController가 없으면 보호 대상이 아니다.
	Enemy->SetPlayerState(StalePlayerState);
	TestTrue(TEXT("UE player-controlled query recognizes PlayerState alone"), Enemy->IsPlayerControlled());
	TestFalse(TEXT("PlayerState alone does not block the door"), Door->HasBlockingPlayer());
	Enemy->SetPlayerState(nullptr);
	Controller->Possess(Enemy);
	TestTrue(TEXT("Enemy possession inside the closing path immediately blocks"), Door->HasBlockingPlayer());
	Controller->UnPossess();
	TestFalse(TEXT("Unpossession immediately removes protection"), Door->HasBlockingPlayer());
	Controller->Possess(Enemy);
	Enemy->Destroy();
	SyncPhysics();
	TestFalse(TEXT("Destroyed Pawn leaves no stale protection"), Door->HasBlockingPlayer());

	// 몸체는 밖에 두고 장식용 충돌만 안으로 옮긴다. Actor 소유권만으로
	// 플레이어를 감지하면 무기/장식까지 차단하는 회귀가 발생한다.
	Controller->Possess(Shooter);
	UBoxComponent* Decoration = NewObject<UBoxComponent>(Shooter);
	Decoration->SetBoxExtent(FVector(20.0f));
	Decoration->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Decoration->SetCollisionResponseToAllChannels(ECR_Block);
	Decoration->RegisterComponent();
	Decoration->SetWorldLocation(FVector::ZeroVector);
	SyncPhysics();
	TestFalse(TEXT("Decoration collision alone does not block"), Door->HasBlockingPlayer());
	TestTrue(TEXT("A new clear close request succeeds"), Door->TrySetDoorOpen(false));
	TestFalse(TEXT("Accepted close changes the target state"), Door->IsDoorOpen());

	Cleanup();
	return true;
}

#endif
