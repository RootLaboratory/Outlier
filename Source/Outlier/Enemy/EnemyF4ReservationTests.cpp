#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/StateTreeComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Room/RoomTagComponent.h"
#include "Shooter/ShooterCharacter.h"

namespace
{
struct FF4ReservationWorld
{
	UWorld* World = nullptr;
	UGameInstance* GameInstance = nullptr;
	UClass* GunClass = nullptr;
	AActor* Target = nullptr;
	UEnemyRoomSubsystem* Rooms = nullptr;
	AEnemyBase* First = nullptr;
	AEnemyBase* Second = nullptr;

	~FF4ReservationWorld()
	{
		if (!World)
		{
			return;
		}
		GEngine->ShutdownWorldNetDriver(World);
		if (World->HasBegunPlay())
		{
			World->EndPlay(EEndPlayReason::Quit);
		}
		World->DestroyWorld(true);
		if (GameInstance)
		{
			GameInstance->Shutdown();
		}
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}

	bool Initialize()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("EnemyF4Reservation")), GetTransientPackage());
		if (!World)
		{
			return false;
		}
		World->AddToRoot();
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		GameInstance = NewObject<UGameInstance>(GEngine);
		World->SetGameInstance(GameInstance);
		GameInstance->Init();
		World->CreateAISystem();
		FURL URL;
		URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
		World->SetGameMode(URL);
		World->InitializeActorsForPlay(URL);
		World->BeginPlay();
		GunClass = LoadClass<AEnemyBase>(nullptr,
			TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"));
		UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
			TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
		if (!GunClass || !ShooterClass)
		{
			return false;
		}
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Target = World->SpawnActor<AActor>(ShooterClass, FVector(3000, 0, 300), FRotator::ZeroRotator, Spawn);
		if (!Target)
		{
			return false;
		}
		Target->SetActorEnableCollision(false);
		First = SpawnGun(FVector(0, -500, 300));
		Second = SpawnGun(FVector(0, 500, 300));
		Rooms = World->GetSubsystem<UEnemyRoomSubsystem>();
		return First && Second && Rooms;
	}

	AEnemyBase* SpawnGun(const FVector& Position)
	{
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AEnemyBase* Enemy = World->SpawnActor<AEnemyBase>(GunClass, Position, FRotator::ZeroRotator, Spawn);
		AEnemyAIController* AI = Enemy ? Cast<AEnemyAIController>(Enemy->GetController()) : nullptr;
		if (!AI)
		{
			return nullptr;
		}
		Enemy->EnterCombatFromRoom(Target->GetActorLocation(), false);
		Enemy->GetStateTreeComponent()->StopLogic(TEXT("Controlled F4 reservation regression"));
		Enemy->SetActorTickEnabled(false);
		Enemy->GetCharacterMovement()->SetComponentTickEnabled(false);
		AI->SetEnemyPerceptionEnabled(false);
		AI->SetActorTickEnabled(false);
		AI->SetCachedVisibleTargetForTesting(Target);
		Enemy->ObserveTrackedTarget(Target);
		Enemy->SetPlayerCurrentlyVisible(true);
		Enemy->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Enemy->EnsureCombatTacticalEpisode(Target, Target->GetActorLocation());
		Enemy->GetRoomTagComp()->AssignDefaultRoomTag(
			FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1"))));
		World->GetSubsystem<UEnemyRoomSubsystem>()->RegisterEnemy(Enemy);
		return Enemy;
	}

	void Advance(float Seconds)
	{
		const int32 Steps = FMath::CeilToInt(Seconds * 60.0f);
		for (int32 Index = 0; Index < Steps; ++Index)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_TimeOnly, Seconds / Steps);
		}
	}

	bool Reserve(AEnemyBase* Enemy, uint64 Request, const FVector& Destination)
	{
		return Rooms->TryReserveCombatDestination(Enemy, Request, Destination, {},
			{Enemy->GetCapsuleComponent()->GetComponentLocation(), Destination}, 5.0f);
	}
};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyF4ReservationTest,
	"Outlier.Enemy.Combat.F4Reservation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyF4ReservationTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Case : {TEXT("TicketOrder"), TEXT("RetryKeepsWaitAge"), TEXT("InactiveDemandCannotWin"),
		TEXT("ExistingOwnerNotStolen"), TEXT("ActualOccupancyNotStolen"), TEXT("YieldSnapshotStable"),
		TEXT("YieldGeometryChangesSerial"), TEXT("YieldStaleFinish"), TEXT("YieldExpires"), TEXT("YieldStun"),
		TEXT("YieldRequesterRelease"), TEXT("YieldBlockerRelease"), TEXT("CrowdingAngle"), TEXT("CrowdingExpires"), TEXT("Reset"),
		TEXT("ReadinessOrder"), TEXT("PriorityHold"), TEXT("StarvationTurn")})
	{
		Names.Add(Case);
		Commands.Add(Case);
	}
}

bool FEnemyF4ReservationTest::RunTest(const FString& Parameters)
{
	FF4ReservationWorld Fixture;
	if (!TestTrue(TEXT("Production Gun reservation world initializes"), Fixture.Initialize()))
	{
		return false;
	}
	UEnemyRoomSubsystem& Rooms = *Fixture.Rooms;
	AEnemyBase* First = Fixture.First;
	AEnemyBase* Second = Fixture.Second;
	const FVector Destination(1500, 0, 300);
	const uint64 FirstRequest = First->BeginCombatReservationRequest();
	const uint64 SecondRequest = Second->BeginCombatReservationRequest();
	if (Parameters == TEXT("ReadinessOrder") || Parameters == TEXT("PriorityHold") || Parameters == TEXT("StarvationTurn"))
	{
		// 비교기는 제한된 주기로 얻은 준비 상태를 입력받는다. 판정 중 새 Trace나 포즈 갱신을 하지 않는다.
		FEnemyCombatReservationRequest Waiting;
		FEnemyCombatReservationRequest Ready;
		Waiting.Ticket = 2;
		Ready.Ticket = 1;
		Ready.bAttackReady = true;
		if (Parameters == TEXT("ReadinessOrder"))
		{
			TestTrue(TEXT("Normal attack readiness wins before ticket order"), Rooms.HasCombatContenderPriority(Second, Ready, First, Waiting));
			Waiting.Ticket = 0;
			TestFalse(TEXT("An earlier non-ready contender does not override an attackable peer"), Rooms.HasCombatContenderPriority(First, Waiting, Second, Ready));
		}
		else if (Parameters == TEXT("PriorityHold"))
		{
			Waiting.PriorityUntil = Fixture.World->GetTimeSeconds() + 1.0;
			TestTrue(TEXT("Short held priority does not flip on a readiness update"), Rooms.HasCombatContenderPriority(First, Waiting, Second, Ready));
			Fixture.Advance(1.1f);
			TestFalse(TEXT("Expired priority cannot permanently suppress readiness"), Rooms.HasCombatContenderPriority(First, Waiting, Second, Ready));
		}
		else
		{
			Waiting.WaitingSince = Fixture.World->GetTimeSeconds();
			Ready.PriorityUntil = Fixture.World->GetTimeSeconds() + 10.0;
			TestFalse(TEXT("Short initial wait respects the current priority"), Rooms.HasCombatContenderPriority(First, Waiting, Second, Ready));
			Fixture.Advance(2.1f);
			TestTrue(TEXT("Long waiter receives the next safe turn despite repeated readiness"), Rooms.HasCombatContenderPriority(First, Waiting, Second, Ready));
		}
		return !HasAnyErrors();
	}
	if (Parameters.StartsWith(TEXT("Yield")) || Parameters == TEXT("Reset"))
	{
		const FVector Start = First->GetActorLocation();
		const FVector End = Second->GetActorLocation() + FVector(500, 0, 0);
		if (!TestTrue(TEXT("Actual cooperative requester can ask a non-ready blocker to evaluate yielding"),
			Rooms.RequestCombatYield(First, Second, Start, End)))
		{
			return false;
		}
		FEnemyCombatYieldContext Before;
		TestTrue(TEXT("Yield context is available"), Rooms.GetCombatYieldContext(Second, Before));
		if (Parameters == TEXT("YieldSnapshotStable"))
		{
			Rooms.RequestCombatYield(First, Second, Start + FVector(0, 1, 0), End + FVector(0, 1, 0));
			FEnemyCombatYieldContext After;
			TestTrue(TEXT("Refreshed request remains valid"), Rooms.GetCombatYieldContext(Second, After));
			TestEqual(TEXT("Small aim jitter retains serial"), After.Serial, Before.Serial);
			TestTrue(TEXT("In-flight validation sees the same protected segment"), After.Start == Before.Start && After.End == Before.End);
			return !HasAnyErrors();
		}
		if (Parameters == TEXT("YieldGeometryChangesSerial") || Parameters == TEXT("YieldStaleFinish"))
		{
			Rooms.RequestCombatYield(First, Second, Start, End + FVector(0, 150, 0));
			FEnemyCombatYieldContext After;
			TestTrue(TEXT("Changed segment remains available"), Rooms.GetCombatYieldContext(Second, After));
			TestNotEqual(TEXT("Different protected segment has a new serial"), Before.Serial, After.Serial);
			Rooms.FinishCombatYield(Second, Before.Serial);
			TestTrue(TEXT("Previous completion cannot erase the new request"), Rooms.GetCombatYieldContext(Second, After));
			Rooms.FinishCombatYield(Second, After.Serial);
		}
		else if (Parameters == TEXT("YieldExpires"))
		{
			Fixture.Advance(1.1f);
		}
		else if (Parameters == TEXT("YieldStun"))
		{
			Second->EnterStun();
		}
		else if (Parameters == TEXT("YieldRequesterRelease"))
		{
			Rooms.ReleaseCombatReservation(First);
		}
		else if (Parameters == TEXT("YieldBlockerRelease"))
		{
			Rooms.ReleaseCombatReservation(Second);
		}
		else if (Parameters == TEXT("Reset"))
		{
			Rooms.ResetRuntimeCombatState();
		}
		FEnemyCombatYieldContext After;
		TestFalse(TEXT("Finished, expired, or interrupted yield is no longer an approval"), Rooms.GetCombatYieldContext(Second, After));
		return !HasAnyErrors();
	}
	if (Parameters.StartsWith(TEXT("Crowding")))
	{
		const FVector Center = First->GetCombatNavigationTargetLocation(Fixture.Target);
		const FVector East = Center + FVector(500, 0, 0);
		TestTrue(TEXT("First observer reserves its actual approach position"), Fixture.Reserve(First, FirstRequest, East));
		if (Parameters == TEXT("CrowdingExpires"))
		{
			Fixture.Advance(1.1f);
			TestEqual(TEXT("Expired future position cannot force old observation angles"), Rooms.GetCombatObservationCrowding(Second, Center, East), 0.0);
		}
		else
		{
			const double Same = Rooms.GetCombatObservationCrowding(Second, Center, Center + FVector(700, 0, 0));
			const double Other = Rooms.GetCombatObservationCrowding(Second, Center, Center + FVector(0, 500, 0));
			TestTrue(TEXT("Different reachable viewing angles receive a lower soft crowding cost"), Same > Other);
			TestEqual(TEXT("Unrelated clue has no angular penalty"), Rooms.GetCombatObservationCrowding(Second, Center + FVector(1000, 1000, 0), East), 0.0);
		}
		return !HasAnyErrors();
	}
	if (Parameters == TEXT("ActualOccupancyNotStolen"))
	{
		Second->SetActorLocation(Destination);
		// 실제 점유는 방 등록 집합을 사용한다. 물리 쿼리와 등록을 모두 확인해 예약 없는 점유자를 검증한다.
		UCapsuleComponent* Capsule = Second->GetCapsuleComponent();
		TArray<FOverlapResult> Overlaps;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(F4ActualOccupancy), false, First);
		Fixture.World->OverlapMultiByObjectType(Overlaps, Capsule->GetComponentLocation(), Capsule->GetComponentQuat(),
			FCollisionObjectQueryParams(Capsule->GetCollisionObjectType()),
			FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Query);
		TestTrue(TEXT("Physics query confirms the live occupying capsule"), Overlaps.ContainsByPredicate(
			[Second](const FOverlapResult& Overlap) { return Overlap.GetActor() == Second; }));
		TestTrue(TEXT("The occupying enemy belongs to the production registered set"), Rooms.RegisteredEnemyKeys.Contains(Second));
		TestFalse(TEXT("A live capsule blocks even when it owns no future reservation"), Fixture.Reserve(First, FirstRequest, Destination));
		return !HasAnyErrors();
	}
	TestTrue(TEXT("First contender acquires the destination"), Fixture.Reserve(First, FirstRequest, Destination));
	TestFalse(TEXT("Second contender cannot steal the live reservation"), Fixture.Reserve(Second, SecondRequest, Destination));
	if (Parameters == TEXT("ExistingOwnerNotStolen"))
	{
		TestTrue(TEXT("Original reservation remains current"), Rooms.IsCombatReservationCurrent(First, FirstRequest));
		return !HasAnyErrors();
	}
	const double WaitingSince = Rooms.CombatReservationRequests.FindChecked(Second).WaitingSince;
	if (Parameters == TEXT("RetryKeepsWaitAge"))
	{
		Fixture.Advance(0.6f);
		Rooms.ReleaseCombatReservation(Second, SecondRequest);
		const uint64 Retried = Second->BeginCombatReservationRequest();
		TestFalse(TEXT("Same obstacle still blocks the retry"), Fixture.Reserve(Second, Retried, Destination));
		TestEqual(TEXT("Same episode retry retains first blocked time"), Rooms.CombatReservationRequests.FindChecked(Second).WaitingSince, WaitingSince);
		TestFalse(TEXT("Old request cannot act on the new retry"), Rooms.IsCombatReservationRequestCurrent(Second, SecondRequest));
		return !HasAnyErrors();
	}
	Rooms.ReleaseCombatReservation(First, FirstRequest);
	if (Parameters == TEXT("InactiveDemandCannotWin"))
	{
		Rooms.ReleaseCombatReservation(Second, SecondRequest);
		const uint64 Fresh = First->BeginCombatReservationRequest();
		TestTrue(TEXT("Inactive contender metadata does not prevent a new safe claim"), Fixture.Reserve(First, Fresh, Destination));
	}
	else
	{
		const uint64 LateRequest = First->BeginCombatReservationRequest();
		TestFalse(TEXT("Re-request does not jump ahead of a waiting Ticket"), Fixture.Reserve(First, LateRequest, Destination));
		TestTrue(TEXT("Next waiting contender acquires the released position"), Fixture.Reserve(Second, SecondRequest, Destination));
	}
	return !HasAnyErrors();
}

#endif
