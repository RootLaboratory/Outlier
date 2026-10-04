#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Enemy/EnemyBase.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
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

namespace
{
	// 이동 검증도 전체 World Tick 없이 실제 물리 조회와 Door Tick만 진행한다.
	struct FScopedDoorSafetyWorld
	{
		UWorld* World = nullptr;

		FScopedDoorSafetyWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false,
				MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("DoorSafetyMotionTest")));
			if (World)
			{
				GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
				World->InitializeActorsForPlay(FURL());
			}
		}

		~FScopedDoorSafetyWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
				GEngine->DestroyWorldContext(World);
				World->RemoveFromRoot();
			}
		}

		AInteractableDoor* SpawnMovingDoor()
		{
			UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			AInteractableDoor* Door = World->SpawnActor<AInteractableDoor>();
			if (!Cube || !Door)
			{
				return nullptr;
			}
			Door->DoorMeshLeft->SetStaticMesh(Cube);
			Door->DoorMeshRight->SetStaticMesh(Cube);
			Door->DoorMeshLeft->SetRelativeLocation(FVector(-60.0f, 0.0f, 0.0f));
			Door->DoorMeshRight->SetRelativeLocation(FVector(60.0f, 0.0f, 0.0f));
			Door->DoorMeshLeft->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Door->DoorMeshRight->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Door->DoorCurve = NewObject<UCurveFloat>(Door);
			Door->DoorCurve->FloatCurve.AddKey(0.0f, 0.0f);
			Door->DoorCurve->FloatCurve.AddKey(1.0f, 1.0f);
			Door->bInitiallyOpen = true;
			Door->DispatchBeginPlay();
			return Door;
		}

		void SyncPhysics() const
		{
			World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
			World->GetPhysicsScene()->Flush();
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDoorPlayerSafetyMotionTest,
	"Outlier.Interaction.DoorPlayerSafety.ClosingMotion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDoorPlayerSafetyMotionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FScopedDoorSafetyWorld Fixture;
	UWorld* World = Fixture.World;
	if (!TestNotNull(TEXT("Motion test world exists"), World)
		|| !TestNotNull(TEXT("Motion test physics scene exists"), World->GetPhysicsScene()))
	{
		return false;
	}
	AInteractableDoor* Door = Fixture.SpawnMovingDoor();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	AShooterCharacter* Shooter = ShooterClass ? World->SpawnActor<AShooterCharacter>(
		ShooterClass, FVector(2000.0f, 0.0f, 0.0f), FRotator::ZeroRotator) : nullptr;
	APartnerCharacter* Partner = World->SpawnActor<APartnerCharacter>(
		APartnerCharacter::StaticClass(), FVector(2500.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	AEnemyBase* Enemy = World->SpawnActor<AEnemyBase>(
		AEnemyBase::StaticClass(), FVector(3000.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Moving door exists"), Door)
		|| !TestNotNull(TEXT("Motion controller exists"), Controller)
		|| !TestNotNull(TEXT("Motion Shooter exists"), Shooter)
		|| !TestNotNull(TEXT("Motion Partner exists"), Partner)
		|| !TestNotNull(TEXT("Motion enemy exists"), Enemy))
	{
		return false;
	}
	ACharacter* Characters[] = { Shooter, Partner, Enemy };
	for (ACharacter* Character : Characters)
	{
		Character->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Character->GetCapsuleComponent()->SetGenerateOverlapEvents(false);
	}
	Fixture.SyncPhysics();
	int32 CloseFinishedCount = 0;
	int32 OpenFinishedCount = 0;
	int32 SafetyReopenCount = 0;
	Door->OnDoorMotionFinished.AddLambda([&](AInteractableDoor*, bool bOpen)
	{
		if (bOpen)
		{
			++OpenFinishedCount;
		}
		else
		{
			++CloseFinishedCount;
		}
	});
	Door->OnDoorSafetyReopenStarted.AddLambda([&](AInteractableDoor*) { ++SafetyReopenCount; });
	const FVector EntryLocations[] = {
		FVector(-60.0f, 0.0f, 150.0f), FVector(60.0f, 0.0f, 230.0f), FVector::ZeroVector
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Characters); ++Index)
	{
		Controller->UnPossess();
		for (ACharacter* Character : Characters)
		{
			Character->SetActorLocation(FVector(2000.0f, 0.0f, 0.0f));
		}
		ACharacter* Character = Characters[Index];
		if (Character == Enemy)
		{
			// AI가 이미 범위 안에 있는 상태에서 닫기를 시작하고, 이동 중 빙의만 전환한다.
			Character->SetActorLocation(EntryLocations[Index]);
		}
		else
		{
			Controller->Possess(Character);
		}
		Fixture.SyncPhysics();
		TestTrue(TEXT("Clear or AI-only close starts"), Door->TrySetDoorOpen(false));
		static_cast<AActor*>(Door)->Tick(0.4f);
		TestTrue(TEXT("Door is midway through closing"),
			FMath::IsNearlyEqual(Door->DoorTimeline.GetPlaybackPosition(), 0.6f));
		const FVector LeftBeforeEntry = Door->DoorMeshLeft->GetRelativeLocation();
		const FVector RightBeforeEntry = Door->DoorMeshRight->GetRelativeLocation();
		Character->SetActorLocation(EntryLocations[Index]);
		Controller->Possess(Character);
		Fixture.SyncPhysics();
		if (!TestTrue(TEXT("Current player body is detected during closing"), Door->HasBlockingPlayer()))
		{
			return false;
		}
		// 이 Delta라면 닫힌 끝까지 갈 수 있다. 감지는 이동/완료보다 먼저 실행되어야 한다.
		static_cast<AActor*>(Door)->Tick(0.7f);
		TestTrue(TEXT("Safety changes the target to open"), Door->IsDoorOpen());
		TestTrue(TEXT("Left leaf does not close further on the detection frame"),
			Door->DoorMeshLeft->GetRelativeLocation().Equals(LeftBeforeEntry));
		TestTrue(TEXT("Right leaf does not close further on the detection frame"),
			Door->DoorMeshRight->GetRelativeLocation().Equals(RightBeforeEntry));
		TestTrue(TEXT("Safety reverses the timeline"), !Door->DoorTimeline.IsReversing());
		TestEqual(TEXT("Each interruption emits one safety signal"), SafetyReopenCount, Index + 1);
		TestFalse(TEXT("Occupied close during safety opening is rejected"), Door->TrySetDoorOpen(false));
		TestTrue(TEXT("An identical open request does not replace safety motion"), Door->TrySetDoorOpen(true));
		Character->SetActorLocation(FVector(2000.0f, 0.0f, 0.0f));
		Fixture.SyncPhysics();
		static_cast<AActor*>(Door)->Tick(0.5f);
		TestTrue(TEXT("Safety opens the left leaf fully"), Door->DoorMeshLeft->GetRelativeLocation().Equals(
			FVector(-60.0f, 0.0f, 0.0f) + Door->OpenOffsetLeft));
		TestTrue(TEXT("Safety opens the right leaf fully"), Door->DoorMeshRight->GetRelativeLocation().Equals(
			FVector(60.0f, 0.0f, 0.0f) + Door->OpenOffsetRight));
		TestFalse(TEXT("Completed safety opening disables Tick"), Door->IsActorTickEnabled());
		static_cast<AActor*>(Door)->Tick(1.1f);
		TestTrue(TEXT("Clearance never creates an automatic close"), Door->IsDoorOpen());
		TestEqual(TEXT("Interrupted closing never completes"), CloseFinishedCount, 0);
		TestEqual(TEXT("Safety opening never emits normal opening completion"), OpenFinishedCount, 0);
	}

	TestTrue(TEXT("New explicit clear close succeeds after safety opening"), Door->TrySetDoorOpen(false));
	static_cast<AActor*>(Door)->Tick(1.1f);
	TestEqual(TEXT("New normal close completes once"), CloseFinishedCount, 1);
	TestFalse(TEXT("Normal close finishes with Tick disabled"), Door->IsActorTickEnabled());
	TestTrue(TEXT("Normal open still succeeds"), Door->TrySetDoorOpen(true));
	static_cast<AActor*>(Door)->Tick(1.1f);
	TestEqual(TEXT("New normal opening completes once"), OpenFinishedCount, 1);

	// 첫 닫힘 프레임 전에 진입하면 이미 열린 끝점에서 안전 취소된다.
	// 같은 목표의 반복 요청도 검사를 생략하거나 이전 Reverse 재생을 남기면 안 된다.
	Controller->Possess(Shooter);
	TestTrue(TEXT("Immediate-interruption close starts while clear"), Door->TrySetDoorOpen(false));
	Shooter->SetActorLocation(EntryLocations[0]);
	Fixture.SyncPhysics();
	TestFalse(TEXT("Repeated close rejects a new body before the first motion frame"), Door->TrySetDoorOpen(false));
	TestTrue(TEXT("Immediate safety cancellation retains the open target"), Door->IsDoorOpen());
	TestFalse(TEXT("Endpoint reversal stops the earlier closing timeline"), Door->DoorTimeline.IsPlaying());
	TestFalse(TEXT("Endpoint reversal disables idle Tick"), Door->IsActorTickEnabled());
	static_cast<AActor*>(Door)->Tick(1.1f);
	TestTrue(TEXT("A cancelled endpoint never resumes closing"),
		Door->DoorMeshLeft->GetRelativeLocation().Equals(FVector(-180.0f, 0.0f, 0.0f)));
	TestEqual(TEXT("Endpoint cancellation never completes closing"), CloseFinishedCount, 1);
	TestEqual(TEXT("Endpoint cancellation never completes normal opening"), OpenFinishedCount, 1);
	TestEqual(TEXT("Endpoint cancellation emits one safety signal"), SafetyReopenCount, 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDoorPlayerSafetyReplicationTest,
	"Outlier.Interaction.DoorPlayerSafety.MotionSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDoorPlayerSafetyReplicationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FScopedDoorSafetyWorld Fixture;
	if (!TestNotNull(TEXT("Snapshot test world exists"), Fixture.World))
	{
		return false;
	}
	AInteractableDoor* Door = Fixture.SpawnMovingDoor();
	AGameStateBase* GameState = Fixture.World->SpawnActor<AGameStateBase>();
	if (!TestNotNull(TEXT("Snapshot door exists"), Door)
		|| !TestNotNull(TEXT("Snapshot clock exists"), GameState))
	{
		return false;
	}
	Fixture.World->SetGameState(GameState);
	FDoorMotionState Closing;
	Closing.Revision = 10;
	Closing.bMoving = true;
	Closing.PlaybackPosition = 0.9f;
	Closing.ServerTimeSeconds = GameState->GetServerWorldTimeSeconds() - 0.2;
	Door->ApplyReplicatedDoorMotion(Closing);
	TestTrue(TEXT("Closing snapshot compensates elapsed server time"),
		FMath::IsNearlyEqual(Door->DoorTimeline.GetPlaybackPosition(), 0.7f));
	TestTrue(TEXT("Closing snapshot reverses the timeline"), Door->DoorTimeline.IsReversing());
	FDoorMotionState SafetyOpening = Closing;
	SafetyOpening.Revision = 11;
	SafetyOpening.bOpen = true;
	SafetyOpening.PlaybackPosition = 0.4f;
	Door->ApplyReplicatedDoorMotion(SafetyOpening);
	TestTrue(TEXT("Safety snapshot uses server position rather than local closing position"),
		FMath::IsNearlyEqual(Door->DoorTimeline.GetPlaybackPosition(), 0.6f));
	TestTrue(TEXT("Safety snapshot plays toward open"), !Door->DoorTimeline.IsReversing());
	Door->DoorTimeline.SetPlaybackPosition(0.8f, false);
	Door->ApplyReplicatedDoorMotion(SafetyOpening);
	Door->ApplyReplicatedDoorMotion(Closing);
	TestTrue(TEXT("Duplicate and stale updates never restart the timeline"),
		FMath::IsNearlyEqual(Door->DoorTimeline.GetPlaybackPosition(), 0.8f));
	TestTrue(TEXT("Stale close cannot override safety opening"), Door->IsDoorOpen());
	FDoorMotionState Finished = SafetyOpening;
	Finished.Revision = 12;
	Finished.bMoving = false;
	Door->ApplyReplicatedDoorMotion(Finished);
	TestFalse(TEXT("Final open snapshot disables Tick"), Door->IsActorTickEnabled());
	TestTrue(TEXT("Final open snapshot places both leaves at their open positions"),
		Door->DoorMeshLeft->GetRelativeLocation().Equals(FVector(-180.0f, 0.0f, 0.0f))
		&& Door->DoorMeshRight->GetRelativeLocation().Equals(FVector(180.0f, 0.0f, 0.0f)));

	// 이동 중 입장/관련성 복귀는 이전 닫힘 패킷 없이 최신 열림 기준점만 받아도 복원된다.
	AInteractableDoor* LateDoor = Fixture.SpawnMovingDoor();
	if (!TestNotNull(TEXT("Late snapshot receiver exists"), LateDoor))
	{
		return false;
	}
	LateDoor->bDoorInitialized = false;
	LateDoor->ReplicatedDoorMotion = SafetyOpening;
	LateDoor->OnRep_DoorMotion();
	TestEqual(TEXT("Early replication waits for timeline initialization"), LateDoor->LastAppliedMotionRevision, 0u);
	LateDoor->ReplicatedDoorMotion = Closing;
	LateDoor->OnRep_DoorMotion();
	LateDoor->bDoorInitialized = true;
	LateDoor->ApplyReplicatedDoorMotion(LateDoor->ReplicatedDoorMotion);
	TestTrue(TEXT("Latest pre-initialization snapshot survives an older property update"),
		FMath::IsNearlyEqual(LateDoor->DoorTimeline.GetPlaybackPosition(), 0.6f));
	TestTrue(TEXT("Latest snapshot alone restores an in-progress opening"), LateDoor->IsDoorOpen());
	SafetyOpening.Revision = 13;
	SafetyOpening.ServerTimeSeconds = GameState->GetServerWorldTimeSeconds() - 2.0;
	LateDoor->ApplyReplicatedDoorMotion(SafetyOpening);
	TestTrue(TEXT("Long-delayed opening clamps to the open endpoint"),
		FMath::IsNearlyEqual(LateDoor->DoorTimeline.GetPlaybackPosition(), 1.0f));
	TestFalse(TEXT("An elapsed motion does not leave idle Tick enabled"), LateDoor->IsActorTickEnabled());
	TestFalse(TEXT("An elapsed motion stops timeline playback"), LateDoor->DoorTimeline.IsPlaying());
	FDoorMotionState LongDelayedClose = Closing;
	LongDelayedClose.Revision = 14;
	LongDelayedClose.ServerTimeSeconds = GameState->GetServerWorldTimeSeconds() - 2.0;
	LateDoor->ApplyReplicatedDoorMotion(LongDelayedClose);
	TestTrue(TEXT("Long-delayed closing clamps to the closed endpoint"),
		FMath::IsNearlyEqual(LateDoor->DoorTimeline.GetPlaybackPosition(), 0.0f));
	TestFalse(TEXT("Elapsed closing disables Tick"), LateDoor->IsActorTickEnabled());
	return true;
}

#endif
