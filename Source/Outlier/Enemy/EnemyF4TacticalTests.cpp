#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Enemy/EnemyStateTreeTacticalTasks.h"
#include "Misc/AutomationTest.h"

#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyEnvQuery.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StateTreeComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomTagComponent.h"
#include "Room/RoomVolume.h"
#include "Shooter/ShooterCharacter.h"
#include <limits>



namespace
{
struct FF4TacticalWorld
{
	UWorld* World = nullptr;
	UGameInstance* GameInstance = nullptr;
	UClass* GunClass = nullptr;
	AActor* Target = nullptr;
	UEnemyRoomSubsystem* Rooms = nullptr;
	AEnemyBase* First = nullptr;
	AEnemyBase* Second = nullptr;

	~FF4TacticalWorld()
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
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("EnemyF4Tactical")), GetTransientPackage());
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
		const FGameplayTag RoomTag = FGameplayTag::RequestGameplayTag(TEXT("Room.Level01.1"));
		URoomCombatDefinition* Definition = NewObject<URoomCombatDefinition>(World);
		FRoomCombatRoomDefinition& RoomDefinition = Definition->RoomDefinitions.AddDefaulted_GetRef();
		RoomDefinition.RoomTag = RoomTag;
		RoomDefinition.CombatPhases.AddDefaulted();
		URoomCombatSubsystem* Combat = World->GetSubsystem<URoomCombatSubsystem>();
		Combat->SetCombatDefinitionForTesting(Definition);
		ARoomVolume* Room = World->SpawnActor<ARoomVolume>();
		UBoxComponent* RoomBox = CastChecked<UBoxComponent>(Room->GetRootComponent());
		RoomBox->SetBoxExtent(FVector(6000,6000,2000));
		RoomBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		RoomBox->SetCollisionResponseToAllChannels(ECR_Overlap);
		Combat->RegisterRoom(Room, RoomTag);
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
		World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
		World->GetPhysicsScene()->Flush();
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
		Enemy->GetRoomTagComp()->AssignDefaultRoomTag(FGameplayTag::RequestGameplayTag(TEXT("Room.Level01.1")));
		Enemy->FlightHeightSettings.bEnabled = false;
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


IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyF4YieldCandidateTest,
	"Outlier.Enemy.Combat.F4Tactical.YieldCandidates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyF4YieldCandidateTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Case : {TEXT("BoundedLocalEscape"), TEXT("LargeCapsule"), TEXT("CapsuleOriginOffset"),
		TEXT("DegenerateSegment"), TEXT("InvalidGeometry")})
	{
		Names.Add(Case);
		Commands.Add(Case);
	}
}

bool FEnemyF4YieldCandidateTest::RunTest(const FString& Parameters)
{
	const FVector Origin(0, 0, 400);
	const FVector Offset = Parameters == TEXT("CapsuleOriginOffset") ? FVector(0, 50, 40) : FVector::ZeroVector;
	const float Radius = Parameters == TEXT("LargeCapsule") ? 120.0f : 34.0f;
	const float HalfHeight = FMath::Max(Radius, 88.0f);
	const FVector Start = Origin + Offset - FVector(600, 0, 0);
	const FVector End = Parameters == TEXT("DegenerateSegment") ? Start : Origin + Offset + FVector(1000, 0, 0);
	TArray<FVector> Destinations;
	if (Parameters == TEXT("InvalidGeometry"))
	{
		EnemySearchTesting::BuildYieldDestinations(Origin, Offset, Radius, HalfHeight, 5.0f,
			std::numeric_limits<float>::quiet_NaN(), Start, End, Destinations);
		TestTrue(TEXT("Nonfinite radius cannot introduce an unvalidated destination"), Destinations.IsEmpty());
		EnemySearchTesting::BuildYieldDestinations(Origin, Offset, -1.0f, HalfHeight, 5.0f,
			150.0f, Start, End, Destinations);
		TestTrue(TEXT("Invalid capsule cannot fabricate clearance"), Destinations.IsEmpty());
		return !HasAnyErrors();
	}
	EnemySearchTesting::BuildYieldDestinations(Origin, Offset, Radius, HalfHeight, 5.0f,
		150.0f, Start, End, Destinations);
	TestTrue(TEXT("Local candidate set is nonempty and bounded to eight"), !Destinations.IsEmpty() && Destinations.Num() <= 8);
	const double InnerRadius = FMath::Max(150.0f, Radius * 2.0f + 5.0f);
	for (const FVector& Point : Destinations)
	{
		const FVector Center = Point + Offset;
		const FVector Closest = FMath::ClosestPointOnSegment(Center, Start, End);
		TestTrue(TEXT("Every destination remains in the finite local radius"),
			FVector::Distance(Point, Origin) <= InnerRadius * 2.0 + 0.01);
		TestTrue(TEXT("Candidate vacates the actual capsule corridor"),
			FMath::Abs(Center.Z - Closest.Z) > HalfHeight * 2.0 + 5.0
			|| FVector::DistSquared2D(Center, Closest) > FMath::Square(Radius * 2.0 + 5.0));
		TestTrue(TEXT("Yield does not invent a vertical escape"), FMath::IsNearlyEqual(Point.Z, Origin.Z));
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyF4YieldSnapshotTest,
	"Outlier.Enemy.Combat.F4Tactical.YieldSnapshotRequiresLiveOwner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyF4YieldSnapshotTest::RunTest(const FString& Parameters)
{
	FEnemyRunEnvQueryTaskInstanceData Data;
	TestTrue(TEXT("Ordinary selection has no cooperative-yield ownership requirement"),
		EnemySearchTesting::IsYieldSnapshotCurrent(Data));
	Data.CapturedCombatYieldSerial = 1;
	TestFalse(TEXT("A late yield result cannot apply after its owning Pawn disappears"),
		EnemySearchTesting::IsYieldSnapshotCurrent(Data));
	return !HasAnyErrors();
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyF4YieldSelectorTest,
	"Outlier.Enemy.Combat.F4Tactical.RealSelector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyF4YieldSelectorTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Case : {TEXT("SafePathReserved"), TEXT("NoSafePath"), TEXT("RepeatedNoSafePath"), TEXT("RepeatedNoSafePathTime"), TEXT("OldYieldCannotApply")})
	{
		Names.Add(Case);
		Commands.Add(Case);
	}
}

bool FEnemyF4YieldSelectorTest::RunTest(const FString& Parameters)
{
	FF4TacticalWorld Fixture;
	if (!TestTrue(TEXT("Real authoritative Gun world initializes"), Fixture.Initialize()))
	{
		return false;
	}
	Fixture.First->BeginCombatReservationRequest();
	Fixture.Second->BeginCombatReservationRequest();
	const FVector Start = Fixture.First->GetActorLocation();
	const FVector End = Fixture.Second->GetActorLocation() + FVector(500,0,0);
	if (!TestTrue(TEXT("Cooperative request is admitted"), Fixture.Rooms->RequestCombatYield(Fixture.First, Fixture.Second, Start, End)))
	{
		return false;
	}
	FEnemyRunEnvQueryTaskInstanceData Query;
	Query.Enemy = Fixture.Second;
	Query.bValidateReachability = true;
	Query.RunMode = EEnvQueryRunMode::AllMatching;
	Query.MaxSearchMilliseconds = 1000;
	Query.MaxCollisionChecks = 4096;
	Query.MaxPathCandidates = 32;
	Query.QueryTemplate = NewObject<UEnvQuery>(Fixture.World);
	UEnvQueryOption* Option = NewObject<UEnvQueryOption>(Query.QueryTemplate);
	Query.QueryTemplate->GetOptionsMutable().Add(Option);
	Option->Generator = NewObject<UEnemyEnvQueryGenerator_Approach>(Option);
	Option->Tests.Add(NewObject<UEnemyEnvQueryTest_FlightDestination>(Option));
	EnemySearchTesting::CaptureQuery(Query);
	if (!TestTrue(TEXT("Pending yield captures its own selector identity"), Query.CapturedCombatYieldSerial != 0 && Query.bUnifiedCombatNavigation))
	{
		return false;
	}
	if (Parameters.StartsWith(TEXT("RepeatedNoSafePath")))
	{
		TestTrue(TEXT("First real selection consumes the episode request allowance"), Fixture.Second->TryBeginCombatTacticalRequest());
	}
	EnemySearchTesting::BeginUnifiedSelection(Query);
	TestTrue(TEXT("Real selector contains at most eight deliberate yield candidates"),
		!Query.CombatCandidates.IsEmpty() && Query.CombatCandidates.Num() <= 8);
	for (bool bProgressOnly : Query.CombatCandidatesProgressOnly)
	{
		TestTrue(TEXT("Yield never inherits attack-endpoint authority"), bProgressOnly);
	}
	if (Parameters == TEXT("OldYieldCannotApply"))
	{
		Fixture.Rooms->RequestCombatYield(Fixture.First, Fixture.Second, Start + FVector(200,0,0), End + FVector(200,0,0));
		TestFalse(TEXT("Geometry replacement invalidates captured request"), EnemySearchTesting::IsYieldSnapshotCurrent(Query));
		TestEqual(TEXT("Late selection cannot publish old serial"), EnemySearchTesting::ResumeCombatSelection(Query), EStateTreeRunStatus::Failed);
		TestFalse(TEXT("No old destination is published"), Query.bHasDestination);
		return !HasAnyErrors();
	}
	if (Parameters == TEXT("NoSafePath") || Parameters.StartsWith(TEXT("RepeatedNoSafePath")))
	{
		// 후보 끝점을 실제 충돌로 막는다. 일반 공격 후보가 이 차례에 섞이면 성공으로 새어 나간다.
		for (const auto& Candidate : Query.CombatCandidates)
		{
			AActor* Obstacle = Fixture.World->SpawnActor<AActor>();
			UBoxComponent* Box = NewObject<UBoxComponent>(Obstacle);
			Obstacle->SetRootComponent(Box);
			Box->SetBoxExtent(FVector(35,35,150));
			Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Box->SetCollisionObjectType(ECC_WorldStatic);
			Box->SetCollisionResponseToAllChannels(ECR_Block);
			Box->RegisterComponent();
			Box->SetWorldLocation(Candidate.Destination);
		}
		Fixture.World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
		Fixture.World->GetPhysicsScene()->Flush();
	}
	EStateTreeRunStatus Status = EStateTreeRunStatus::Running;
	for (int32 Step = 0; Step < 512 && Status == EStateTreeRunStatus::Running; ++Step)
	{
		++GFrameCounter;
		Status = EnemySearchTesting::ResumeCombatSelection(Query);
	}
	if (Parameters == TEXT("NoSafePath") || Parameters.StartsWith(TEXT("RepeatedNoSafePath")))
	{
		TestEqual(TEXT("No safe local path ends finitely"), Status, EStateTreeRunStatus::Failed);
		TestFalse(TEXT("Blocked local yield cannot publish destination"), Query.bHasDestination);
	}
	else
	{
		TestEqual(TEXT("Safe local yield passes production path validator"), Status, EStateTreeRunStatus::Succeeded);
		TestTrue(TEXT("Yield publishes validated route"), Query.bHasDestination && Query.SelectedWaypoints.Num() >= 2);
		TestTrue(TEXT("Published route owns reservation"), Fixture.Rooms->IsCombatReservationCurrent(Fixture.Second, Query.CombatReservationRequest));
		FVector CurrentStart, CurrentEnd;
		uint64 CurrentSerial = 0;
		TestTrue(TEXT("Accepted movement keeps yield identity"), Fixture.Second->GetCombatYieldContext(CurrentStart, CurrentEnd, CurrentSerial) && CurrentSerial == Query.CapturedCombatYieldSerial);
		TestFalse(TEXT("Deliberate yield pauses attack"), Fixture.Second->CanAttackTrackedTarget(Fixture.Target));
	}
	if (Parameters.StartsWith(TEXT("RepeatedNoSafePath")))
	{
		const uint64 EpisodeSerial = Fixture.Second->GetCombatTacticalEpisodeSerial();
		uint64 PreviousYieldSerial = Query.CapturedCombatYieldSerial;
		const TArray<FEnemyOrbitCandidate> FailedCandidates = Query.CombatCandidates;
		Fixture.Second->ReleaseCombatReservation(Query.CombatReservationRequest);
		const int32 Limit = Fixture.Second->GetRuntimeStat().TacticalMaxRequests;
		for (int32 Attempt = 1; Attempt < Limit; ++Attempt)
		{
			TestTrue(TEXT("Same conflict may issue a new cooperative request"),
				Fixture.Rooms->RequestCombatYield(Fixture.First, Fixture.Second, Start, End));
			EnemySearchTesting::CaptureQuery(Query);
			TestTrue(TEXT("New claim receives a distinct serial"), Query.CapturedCombatYieldSerial != 0 && Query.CapturedCombatYieldSerial != PreviousYieldSerial);
			PreviousYieldSerial = Query.CapturedCombatYieldSerial;
			TestEqual(TEXT("A new yield claim retains the whole tactical episode"), Fixture.Second->GetCombatTacticalEpisodeSerial(), EpisodeSerial);
			for (const auto& Candidate : FailedCandidates)
			{
				TestTrue(TEXT("Same physical candidate failures survive a new yield serial"),
					Fixture.Second->IsCombatTacticalCandidateFailure(TEXT("YieldPosition"), Candidate.Destination));
			}
			if (Parameters == TEXT("RepeatedNoSafePathTime"))
			{
				Fixture.Advance(Fixture.Second->GetRuntimeStat().TacticalMaxSeconds + 0.1f);
				TestFalse(TEXT("Refreshing a cooperative claim cannot restart the episode clock"), Fixture.Second->CanContinueCombatTacticalRequest());
				TestTrue(TEXT("Expired episode enters bounded observation"), Fixture.Second->IsCombatObservationWaiting());
				return !HasAnyErrors();
			}
			TestTrue(TEXT("Only the remaining request allowance is available"), Fixture.Second->TryBeginCombatTacticalRequest());
			EnemySearchTesting::BeginUnifiedSelection(Query);
			Status = EStateTreeRunStatus::Running;
			for (int32 Step = 0; Step < 512 && Status == EStateTreeRunStatus::Running; ++Step)
			{
				++GFrameCounter;
				Status = EnemySearchTesting::ResumeCombatSelection(Query);
			}
			TestEqual(TEXT("Repeated unsafe selection still finishes without moving"), Status, EStateTreeRunStatus::Failed);
			TestFalse(TEXT("Failure cache cannot leak an unvalidated destination"), Query.bHasDestination);
			Fixture.Second->ReleaseCombatReservation(Query.CombatReservationRequest);
		}
		TestFalse(TEXT("Repeated fresh yield serials cannot replenish request allowance"), Fixture.Second->TryBeginCombatTacticalRequest());
		TestTrue(TEXT("Request exhaustion enters bounded observation"), Fixture.Second->IsCombatObservationWaiting());
	}
	Fixture.Second->ReleaseCombatReservation(Query.CombatReservationRequest);
	return !HasAnyErrors();
}
#endif
