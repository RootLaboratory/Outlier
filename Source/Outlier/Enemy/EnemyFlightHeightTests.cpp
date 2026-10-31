#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyEnvQuery.h"
#include "Enemy/EnemyPoolSubsystem.h"
#include "Enemy/EnemyStateTreeMovementTasks.h"
#include "Enemy/EnemyStateTreeTacticalTasks.h"
#include "Enemy/EnemyStateTreeSelfDestructTasks.h"
#include "AbilitySystemComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomTagComponent.h"
#include "Room/RoomVolume.h"

#include <limits>

namespace
{
constexpr float HeightTestClearance = 5.0f;
constexpr int32 HeightTestMaxChecks = 512;
constexpr double HeightTestMaxMilliseconds = 1000.0;

struct FEnemyFlightHeightTestWorld
{
	const FGameplayTag RoomTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")));
	UWorld* World = nullptr;
	UGameInstance* GameInstance = nullptr;
	ARoomVolume* Room = nullptr;
	AEnemyBase* Enemy = nullptr;
	UEnemyEnvQueryGenerator_Approach* Approach = nullptr;

	FEnemyFlightHeightTestWorld()
	{
		if (!GEngine) return;
		const FName Name = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("EnemyFlightHeightTest"));
		World = UWorld::CreateWorld(EWorldType::Game, false, Name, GetTransientPackage());
		if (!World) return;
		FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
		World->AddToRoot();
		Context.SetCurrentWorld(World);
		GameInstance = NewObject<UGameInstance>(GEngine);
		World->SetGameInstance(GameInstance);
		GameInstance->Init();
		World->CreateAISystem();
		World->InitializeActorsForPlay(FURL());

		URoomCombatDefinition* Definition = NewObject<URoomCombatDefinition>(GetTransientPackage());
		FRoomCombatRoomDefinition& RoomDefinition = Definition->RoomDefinitions.AddDefaulted_GetRef();
		RoomDefinition.RoomTag = RoomTag;
		RoomDefinition.CombatPhases.AddDefaulted();
		if (URoomCombatSubsystem* Combat = World->GetSubsystem<URoomCombatSubsystem>())
		{
			Combat->SetCombatDefinitionForTesting(Definition);
		}
	}

	~FEnemyFlightHeightTestWorld()
	{
		if (!World) return;
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		if (GameInstance) GameInstance->Shutdown();
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}

	AActor* SpawnBlockingBox(const FVector& Location, const FVector& Extent,
		ECollisionChannel ObjectType = ECC_WorldStatic) const
	{
		AActor* Actor = World ? World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location)) : nullptr;
		if (!Actor) return nullptr;
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
		Box->SetBoxExtent(Extent);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionObjectType(ObjectType);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Actor->SetRootComponent(Box);
		Box->RegisterComponent();
		Box->SetWorldLocation(Location);
		World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
		World->GetPhysicsScene()->Flush();
		return Actor;
	}

	bool Initialize(bool bFloor = true, const FVector& Start = FVector(0, 0, 400),
		const FVector& RoomExtent = FVector(6000, 6000, 2000))
	{
		if (!World || !World->GetPhysicsScene()) return false;
		Room = World->SpawnActor<ARoomVolume>(ARoomVolume::StaticClass(), FTransform::Identity);
		if (!Room) return false;
		UBoxComponent* RoomBox = Cast<UBoxComponent>(Room->GetRootComponent());
		if (!RoomBox) return false;
		RoomBox->SetBoxExtent(RoomExtent);
		RoomBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		// 기본 정적 채널의 방 트리거도 바닥으로 오인하지 않는지 함께 검증한다.
		RoomBox->SetCollisionObjectType(ECC_WorldStatic);
		RoomBox->SetCollisionResponseToAllChannels(ECR_Overlap);
		URoomCombatSubsystem* Combat = World->GetSubsystem<URoomCombatSubsystem>();
		if (!Combat || !Combat->RegisterRoom(Room, RoomTag)) return false;
		if (bFloor && !SpawnBlockingBox(FVector(0, 0, -25), FVector(10000, 10000, 25))) return false;

		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Enemy = World->SpawnActor<AEnemyBase>(AEnemyBase::StaticClass(), FTransform(Start), Spawn);
		if (!Enemy) return false;
		Enemy->GetRoomTagComp()->AssignDefaultRoomTag(RoomTag);
		UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent();
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Capsule->SetCollisionObjectType(ECC_Pawn);
		Capsule->SetCollisionResponseToAllChannels(ECR_Block);
		Capsule->SetGenerateOverlapEvents(false);
		Enemy->FlightHeightSettings.bEnabled = true;
		Enemy->FlightHeightSettings.bRandomizePreference = false;
		Enemy->FlightHeightSettings.FixedPreference = EEnemyFlightHeightPreference::Mid;
		Enemy->EnterCombatFromRoom(FVector(3000, 0, 50), false);

		Approach = NewObject<UEnemyEnvQueryGenerator_Approach>(World);
		Approach->AngularSamples = 1;
		Approach->DistanceSpread = 0.0f;
		World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
		World->GetPhysicsScene()->Flush();
		return true;
	}

	EFlightPathStatus Prepare(FEnemyFlightHeightSnapshot& Snapshot, int32 MaxChecks = HeightTestMaxChecks) const
	{
		return EnemyFlightHeight::PrepareQuery(*Enemy, *Approach, Enemy->GetActorLocation(),
			FVector(3000, 0, 50), 400.0f, true, 0, HeightTestClearance,
			MaxChecks, HeightTestMaxMilliseconds, Snapshot);
	}

	double VerticalExtent() const
	{
		const UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent();
		return FlightPathfinding::GetCapsuleWorldExtent(Capsule->GetScaledCapsuleRadius() + HeightTestClearance,
			Capsule->GetScaledCapsuleHalfHeight() + HeightTestClearance, Capsule->GetComponentQuat()).Z;
	}
};

bool CheckCandidates(FAutomationTestBase& Test, const FEnemyFlightHeightSnapshot& Snapshot,
	EFlightPathStatus Status, double ExpectedZ = 0.0)
{
	bool bPassed = Test.TestTrue(TEXT("Snapshot contains candidates"), !Snapshot.Candidates.IsEmpty());
	for (const FEnemyFlightHeightCandidate& Candidate : Snapshot.Candidates)
	{
		bPassed &= Test.TestEqual(TEXT("Candidate has expected status"), Candidate.Status, Status);
		if (Status == EFlightPathStatus::Success)
		{
			bPassed &= Test.TestTrue(TEXT("Candidate has expected actor-origin height"),
				FMath::IsNearlyEqual(Candidate.Location.Z, ExpectedZ, 0.01));
		}
	}
	return bPassed;
}

bool MakeHeightCombatQuery(FEnemyFlightHeightTestWorld& Fixture, const FString& Mode,
	FEnemyRunEnvQueryTaskInstanceData& Data, double TargetHeight = 50.0)
{
	AEnemyAIController* Controller = Fixture.World->SpawnActor<AEnemyAIController>();
	AActor* Target = Fixture.World->SpawnActor<AActor>();
	if (!Controller || !Target) return false;
	UBoxComponent* TargetRoot = NewObject<UBoxComponent>(Target);
	Target->SetRootComponent(TargetRoot);
	TargetRoot->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TargetRoot->RegisterComponent();
	const double TargetDistance = Mode == TEXT("Approach") ? 3000.0
		: Mode == TEXT("Orbit") ? 1500.0 : Mode == TEXT("Retreat") ? 500.0 : 1000.0;
	Target->SetActorLocation(FVector(TargetDistance, 0, TargetHeight));
	Controller->Possess(Fixture.Enemy);
	Controller->SetCachedVisibleTargetForTesting(Target);
	Fixture.Enemy->SetPlayerCurrentlyVisible(true);

	Data.Enemy = Fixture.Enemy;
	Data.bValidateReachability = Mode == TEXT("Approach");
	Data.bValidateOrbitPath = Mode == TEXT("Orbit");
	Data.bValidateRetreatPath = Mode == TEXT("Retreat");
	Data.bValidateChasePath = Mode == TEXT("Chase");
	Data.RunMode = EEnvQueryRunMode::AllMatching;
	Data.MaxSearchMilliseconds = HeightTestMaxMilliseconds;
	Data.MaxCollisionChecks = 4096;
	Data.MaxPathCandidates = 32;
	Data.QueryTemplate = NewObject<UEnvQuery>(Fixture.World);
	UEnvQueryOption* Option = NewObject<UEnvQueryOption>(Data.QueryTemplate);
	Data.QueryTemplate->GetOptionsMutable().Add(Option);
	if (Data.bValidateReachability)
	{
		UEnemyEnvQueryGenerator_Approach* Generator = NewObject<UEnemyEnvQueryGenerator_Approach>(Option);
		Generator->AngularSamples = 3;
		Generator->DistanceSpread = 0;
		Option->Generator = Generator;
	}
	else if (Data.bValidateOrbitPath) Option->Generator = NewObject<UEnemyEnvQueryGenerator_Orbit>(Option);
	else if (Data.bValidateRetreatPath) Option->Generator = NewObject<UEnemyEnvQueryGenerator_Retreat>(Option);
	else if (Data.bValidateChasePath) Option->Generator = NewObject<UEnemyEnvQueryGenerator_Chase>(Option);
	else return false;
	Option->Tests.Add(NewObject<UEnemyEnvQueryTest_FlightDestination>(Option));
	EnemySearchTesting::CaptureQuery(Data);
	// 무작위 선회 길이 대신 재현 가능한 3.5m를 사용한다. 높이 보정/실제 질의/경로 검사는 그대로 실행한다.
	Data.CapturedOrbitLength = Data.bValidateOrbitPath ? 350.0f : 0.0f;
	Data.QueryResult = EEnemyEnvQueryResult::Running;
	return true;
}

bool FinishHeightQuery(FAutomationTestBase& Test, FEnemyFlightHeightTestWorld& Fixture,
	FEnemyRunEnvQueryTaskInstanceData& Data, FEnvQueryRequest& Request,
	TSharedPtr<FEnvQueryResult>& OutResult, TFunctionRef<void()> BeforeCompletion)
{
	UEnvQueryManager* Manager = UEnvQueryManager::GetCurrent(Fixture.World);
	if (!Test.TestNotNull(TEXT("EQS manager exists"), Manager)) return false;
	bool bCompleted = false;
	const FQueryFinishedSignature Finished = FQueryFinishedSignature::CreateLambda(
		[&Data, &OutResult, &bCompleted](TSharedPtr<FEnvQueryResult> Result)
		{
			OutResult = Result;
			bCompleted = true;
			if (Result) EnemySearchTesting::CompletePendingQuery(Data, *Result);
		});
	const int32 RequestId = Request.Execute(Data.RunMode, Finished);
	Data.RequestId = RequestId;
	if (!Test.TestTrue(TEXT("Actual asynchronous EQS request starts"), RequestId != INDEX_NONE)) return false;
	// 실패/조기 종료에도 스택 참조를 잡은 Delegate를 다음 테스트까지 남기지 않는다.
	ON_SCOPE_EXIT { if (!bCompleted) Manager->AbortQuery(RequestId); };
	BeforeCompletion();
	for (int32 Tick = 0; Tick < 256 && !bCompleted; ++Tick) Manager->Tick(1.0f / 60.0f);
	return Test.TestTrue(TEXT("Actual EQS delegate finishes within bounded manager ticks"), bCompleted)
		&& Test.TestTrue(TEXT("Actual EQS result exists"), OutResult.IsValid());
}

FEnemyFlyToLocationTaskInstanceData MakeHeightFlightData(const FEnemyRunEnvQueryTaskInstanceData& Query)
{
	FEnemyFlyToLocationTaskInstanceData Fly;
	Fly.Enemy = Query.Enemy;
	Fly.Destination = Query.Destination;
	Fly.LastDestination = Query.Destination;
	Fly.CachedController = Query.CachedController.Get();
	Fly.CachedCapsule = Query.Enemy->GetCapsuleComponent();
	Fly.CachedRoomSubsystem = Query.CachedRooms;
	Fly.CachedRoom = Query.CapturedRoom;
	Fly.PathRoomTag = Query.CapturedRoomTag;
	Fly.bOwnsMovement = true;
	Fly.bUsePlannedPath = Query.bValidateOrbitPath;
	Fly.bUsePathfinding = !Fly.bUsePlannedPath;
	Fly.PlannedWaypoints = Query.SelectedWaypoints;
	Fly.bConstrainRetreatPath = Query.bValidateRetreatPath;
	Fly.RetreatOrigin = Query.RetreatOrigin;
	Fly.RetreatDirection = Query.RetreatDirection;
	Fly.GoalSearchRadius = 0;
	Fly.MaxSearchMilliseconds = HeightTestMaxMilliseconds;
	Fly.MaxCollisionChecks = 4096;
	Fly.bRotateTowardDestination = false;
	return Fly;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyFlightHeightSettingsValidationTest,
	"Outlier.Enemy.EQS.FlightHeight.SettingsValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyFlightHeightSettingsValidationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FEnemyFlightHeightSettings Defaults;
	TestFalse(TEXT("Height behavior requires opt-in"), Defaults.bEnabled);
	TestTrue(TEXT("Defaults are valid"), Defaults.IsValid());
	TestEqual(TEXT("Low default is floor plus 100cm"), Defaults.GetHeight(EEnemyFlightHeightPreference::Low), 100.0f);
	TestEqual(TEXT("Mid default is floor plus 150cm"), Defaults.GetHeight(EEnemyFlightHeightPreference::Mid), 150.0f);
	TestEqual(TEXT("High default is floor plus 250cm"), Defaults.GetHeight(EEnemyFlightHeightPreference::High), 250.0f);
	const float NaN = std::numeric_limits<float>::quiet_NaN();
	const float Infinity = std::numeric_limits<float>::infinity();
	for (float Invalid : {-1.0f, NaN, Infinity})
	{
		FEnemyFlightHeightSettings Settings = Defaults;
		Settings.LowHeight = Invalid;
		TestFalse(TEXT("Negative or nonfinite low height is invalid"), Settings.IsValid());
	}
	for (float FEnemyFlightHeightSettings::* Field : {&FEnemyFlightHeightSettings::MidHeight,
		&FEnemyFlightHeightSettings::HighHeight, &FEnemyFlightHeightSettings::FloorProbeDistance,
		&FEnemyFlightHeightSettings::CorrectionStep})
	{
		for (float Invalid : {NaN, Infinity})
		{
			FEnemyFlightHeightSettings Settings = Defaults;
			Settings.*Field = Invalid;
			TestFalse(TEXT("Nonfinite height/probe/correction is invalid"), Settings.IsValid());
		}
	}
	for (float FEnemyFlightHeightSettings::* Field : {&FEnemyFlightHeightSettings::FloorProbeDistance,
		&FEnemyFlightHeightSettings::CorrectionStep})
	{
		for (float Invalid : {0.0f, -1.0f})
		{
			FEnemyFlightHeightSettings Settings = Defaults;
			Settings.*Field = Invalid;
			TestFalse(TEXT("Probe and correction distances must be positive"), Settings.IsValid());
		}
	}
	FEnemyFlightHeightSettings Settings = Defaults;
	Settings.MidHeight = Defaults.LowHeight - 1.0f;
	TestFalse(TEXT("Mid cannot be below low"), Settings.IsValid());
	Settings = Defaults;
	Settings.HighHeight = Defaults.MidHeight - 1.0f;
	TestFalse(TEXT("High cannot be below mid"), Settings.IsValid());
	for (int32 Steps : {-1, 9})
	{
		Settings = Defaults;
		Settings.CorrectionSteps = Steps;
		TestFalse(TEXT("Correction count outside 0..8 is invalid"), Settings.IsValid());
	}
	for (int32 Steps : {0, 8})
	{
		Settings = Defaults;
		Settings.CorrectionSteps = Steps;
		TestTrue(TEXT("Correction count endpoints are valid"), Settings.IsValid());
	}
	Settings = Defaults;
	Settings.LowHeight = Settings.MidHeight = Settings.HighHeight = 0.0f;
	TestTrue(TEXT("Equal nonnegative heights are valid"), Settings.IsValid());
	Settings.FixedPreference = static_cast<EEnemyFlightHeightPreference>(255);
	TestFalse(TEXT("Unknown fixed preference is invalid"), Settings.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyFlightHeightCombatLifecycleTest,
	"Outlier.Enemy.EQS.FlightHeight.CombatLifecycleAndStableHint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyFlightHeightCombatLifecycleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemyFlightHeightTestWorld Fixture;
	if (!TestTrue(TEXT("Fixture initializes"), Fixture.Initialize())) return false;
	AEnemyBase& Enemy = *Fixture.Enemy;
	TestFalse(TEXT("Fixture world has not begun play"), Fixture.World->HasBegunPlay());
	TestFalse(TEXT("Native Enemy has not begun play"), Enemy.HasActorBegunPlay());
	TestEqual(TEXT("Native capsule radius remains 34cm"), Enemy.GetCapsuleComponent()->GetScaledCapsuleRadius(), 34.0f);
	TestEqual(TEXT("Native capsule half-height remains 88cm"), Enemy.GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), 88.0f);
	TestEqual(TEXT("Combat entry selects fixed mid"), Enemy.GetCombatHeightPreference(), EEnemyFlightHeightPreference::Mid);
	const bool bLeft = Enemy.PrefersCombatLeft();
	const int32 Lane = Enemy.GetCombatChaseLane();
	Enemy.FlightHeightSettings.FixedPreference = EEnemyFlightHeightPreference::High;
	for (int32 Index = 0; Index < 8; ++Index)
	{
		Enemy.EnterCombatFromRoom(FVector(3000 + Index * 10, 0, 50), false);
		Enemy.EnterAlertFromPerception(FVector(3100, 0, 50));
		TestEqual(TEXT("Repeated combat information preserves height hint"), Enemy.GetCombatHeightPreference(), EEnemyFlightHeightPreference::Mid);
		TestEqual(TEXT("Repeated combat information preserves orbit direction"), Enemy.PrefersCombatLeft(), bLeft);
		TestEqual(TEXT("Repeated combat information preserves chase lane"), Enemy.GetCombatChaseLane(), Lane);
	}
	Enemy.EnterStun();
	Enemy.EnterCombatFromRoom(FVector(3200, 0, 50), false);
	Enemy.RestoreStateAfterStun();
	TestEqual(TEXT("Stun within combat preserves preference"), Enemy.GetCombatHeightPreference(), EEnemyFlightHeightPreference::Mid);
	TestEqual(TEXT("Stun restores combat"), Enemy.GetCombatState(), EEnemyCombatState::Combat);

	// 새 대여의 실제 초기화 경로로 이전 전투 성향을 지운다. 보호 멤버를 우회하지 않는다.
	UEnemyPoolSubsystem* Pool = Fixture.World->GetSubsystem<UEnemyPoolSubsystem>();
	if (!TestNotNull(TEXT("Pool subsystem exists"), Pool)) return false;
	Enemy.CombatHeightPreference = EEnemyFlightHeightPreference::High;
	Enemy.PrepareForPoolSpawn(Pool);
	FEnemyPoolLeaseContext Context;
	Context.RoomTag = Fixture.RoomTag;
	Context.GameplayGeneration = 1;
	if (!TestTrue(TEXT("New lease resets combat runtime"), Enemy.BeginPoolLease(Context, FTransform(FVector(0, 0, 400)), 1))) return false;
	TestEqual(TEXT("Pool lease resets height to mid"), Enemy.GetCombatHeightPreference(), EEnemyFlightHeightPreference::Mid);
	TestEqual(TEXT("Pool lease resets combat state"), Enemy.GetCombatState(), EEnemyCombatState::NonCombat);
	Enemy.EnterCombatFromRoom(FVector(3000, 0, 50), false);
	TestEqual(TEXT("Next combat selects newly configured high"), Enemy.GetCombatHeightPreference(), EEnemyFlightHeightPreference::High);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyFlightHeightPreStunPromotionTest,
	"Outlier.Enemy.EQS.FlightHeight.PreStunPromotion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyFlightHeightPreStunPromotionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemyFlightHeightTestWorld Fixture;
	if (!TestTrue(TEXT("Fixture initializes"), Fixture.Initialize())) return false;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AEnemyBase* Enemy = Fixture.World->SpawnActor<AEnemyBase>(AEnemyBase::StaticClass(), FTransform(FVector(0, 500, 400)), Spawn);
	if (!TestNotNull(TEXT("Noncombat Enemy spawns"), Enemy)) return false;
	Enemy->GetRoomTagComp()->AssignDefaultRoomTag(Fixture.RoomTag);
	Enemy->FlightHeightSettings.bEnabled = true;
	Enemy->FlightHeightSettings.bRandomizePreference = false;
	Enemy->FlightHeightSettings.FixedPreference = EEnemyFlightHeightPreference::Low;
	Enemy->EnterStun();
	Enemy->EnterCombatFromRoom(FVector(3000, 0, 50), false);
	TestEqual(TEXT("Promotion selects fixed low while stunned"), Enemy->GetCombatHeightPreference(), EEnemyFlightHeightPreference::Low);
	TestEqual(TEXT("Promotion does not end stun"), Enemy->GetCombatState(), EEnemyCombatState::Stun);
	const bool bLeft = Enemy->PrefersCombatLeft();
	const int32 Lane = Enemy->GetCombatChaseLane();
	Enemy->FlightHeightSettings.FixedPreference = EEnemyFlightHeightPreference::High;
	Enemy->EnterCombatFromRoom(FVector(3100, 0, 50), false);
	Enemy->RestoreStateAfterStun();
	TestEqual(TEXT("Restoration commits promoted combat"), Enemy->GetCombatState(), EEnemyCombatState::Combat);
	TestEqual(TEXT("Repeated promotion retains low"), Enemy->GetCombatHeightPreference(), EEnemyFlightHeightPreference::Low);
	TestEqual(TEXT("Repeated promotion retains orbit hint"), Enemy->PrefersCombatLeft(), bLeft);
	TestEqual(TEXT("Repeated promotion retains lane hint"), Enemy->GetCombatChaseLane(), Lane);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyFlightHeightFloorPreferenceTest,
	"Outlier.Enemy.EQS.FlightHeight.FloorRelativePreferences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyFlightHeightFloorPreferenceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemyFlightHeightTestWorld Fixture;
	if (!TestTrue(TEXT("Fixture initializes"), Fixture.Initialize())) return false;
	for (EEnemyFlightHeightPreference Preference : {EEnemyFlightHeightPreference::Low,
		EEnemyFlightHeightPreference::Mid, EEnemyFlightHeightPreference::High})
	{
		Fixture.Enemy->CombatHeightPreference = Preference;
		FEnemyFlightHeightSnapshot Snapshot;
		TestEqual(TEXT("Height preparation succeeds"), Fixture.Prepare(Snapshot), EFlightPathStatus::Success);
		CheckCandidates(*this, Snapshot, EFlightPathStatus::Success, Fixture.Enemy->FlightHeightSettings.GetHeight(Preference));
		TestTrue(TEXT("Collision counts remain bounded"), Snapshot.CollisionChecks > 0 && Snapshot.CollisionChecks <= HeightTestMaxChecks);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyFlightHeightFloorStepTest,
	"Outlier.Enemy.EQS.FlightHeight.CandidateLocalFloorStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyFlightHeightFloorStepTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemyFlightHeightTestWorld Fixture;
	if (!TestTrue(TEXT("Fixture initializes"), Fixture.Initialize())) return false;
	if (!TestNotNull(TEXT("Raised platform spawns under candidates"),
		Fixture.SpawnBlockingBox(FVector(1500, 0, 50), FVector(300, 300, 50)))) return false;
	FEnemyFlightHeightSnapshot Snapshot;
	TestEqual(TEXT("Raised floor query succeeds"), Fixture.Prepare(Snapshot), EFlightPathStatus::Success);
	CheckCandidates(*this, Snapshot, EFlightPathStatus::Success, 250.0);
	for (const FEnemyFlightHeightCandidate& Candidate : Snapshot.Candidates)
	{
		TestTrue(TEXT("Platform lies under candidate XY"), FMath::IsNearlyEqual(Candidate.Location.X, 1500.0, 0.01));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyFlightHeightLowCeilingTest,
	"Outlier.Enemy.EQS.FlightHeight.LowCeilingCorrection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyFlightHeightLowCeilingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemyFlightHeightTestWorld Fixture;
	// 시작점이 천장 위에 있으면 천장 윗면이 현재 층 바닥이 되므로 아래에서 검사한다.
	if (!TestTrue(TEXT("Fixture starts below ceiling"), Fixture.Initialize(true, FVector(0, 0, 150)))) return false;
	Fixture.Enemy->CombatHeightPreference = EEnemyFlightHeightPreference::High;
	if (!TestNotNull(TEXT("Ceiling bottom is 300cm"),
		Fixture.SpawnBlockingBox(FVector(1500, 0, 325), FVector(400, 400, 25)))) return false;
	FEnemyFlightHeightSnapshot Snapshot;
	TestEqual(TEXT("Low ceiling query succeeds"), Fixture.Prepare(Snapshot), EFlightPathStatus::Success);
	const double ExpectedZ = 300.0 - Fixture.VerticalExtent() - 1.0;
	CheckCandidates(*this, Snapshot, EFlightPathStatus::Success, ExpectedZ);
	TestTrue(TEXT("Correction lowers high preference"), ExpectedZ < 250.0);
	TestEqual(TEXT("Correction preserves combat hint"), Fixture.Enemy->GetCombatHeightPreference(), EEnemyFlightHeightPreference::High);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyFlightHeightNarrowSpaceTest,
	"Outlier.Enemy.EQS.FlightHeight.NarrowSpaceBetweenCorrectionSamples",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyFlightHeightNarrowSpaceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemyFlightHeightTestWorld Fixture;
	if (!TestTrue(TEXT("Fixture initializes"), Fixture.Initialize(true, FVector(0, 0, 100)))) return false;
	Fixture.Enemy->CombatHeightPreference = EEnemyFlightHeightPreference::High;
	// 기본 캡슐+여유의 유효 원점 범위는 94..97cm다. 250-50*n 표본에는 없다.
	const double CeilingBottom = 2.0 * Fixture.VerticalExtent() + 5.0;
	if (!TestNotNull(TEXT("Narrow ceiling spawns"), Fixture.SpawnBlockingBox(
		FVector(1500, 0, CeilingBottom + 25), FVector(400, 400, 25)))) return false;
	FEnemyFlightHeightSnapshot Snapshot;
	TestEqual(TEXT("Narrow space query succeeds"), Fixture.Prepare(Snapshot), EFlightPathStatus::Success);
	CheckCandidates(*this, Snapshot, EFlightPathStatus::Success, CeilingBottom - Fixture.VerticalExtent() - 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyFlightHeightNoSpaceTest,
	"Outlier.Enemy.EQS.FlightHeight.NoHeightSpaceExcluded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyFlightHeightNoSpaceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemyFlightHeightTestWorld Fixture;
	if (!TestTrue(TEXT("Fixture initializes"), Fixture.Initialize(true, FVector(0, 0, 100)))) return false;
	const double CeilingBottom = 2.0 * Fixture.VerticalExtent() - 1.0;
	if (!TestNotNull(TEXT("Ceiling leaves insufficient capsule space"), Fixture.SpawnBlockingBox(
		FVector(1500, 0, CeilingBottom + 25), FVector(400, 400, 25)))) return false;
	FEnemyFlightHeightSnapshot Snapshot;
	TestEqual(TEXT("Query completes without exhausting budget"), Fixture.Prepare(Snapshot), EFlightPathStatus::Success);
	CheckCandidates(*this, Snapshot, EFlightPathStatus::BlockedEndpoint);
	TestTrue(TEXT("Space rejection remains bounded"), Snapshot.CollisionChecks <= HeightTestMaxChecks);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyFlightHeightMissingFloorTest,
	"Outlier.Enemy.EQS.FlightHeight.MissingStaticFloorUnavailable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyFlightHeightMissingFloorTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemyFlightHeightTestWorld Fixture;
	if (!TestTrue(TEXT("Fixture has no static floor"), Fixture.Initialize(false))) return false;
	if (!TestNotNull(TEXT("Dynamic floor decoy spawns"),
		Fixture.SpawnBlockingBox(FVector(0, 0, -25), FVector(10000, 10000, 25), ECC_WorldDynamic))) return false;
	FEnemyFlightHeightSnapshot Snapshot;
	TestEqual(TEXT("Query completes with unavailable candidates"), Fixture.Prepare(Snapshot), EFlightPathStatus::Success);
	CheckCandidates(*this, Snapshot, EFlightPathStatus::Unavailable);
	TestEqual(TEXT("Only one floor probe per candidate is consumed"), Snapshot.CollisionChecks, Snapshot.Candidates.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyFlightHeightRoomBoundaryTest,
	"Outlier.Enemy.EQS.FlightHeight.RoomBoundaryExcluded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyFlightHeightRoomBoundaryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemyFlightHeightTestWorld Fixture;
	// 후보 중심은 방 안이지만 반경+여유가 경계를 넘는다.
	if (!TestTrue(TEXT("Fixture has capsule-tight room edge"),
		Fixture.Initialize(true, FVector(0, 0, 400), FVector(1520, 1000, 1000)))) return false;
	FEnemyFlightHeightSnapshot Snapshot;
	TestEqual(TEXT("Boundary query completes"), Fixture.Prepare(Snapshot), EFlightPathStatus::Success);
	CheckCandidates(*this, Snapshot, EFlightPathStatus::BlockedEndpoint);
	TestTrue(TEXT("Bounded corrections cannot escape room"), Snapshot.CollisionChecks <= HeightTestMaxChecks);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyFlightHeightInvalidSettingsTest,
	"Outlier.Enemy.EQS.FlightHeight.InvalidSettingsAndOptIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyFlightHeightInvalidSettingsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemyFlightHeightTestWorld Fixture;
	if (!TestTrue(TEXT("Fixture initializes"), Fixture.Initialize())) return false;
	Fixture.Enemy->FlightHeightSettings.CorrectionStep = 0.0f;
	FEnemyFlightHeightSnapshot Snapshot;
	TestEqual(TEXT("Invalid enabled settings fail before tracing"), Fixture.Prepare(Snapshot), EFlightPathStatus::InvalidRequest);
	TestEqual(TEXT("Invalid settings perform no checks"), Snapshot.CollisionChecks, 0);
	TestTrue(TEXT("Invalid settings produce no candidates"), Snapshot.Candidates.IsEmpty());
	Fixture.Enemy->FlightHeightSettings.bEnabled = false;
	TestEqual(TEXT("Disabled behavior bypasses height resolution"), Fixture.Prepare(Snapshot), EFlightPathStatus::Success);
	TestFalse(TEXT("Disabled snapshot records opt-out"), Snapshot.bEnabled);
	TestTrue(TEXT("Disabled query leaves legacy candidates to generator"), Snapshot.Candidates.IsEmpty());
	TestEqual(TEXT("Disabled settings perform no checks"), Snapshot.CollisionChecks, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyFlightHeightCollisionBudgetTest,
	"Outlier.Enemy.EQS.FlightHeight.CollisionBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyFlightHeightCollisionBudgetTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemyFlightHeightTestWorld Fixture;
	if (!TestTrue(TEXT("Fixture initializes"), Fixture.Initialize())) return false;
	for (int32 MaxChecks : {1, 2})
	{
		FEnemyFlightHeightSnapshot Snapshot;
		TestEqual(TEXT("Probe budget exhaustion stays distinct from blockage"), Fixture.Prepare(Snapshot, MaxChecks), EFlightPathStatus::BudgetExceeded);
		TestEqual(TEXT("Resolver stops at collision budget"), Snapshot.CollisionChecks, MaxChecks);
		TestEqual(TEXT("No later candidates are resolved after exhaustion"), Snapshot.Candidates.Num(), 1);
		CheckCandidates(*this, Snapshot, EFlightPathStatus::BudgetExceeded);
	}
	FEnemyFlightHeightSnapshot Snapshot;
	TestEqual(TEXT("Sufficient budget permits clear geometry"), Fixture.Prepare(Snapshot), EFlightPathStatus::Success);
	CheckCandidates(*this, Snapshot, EFlightPathStatus::Success, 150.0);
	TestTrue(TEXT("Floor, ceiling and destination checks stay within bounded probe count"),
		Snapshot.CollisionChecks >= 3 * Snapshot.Candidates.Num()
		&& Snapshot.CollisionChecks <= (2 + 1 + 2 * Fixture.Enemy->FlightHeightSettings.CorrectionSteps) * Snapshot.Candidates.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyFlightHeightSnapshotCurrentTest,
	"Outlier.Enemy.EQS.FlightHeight.SnapshotSettingsChange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyFlightHeightSnapshotCurrentTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemyFlightHeightTestWorld Fixture;
	if (!TestTrue(TEXT("Fixture initializes"), Fixture.Initialize())) return false;
	FEnemyFlightHeightSnapshot Snapshot;
	TestEqual(TEXT("Snapshot prepares"), Fixture.Prepare(Snapshot), EFlightPathStatus::Success);
	TestTrue(TEXT("Unchanged snapshot is current"), EnemyFlightHeight::IsSnapshotCurrent(*Fixture.Enemy, Snapshot));
	const FEnemyFlightHeightSettings Original = Fixture.Enemy->FlightHeightSettings;
	for (float FEnemyFlightHeightSettings::* Field : {&FEnemyFlightHeightSettings::LowHeight,
		&FEnemyFlightHeightSettings::MidHeight, &FEnemyFlightHeightSettings::HighHeight,
		&FEnemyFlightHeightSettings::FloorProbeDistance, &FEnemyFlightHeightSettings::CorrectionStep})
	{
		Fixture.Enemy->FlightHeightSettings = Original;
		Fixture.Enemy->FlightHeightSettings.*Field += 1.0f;
		TestFalse(TEXT("Changed height/probe/correction invalidates snapshot"), EnemyFlightHeight::IsSnapshotCurrent(*Fixture.Enemy, Snapshot));
	}
	Fixture.Enemy->FlightHeightSettings = Original;
	++Fixture.Enemy->FlightHeightSettings.CorrectionSteps;
	TestFalse(TEXT("Changed correction count invalidates snapshot"), EnemyFlightHeight::IsSnapshotCurrent(*Fixture.Enemy, Snapshot));
	Fixture.Enemy->FlightHeightSettings = Original;
	Fixture.Enemy->FlightHeightSettings.bRandomizePreference = !Original.bRandomizePreference;
	TestFalse(TEXT("Changed randomization invalidates snapshot"), EnemyFlightHeight::IsSnapshotCurrent(*Fixture.Enemy, Snapshot));
	Fixture.Enemy->FlightHeightSettings = Original;
	Fixture.Enemy->FlightHeightSettings.FixedPreference = EEnemyFlightHeightPreference::High;
	TestFalse(TEXT("Changed configured preference invalidates snapshot"), EnemyFlightHeight::IsSnapshotCurrent(*Fixture.Enemy, Snapshot));
	Fixture.Enemy->FlightHeightSettings = Original;
	Fixture.Enemy->CombatHeightPreference = EEnemyFlightHeightPreference::High;
	TestFalse(TEXT("Changed combat hint invalidates snapshot"), EnemyFlightHeight::IsSnapshotCurrent(*Fixture.Enemy, Snapshot));
	Fixture.Enemy->CombatHeightPreference = Snapshot.Preference;
	Fixture.Enemy->FlightHeightSettings.bEnabled = false;
	TestFalse(TEXT("Disabling height invalidates enabled snapshot"), EnemyFlightHeight::IsSnapshotCurrent(*Fixture.Enemy, Snapshot));
	Snapshot.bEnabled = false;
	TestTrue(TEXT("Disabled snapshot stays current while disabled"), EnemyFlightHeight::IsSnapshotCurrent(*Fixture.Enemy, Snapshot));
	Fixture.Enemy->FlightHeightSettings = Original;
	TestFalse(TEXT("Enabling height invalidates disabled snapshot"), EnemyFlightHeight::IsSnapshotCurrent(*Fixture.Enemy, Snapshot));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyFlightHeightNamedParamsTest,
	"Outlier.Enemy.EQS.FlightHeight.NamedParamSerializedCoordinates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyFlightHeightNamedParamsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemyFlightHeightTestWorld Fixture;
	if (!TestTrue(TEXT("Fixture initializes"), Fixture.Initialize())) return false;
	Fixture.Approach->AngularSamples = 3;
	FEnemyFlightHeightSnapshot Snapshot;
	TestEqual(TEXT("Non-axis candidates prepare"), Fixture.Prepare(Snapshot), EFlightPathStatus::Success);
	if (!TestTrue(TEXT("Serialization has multiple candidates"), Snapshot.Candidates.Num() > 2)) return false;
	Snapshot.Candidates[1].Status = EFlightPathStatus::BlockedEndpoint;
	UEnvQuery* Query = NewObject<UEnvQuery>(Fixture.World);
	UEnvQueryOption* Option = NewObject<UEnvQueryOption>(Query);
	Option->Generator = NewObject<UEnemyEnvQueryGenerator_Approach>(Option);
	Query->GetOptionsMutable().Add(Option);
	FEnvQueryRequest Request(Query, Fixture.Enemy);
	EnemyFlightHeight::SetQuerySnapshot(Request, Snapshot);
	UEnvQueryManager* Manager = UEnvQueryManager::GetCurrent(Fixture.World);
	if (!TestNotNull(TEXT("EQS manager exists"), Manager)) return false;
	// 실행 없이 실제 Request -> Instance 직렬화 경로의 NamedParams를 읽는다.
	const TSharedPtr<FEnvQueryInstance> Instance = Manager->PrepareQueryInstance(Request, EEnvQueryRunMode::AllMatching);
	if (!TestTrue(TEXT("EQS request prepares an instance"), Instance.IsValid())) return false;
	int32 SerializedCount = 0;
	for (const FEnemyFlightHeightCandidate& Candidate : Snapshot.Candidates)
	{
		if (Candidate.Status != EFlightPathStatus::Success) continue;
		const double Coordinates[] = {Candidate.Location.X, Candidate.Location.Y, Candidate.Location.Z};
		const TCHAR* Fields[] = {TEXT("X"), TEXT("Y"), TEXT("Z")};
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const FName Key(*FString::Printf(TEXT("FlightHeight%d%s"), SerializedCount, Fields[Axis]));
			const float* Value = Instance->NamedParams.Find(Key);
			if (TestNotNull(TEXT("Serialized coordinate exists"), Value))
			{
				TestEqual(TEXT("Snapshot already matches float serialized coordinate"), double(*Value), Coordinates[Axis]);
			}
		}
		++SerializedCount;
	}
	const float* Count = Instance->NamedParams.Find(FName(TEXT("FlightHeightCandidateCount")));
	if (TestNotNull(TEXT("Serialized candidate count exists"), Count))
	{
		TestEqual(TEXT("Only successful candidates serialize"), *Count, float(SerializedCount));
	}
	const FName ExcludedIndex(*FString::Printf(TEXT("FlightHeight%dX"), SerializedCount));
	TestFalse(TEXT("Excluded candidate leaves no extra serialized item"), Instance->NamedParams.Contains(ExcludedIndex));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyFlightHeightChaseBiasTest,
	"Outlier.Enemy.EQS.FlightHeight.ChaseNearBiasAndFloorClamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyFlightHeightChaseBiasTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemyFlightHeightTestWorld Fixture;
	if (!TestTrue(TEXT("Fixture initializes"), Fixture.Initialize())) return false;
	Fixture.Enemy->CombatHeightPreference = EEnemyFlightHeightPreference::High;
	UEnemyEnvQueryGenerator_Chase* Chase = NewObject<UEnemyEnvQueryGenerator_Chase>(Fixture.World);
	const FVector Start = Fixture.Enemy->GetActorLocation();
	// 캡슐 바닥 여유보다 낮은 타깃도 관통하지 않아야 한다.
	const double MinimumZ = Fixture.VerticalExtent()
		- (Fixture.Enemy->GetCapsuleComponent()->GetComponentLocation() - Start).Z + 1.0;
	for (double Distance : {300.0, 650.0, 1000.0})
	{
		FEnemyFlightHeightSnapshot Snapshot;
		TestEqual(TEXT("Chase query completes"), EnemyFlightHeight::PrepareQuery(*Fixture.Enemy, *Chase,
			Start, FVector(Distance, 0, 120), 400.0f, true, 1, HeightTestClearance,
			HeightTestMaxChecks, HeightTestMaxMilliseconds, Snapshot), EFlightPathStatus::Success);
		const double ExpectedZ = Distance == 300.0 ? 120.0 : (Distance == 650.0 ? 185.0 : 250.0);
		CheckCandidates(*this, Snapshot, EFlightPathStatus::Success, ExpectedZ);
		for (const FEnemyFlightHeightCandidate& Candidate : Snapshot.Candidates)
		{
			TestTrue(TEXT("Chase respects floor capsule clearance"), Candidate.Location.Z >= MinimumZ - 0.01);
			if (Distance == 300.0) TestTrue(TEXT("Near chase removes lane bias"), FMath::IsNearlyZero(Candidate.Location.Y, 0.01));
		}
	}
	FEnemyFlightHeightSnapshot Clamped;
	TestEqual(TEXT("Low target chase completes"), EnemyFlightHeight::PrepareQuery(*Fixture.Enemy, *Chase,
		Start, FVector(300, 0, 0), 400.0f, true, 1, HeightTestClearance,
		HeightTestMaxChecks, HeightTestMaxMilliseconds, Clamped), EFlightPathStatus::Success);
	CheckCandidates(*this, Clamped, EFlightPathStatus::Success, MinimumZ);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyFlightHeightOrbit3DTest,
	"Outlier.Enemy.EQS.FlightHeight.Orbit3DEndpointAndIntermediateZ",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyFlightHeightOrbit3DTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FVector Start(1500, 0, 400);
	const FVector Center(0, 0, 50);
	for (bool bLeft : {false, true})
	{
		TArray<FEnemyOrbitCandidate> Candidates;
		EnemyOrbit::BuildCandidates(Start, Center, 425.0f, bLeft, Candidates);
		if (!TestTrue(TEXT("Orbit candidates exist"), !Candidates.IsEmpty())) return false;
		for (double DestinationZ : {150.0, 650.0})
		{
			FEnemyOrbitCandidate Candidate = Candidates[0];
			Candidate.Destination.Z = DestinationZ;
			TArray<FVector> Waypoints;
			EnemyOrbit::BuildWaypoints(Start, Center, Candidate, Waypoints);
			const double CurveLength = FMath::Sqrt(FMath::Square(double(Candidate.ArcLength))
				+ FMath::Square(DestinationZ - Start.Z));
			const int32 Segments = FMath::CeilToInt(CurveLength / 50.0);
			if (!TestEqual(TEXT("Arc has bounded subdivision"), Waypoints.Num(), Segments + 1)) return false;
			TestTrue(TEXT("Orbit starts at captured start"), Waypoints[0].Equals(Start, 0.001));
			TestTrue(TEXT("Orbit ends at corrected 3D destination"), Waypoints.Last().Equals(Candidate.Destination, 0.001));
			double SpatialLength = 0.0;
			for (int32 Index = 1; Index <= Segments; ++Index)
			{
				TestTrue(TEXT("Height-adjusted segment stays within Fly geometry limit"),
					FVector::Dist(Waypoints[Index - 1], Waypoints[Index]) <= 50.01);
				SpatialLength += FVector::Dist(Waypoints[Index - 1], Waypoints[Index]);
				const double Alpha = double(Index) / Segments;
				const double ExpectedZ = Start.Z + (DestinationZ - Start.Z) * Alpha;
				TestTrue(TEXT("Intermediate height interpolates to corrected endpoint"),
					FMath::IsNearlyEqual(Waypoints[Index].Z, ExpectedZ, 0.001));
				TestTrue(TEXT("Intermediate XY retains orbit radius"),
					FMath::IsNearlyEqual(FVector::Dist2D(Waypoints[Index], Center), 1500.0, 0.001));
				const double Angle = (bLeft ? 1.0 : -1.0) * Candidate.ArcLength * Alpha / 1500.0;
				TestTrue(TEXT("Intermediate XY follows the selected arc"),
					FMath::IsNearlyEqual(Waypoints[Index].X, 1500.0 * FMath::Cos(Angle), 0.01)
					&& FMath::IsNearlyEqual(Waypoints[Index].Y, 1500.0 * FMath::Sin(Angle), 0.01));
			}
			TestTrue(TEXT("Typical 425cm arc with 250cm height change stays within 500cm spatial travel"),
				SpatialLength > 0.0 && SpatialLength <= 500.0);
		}
		FEnemyOrbitCandidate TooHigh = Candidates[0];
		TooHigh.Destination.Z = Start.Z + 600.0;
		TArray<FVector> Rejected;
		EnemyOrbit::BuildWaypoints(Start, Center, TooHigh, Rejected);
		TestTrue(TEXT("More than five metres of spatial travel produces no Fly route"), Rejected.IsEmpty());
	}
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyFlightHeightQueryIntegrationTest,
	"Outlier.Enemy.EQS.FlightHeight.Integration.QueryToFly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyFlightHeightQueryIntegrationTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Mode : {TEXT("Approach"), TEXT("Orbit"), TEXT("Retreat"), TEXT("Chase")})
	{
		for (const TCHAR* Height : {TEXT("Low"), TEXT("Mid"), TEXT("High"), TEXT("Disabled")})
		{
			const FString Scenario = FString::Printf(TEXT("%s.%s"), Mode, Height);
			Names.Add(Scenario);
			Commands.Add(Scenario);
		}
	}
}

bool FEnemyFlightHeightQueryIntegrationTest::RunTest(const FString& Parameters)
{
	FString Mode, Height;
	if (!Parameters.Split(TEXT("."), &Mode, &Height)) return false;
	FEnemyFlightHeightTestWorld Fixture;
	if (!TestTrue(TEXT("Fixture initializes"), Fixture.Initialize())) return false;
	Fixture.Enemy->FlightHeightSettings.bEnabled = Height != TEXT("Disabled");
	Fixture.Enemy->CombatHeightPreference = Height == TEXT("Low") ? EEnemyFlightHeightPreference::Low
		: Height == TEXT("High") ? EEnemyFlightHeightPreference::High : EEnemyFlightHeightPreference::Mid;
	FEnemyRunEnvQueryTaskInstanceData Data;
	if (!TestTrue(TEXT("Combat query inputs initialize"), MakeHeightCombatQuery(Fixture, Mode, Data))) return false;
	FEnvQueryRequest Request(Data.QueryTemplate, Fixture.Enemy);
	if (!TestTrue(TEXT("Actual Task request preparation succeeds"), EnemySearchTesting::PrepareRequest(Data, Request))) return false;
	TSharedPtr<FEnvQueryResult> Result;
	if (!FinishHeightQuery(*this, Fixture, Data, Request, Result, [] {})) return false;
	if (!TestEqual(TEXT("Live EQS result passes actual Task selection"), Data.QueryResult, EEnemyEnvQueryResult::Success)
		|| !TestTrue(TEXT("Selection exposes a destination"), Data.bHasDestination)) return false;
	TestTrue(TEXT("EQS actually generated candidates"), Result->IsSuccessful() && !Result->Items.IsEmpty());
	TestEqual(TEXT("Request handle clears after completion"), Data.RequestId, INDEX_NONE);

	if (Data.HeightSnapshot.bEnabled)
	{
		bool bMatchesCorrectedCandidate = false;
		for (const FEnemyFlightHeightCandidate& Candidate : Data.HeightSnapshot.Candidates)
		{
			bMatchesCorrectedCandidate |= Candidate.Status == EFlightPathStatus::Success
				&& Candidate.Location.Equals(Data.Destination, 1.0);
		}
		TestTrue(TEXT("Selected endpoint retains the prepared height correction"), bMatchesCorrectedCandidate);
	}
	else
	{
		TestTrue(TEXT("Opt-out does not prepare height candidates"), Data.HeightSnapshot.Candidates.IsEmpty());
		if (Data.bValidateChasePath)
		{
			TArray<FVector> Candidates;
			EnemyChase::BuildCandidates(Data.CapturedChaseStart, Data.CapturedOrbitCenter, Data.CapturedChaseLane, Candidates);
			TestTrue(TEXT("Opt-out retains a legacy chase candidate"), Candidates.ContainsByPredicate(
				[&Data](const FVector& Candidate) { return Candidate.Equals(Data.Destination, 1.0); }));
		}
		else
		{
			const double ExpectedZ = Mode == TEXT("Approach") ? 150.0 : Fixture.Enemy->GetActorLocation().Z;
			TestTrue(TEXT("Opt-out retains legacy generator altitude"), FMath::IsNearlyEqual(Data.Destination.Z, ExpectedZ, 1.0));
		}
	}
	if (Data.bValidateRetreatPath)
	{
		const double Distance = FVector::Dist2D(Data.Destination, Data.RetreatOrigin);
		TestTrue(TEXT("Corrected retreat still ends beyond 10m and within 15m"), Distance > 1000 && Distance <= 1500);
		TestTrue(TEXT("Retreat stays on the captured away side"),
			FVector::DotProduct(Data.Destination - Data.RetreatOrigin, Data.RetreatDirection) > 0);
	}
	if (Data.bValidateOrbitPath)
	{
		if (!TestTrue(TEXT("Selected orbit contains a curve"), Data.SelectedWaypoints.Num() >= 2)) return false;
		double Length = 0;
		for (int32 Index = 1; Index < Data.SelectedWaypoints.Num(); ++Index)
		{
			Length += FVector::Dist(Data.SelectedWaypoints[Index - 1], Data.SelectedWaypoints[Index]);
			TestTrue(TEXT("Corrected curve retains horizontal combat radius"), FMath::IsNearlyEqual(
				FVector::Dist2D(Data.SelectedWaypoints[Index], Data.CapturedOrbitCenter), 1500.0, 1.0));
		}
		TestTrue(TEXT("Selected three-dimensional curve stays within five metres"), Length <= 500.01);
	}

	// BP의 출력 바인딩과 같은 값을 전달한다. 위치를 경유점에 옮기는 것은 경로 소비 검증용이며 물리 이동 QA는 별도다.
	FEnemyFlyToLocationTaskInstanceData Fly = MakeHeightFlightData(Data);
	ON_SCOPE_EXIT { EnemyFlightTesting::Stop(Fly); };
	if (!TestEqual(TEXT("Real Fly path accepts the selected endpoint/curve"),
		EnemyFlightTesting::StartPath(Fly), EStateTreeRunStatus::Running)) return false;
	if (!TestTrue(TEXT("Fly has a non-empty validated route"), Fly.PathWaypoints.Num() >= 2)) return false;
	const FVector CapsuleOffset = Fly.CachedCapsule->GetComponentLocation() - Fixture.Enemy->GetActorLocation();
	TestTrue(TEXT("Fly retains corrected destination in capsule coordinates"),
		Fly.PathWaypoints.Last().Equals(Data.Destination + CapsuleOffset, 1.0));
	const TArray<FVector> Route = Fly.PathWaypoints;
	EStateTreeRunStatus Status = EStateTreeRunStatus::Running;
	for (int32 Index = 1; Index < Route.Num() && Status == EStateTreeRunStatus::Running; ++Index)
	{
		Fixture.Enemy->SetActorLocation(Route[Index] - CapsuleOffset);
		Status = EnemyFlightTesting::TickPath(Fly, 1.0f / 60.0f);
	}
	TestEqual(TEXT("Real Fly consumes every waypoint and completes"), Status, EStateTreeRunStatus::Succeeded);
	TestEqual(TEXT("Fly records arrival"), Fly.MoveResult, EEnemyFlightMoveResult::Arrived);
	TestTrue(TEXT("Arrival releases the consumed path"), Fly.PathWaypoints.IsEmpty() && !Fly.bOwnsMovement);
	AddInfo(FString::Printf(TEXT("[FlightHeightScenario] %s Result=%s Checks=%d Destination=%s"),
		*Parameters, *UEnum::GetValueAsString(Data.QueryResult), Data.HeightSnapshot.CollisionChecks,
		*Data.Destination.ToCompactString()));
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyFlightHeightCompletionIntegrationTest,
	"Outlier.Enemy.EQS.FlightHeight.Integration.PendingResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyFlightHeightCompletionIntegrationTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("SettingsChanged"), TEXT("Disabled"), TEXT("Enabled"),
		TEXT("PreferenceChanged"), TEXT("SightLost"), TEXT("TargetChanged"), TEXT("TargetMoved"),
		TEXT("Possessed"), TEXT("Death"), TEXT("PoolReuse"), TEXT("Inactive"), TEXT("RequestReplaced")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyFlightHeightCompletionIntegrationTest::RunTest(const FString& Parameters)
{
	FEnemyFlightHeightTestWorld Fixture;
	if (!TestTrue(TEXT("Fixture initializes"), Fixture.Initialize())) return false;
	if (Parameters == TEXT("Enabled")) Fixture.Enemy->FlightHeightSettings.bEnabled = false;
	FEnemyRunEnvQueryTaskInstanceData Data;
	if (!TestTrue(TEXT("Combat query inputs initialize"), MakeHeightCombatQuery(Fixture, TEXT("Approach"), Data))) return false;
	Data.TargetChangeDistance = 200;
	FEnvQueryRequest Request(Data.QueryTemplate, Fixture.Enemy);
	if (!TestTrue(TEXT("Request prepares before interruption"), EnemySearchTesting::PrepareRequest(Data, Request))) return false;
	TSharedPtr<FEnvQueryResult> Result;
	bool bPoolReused = false;
	const auto Interrupt = [&]
	{
		if (Parameters == TEXT("SettingsChanged")) Fixture.Enemy->FlightHeightSettings.MidHeight += 50;
		else if (Parameters == TEXT("Disabled")) Fixture.Enemy->FlightHeightSettings.bEnabled = false;
		else if (Parameters == TEXT("Enabled")) Fixture.Enemy->FlightHeightSettings.bEnabled = true;
		else if (Parameters == TEXT("PreferenceChanged")) Fixture.Enemy->CombatHeightPreference = EEnemyFlightHeightPreference::High;
		else if (Parameters == TEXT("SightLost")) Fixture.Enemy->SetPlayerCurrentlyVisible(false);
		else if (Parameters == TEXT("TargetChanged")) Data.CachedController->SetCachedVisibleTargetForTesting(nullptr);
		else if (Parameters == TEXT("TargetMoved")) Data.CapturedTarget->AddActorWorldOffset(FVector(250, 0, 0));
		else if (Parameters == TEXT("Possessed")) Fixture.Enemy->SetEnemyPossessed(true);
		else if (Parameters == TEXT("Death")) Fixture.Enemy->GetAbilitySystemComponent()->AddLooseGameplayTag(
			FGameplayTag::RequestGameplayTag(TEXT("State.Dead")));
		else if (Parameters == TEXT("PoolReuse"))
		{
			Fixture.Enemy->PrepareForPoolSpawn(Fixture.World->GetSubsystem<UEnemyPoolSubsystem>());
			FEnemyPoolLeaseContext Lease;
			Lease.RoomTag = Fixture.RoomTag;
			Lease.GameplayGeneration = Data.CapturedGeneration + 1;
			bPoolReused = Fixture.Enemy->BeginPoolLease(Lease,
				FTransform(Fixture.Enemy->GetActorLocation()), Data.CapturedLease + 1);
		}
		else if (Parameters == TEXT("Inactive")) Data.bActive = false;
		else if (Parameters == TEXT("RequestReplaced")) ++Data.RequestId;
	};
	if (!FinishHeightQuery(*this, Fixture, Data, Request, Result, Interrupt)) return false;
	if (Parameters == TEXT("PoolReuse")) TestTrue(TEXT("Actual pool lease reset occurred before completion"), bPoolReused);
	TestFalse(TEXT("Interrupted live query never exposes a destination"), Data.bHasDestination);
	TestTrue(TEXT("Interrupted live query never leaves orbit waypoints"), Data.SelectedWaypoints.IsEmpty());
	const bool bIgnoredRequest = Parameters == TEXT("Inactive") || Parameters == TEXT("RequestReplaced");
	TestEqual(TEXT("Inactive/replaced requests are ignored; changed snapshots are rejected"),
		Data.QueryResult, bIgnoredRequest ? EEnemyEnvQueryResult::Running : EEnemyEnvQueryResult::Stale);
	AddInfo(FString::Printf(TEXT("[FlightHeightScenario] Pending=%s Result=%s"),
		*Parameters, *UEnum::GetValueAsString(Data.QueryResult)));
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyFlightHeightEnvironmentIntegrationTest,
	"Outlier.Enemy.EQS.FlightHeight.Integration.Environment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyFlightHeightEnvironmentIntegrationTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Mode : {TEXT("Approach"), TEXT("Orbit"), TEXT("Retreat"), TEXT("Chase")})
	{
		for (const TCHAR* Environment : {TEXT("LowCeiling"), TEXT("FloorStep"), TEXT("Obstacle"),
			TEXT("RoomBoundary"), TEXT("MissingFloor")})
		{
			const FString Scenario = FString::Printf(TEXT("%s.%s"), Mode, Environment);
			Names.Add(Scenario);
			Commands.Add(Scenario);
		}
	}
}

bool FEnemyFlightHeightEnvironmentIntegrationTest::RunTest(const FString& Parameters)
{
	FString Mode, Environment;
	if (!Parameters.Split(TEXT("."), &Mode, &Environment)) return false;
	const bool bMissingFloor = Environment == TEXT("MissingFloor");
	FEnemyFlightHeightTestWorld Fixture;
	if (!TestTrue(TEXT("Fixture initializes"), Fixture.Initialize(!bMissingFloor))) return false;
	if (Environment == TEXT("LowCeiling"))
	{
		Fixture.Enemy->FlightHeightSettings.HighHeight = 800;
		Fixture.Enemy->CombatHeightPreference = EEnemyFlightHeightPreference::High;
		if (!TestNotNull(TEXT("Low ceiling spawns"), Fixture.SpawnBlockingBox(FVector(0, 0, 675), FVector(5000, 5000, 25)))) return false;
	}
	else if (Environment == TEXT("FloorStep"))
	{
		if (!TestNotNull(TEXT("Raised floor spawns"), Fixture.SpawnBlockingBox(FVector(0, 0, 25), FVector(5000, 5000, 50)))) return false;
	}
	else if (Environment == TEXT("RoomBoundary"))
	{
		CastChecked<UBoxComponent>(Fixture.Room->GetRootComponent())->SetBoxExtent(FVector(2000, 2500, 2000));
	}
	FEnemyRunEnvQueryTaskInstanceData Data;
	// 단차로 바닥이 75cm 올라가면 타깃도 같은 만큼 올린다. 타깃이 바닥 아래면 선회/후퇴의 시야 검사가 정상적으로 실패한다.
	const double TargetHeight = Environment == TEXT("FloorStep") ? 125.0 : 50.0;
	if (!TestTrue(TEXT("Combat query inputs initialize"), MakeHeightCombatQuery(Fixture, Mode, Data, TargetHeight))) return false;
	if (Environment == TEXT("FloorStep"))
		TestTrue(TEXT("Raised-floor target remains above the floor"), Data.CapturedOrbitCenter.Z > 75.0);
	FEnvQueryRequest Request(Data.QueryTemplate, Fixture.Enemy);
	if (!TestTrue(TEXT("Environmental query prepares"), EnemySearchTesting::PrepareRequest(Data, Request))) return false;
	if (Environment == TEXT("Obstacle"))
	{
		const FEnemyFlightHeightCandidate* Candidate = Data.HeightSnapshot.Candidates.FindByPredicate(
			[](const FEnemyFlightHeightCandidate& Item) { return Item.Status == EFlightPathStatus::Success; });
		if (!TestNotNull(TEXT("Original corrected candidate exists"), Candidate)) return false;
		if (!TestNotNull(TEXT("Candidate obstacle spawns"), Fixture.SpawnBlockingBox(Candidate->Location, FVector(80, 80, 80)))) return false;
		// 후보가 있던 곳에 장애물을 놓은 뒤 실제 요청 준비부터 다시 진행한다.
		Request = FEnvQueryRequest(Data.QueryTemplate, Fixture.Enemy);
		if (!TestTrue(TEXT("Obstacle-aware query prepares"), EnemySearchTesting::PrepareRequest(Data, Request))) return false;
	}
	TSharedPtr<FEnvQueryResult> Result;
	if (!FinishHeightQuery(*this, Fixture, Data, Request, Result, [] {})) return false;
	AddInfo(FString::Printf(TEXT("[FlightHeightScenario] Environment=%s Result=%s Path=%s Checks=%d Items=%d TargetZ=%.1f"),
		*Parameters, *UEnum::GetValueAsString(Data.QueryResult), *UEnum::GetValueAsString(Data.SelectionPathStatus),
		Data.HeightSnapshot.CollisionChecks, Result->Items.Num(), Data.CapturedOrbitCenter.Z));
	if (bMissingFloor)
	{
		TestTrue(TEXT("Missing floor produces no accepted EQS items"), Result->Items.IsEmpty());
		TestFalse(TEXT("Missing floor never exposes a destination"), Data.bHasDestination);
		TestFalse(TEXT("Unknown floor is not proof that both orbit sides are blocked"), Data.bNoOrbitDestination);
		TestFalse(TEXT("Unknown floor is not proof that all retreat routes are blocked"), Data.bNoRetreatDestination);
		TestTrue(TEXT("Missing floor is distinct from exhausted path budget"), Data.QueryResult != EEnemyEnvQueryResult::PathBudgetExceeded);
		return true;
	}
	if (!TestEqual(TEXT("Environmental query selects a safe candidate"), Data.QueryResult, EEnemyEnvQueryResult::Success)) return false;
	if (Environment == TEXT("LowCeiling"))
		TestTrue(TEXT("Selected capsule fits below the ceiling"), Data.Destination.Z + Fixture.VerticalExtent() <= 650.01);
	if (Environment == TEXT("FloorStep"))
		TestTrue(TEXT("Selected height uses the raised floor"), Data.Destination.Z >= 75.0 + Fixture.VerticalExtent() - 0.01);
	FEnemyFlyToLocationTaskInstanceData Fly = MakeHeightFlightData(Data);
	ON_SCOPE_EXIT { EnemyFlightTesting::Stop(Fly); };
	TestEqual(TEXT("Actual Fly validates corrected environmental route"), EnemyFlightTesting::StartPath(Fly), EStateTreeRunStatus::Running);
	TestFalse(TEXT("Safe environmental route is not waiting for a retry"), Fly.bWaitingForPath);
	TestTrue(TEXT("Safe environmental route contains waypoints"), Fly.PathWaypoints.Num() >= 2);
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyFlightHeightFailureIntegrationTest,
	"Outlier.Enemy.EQS.FlightHeight.Integration.FailureClassification",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyFlightHeightFailureIntegrationTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Mode : {TEXT("Approach"), TEXT("Orbit"), TEXT("Retreat"), TEXT("Chase")})
	{
		for (const TCHAR* Failure : {TEXT("InvalidSettings"), TEXT("PreparationBudget"), TEXT("SelectionBudget")})
		{
			const FString Scenario = FString::Printf(TEXT("%s.%s"), Mode, Failure);
			Names.Add(Scenario);
			Commands.Add(Scenario);
		}
	}
}

bool FEnemyFlightHeightFailureIntegrationTest::RunTest(const FString& Parameters)
{
	FString Mode, Failure;
	if (!Parameters.Split(TEXT("."), &Mode, &Failure)) return false;
	FEnemyFlightHeightTestWorld Fixture;
	if (!TestTrue(TEXT("Fixture initializes"), Fixture.Initialize())) return false;
	if (Failure == TEXT("InvalidSettings")) Fixture.Enemy->FlightHeightSettings.MidHeight = -1;
	FEnemyRunEnvQueryTaskInstanceData Data;
	if (!TestTrue(TEXT("Combat query inputs initialize"), MakeHeightCombatQuery(Fixture, Mode, Data))) return false;
	if (Failure == TEXT("PreparationBudget")) Data.MaxCollisionChecks = 1;
	FEnvQueryRequest Request(Data.QueryTemplate, Fixture.Enemy);
	if (Failure == TEXT("InvalidSettings"))
	{
		TestFalse(TEXT("Invalid height settings reject request before EQS execution"), EnemySearchTesting::PrepareRequest(Data, Request));
		TestEqual(TEXT("No asynchronous request was submitted"), Data.RequestId, INDEX_NONE);
	}
	else if (Failure == TEXT("PreparationBudget"))
	{
		TestFalse(TEXT("Height preparation respects its bounded collision budget"), EnemySearchTesting::PrepareRequest(Data, Request));
		TestEqual(TEXT("Preparation budget has an explicit retryable classification"), Data.QueryResult, EEnemyEnvQueryResult::PathBudgetExceeded);
		TestTrue(TEXT("Preparation never exceeds the configured check count"), Data.HeightSnapshot.CollisionChecks <= Data.MaxCollisionChecks);
	}
	else
	{
		if (!TestTrue(TEXT("Height preparation succeeds before selection budget is exhausted"), EnemySearchTesting::PrepareRequest(Data, Request))) return false;
		TSharedPtr<FEnvQueryResult> Result;
		// 보정에 사용한 검사만 남겨 선택 단계의 공유 예산 소진을 재현한다. 높이 보정 자체는 다시 실행하지 않는다.
		const auto ExhaustSelectionBudget = [&] { Data.MaxCollisionChecks = Data.HeightSnapshot.CollisionChecks; };
		if (!FinishHeightQuery(*this, Fixture, Data, Request, Result, ExhaustSelectionBudget)) return false;
		TestEqual(TEXT("Selection also respects consumed height budget"), Data.QueryResult, EEnemyEnvQueryResult::PathBudgetExceeded);
	}
	TestFalse(TEXT("Failed query never exposes a destination"), Data.bHasDestination);
	TestFalse(TEXT("Configuration/budget failure cannot force stationary orbit fire"), Data.bNoOrbitDestination);
	TestFalse(TEXT("Configuration/budget failure cannot force stationary retreat fire"), Data.bNoRetreatDestination);
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyFlightHeightSearchRegressionTest,
	"Outlier.Enemy.EQS.FlightHeight.Integration.SearchUnchanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyFlightHeightSearchRegressionTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("Anchor"), TEXT("Points")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyFlightHeightSearchRegressionTest::RunTest(const FString& Parameters)
{
	FEnemyFlightHeightTestWorld Fixture;
	if (!TestTrue(TEXT("Fixture initializes"), Fixture.Initialize())) return false;
	// 컨트롤러/직접 시야 입력을 준비한 뒤 시야를 잃은 실제 수색 회차로 전환한다.
	FEnemyRunEnvQueryTaskInstanceData Combat;
	if (!TestTrue(TEXT("Combat inputs initialize"), MakeHeightCombatQuery(Fixture, TEXT("Approach"), Combat))) return false;
	Fixture.Enemy->SetPlayerCurrentlyVisible(false);
	Fixture.Enemy->UpdateLastKnownPlayerLocation(FVector(100, 0, 400));
	if (!TestTrue(TEXT("Search episode begins"), Fixture.Enemy->BeginSearchEpisode())) return false;
	ON_SCOPE_EXIT { Fixture.Enemy->EndSearchEpisode(); };
	FEnemyRunEnvQueryTaskInstanceData Data;
	Data.Enemy = Fixture.Enemy;
	Data.SearchMode = Parameters == TEXT("Anchor") ? EEnemySearchQueryMode::Anchor : EEnemySearchQueryMode::Points;
	Data.bRequireVisibleTarget = false;
	Data.RunMode = EEnvQueryRunMode::AllMatching;
	Data.MaxCollisionChecks = 4096;
	Data.MaxPathCandidates = 32;
	Data.MaxSearchMilliseconds = HeightTestMaxMilliseconds;
	Data.QueryTemplate = NewObject<UEnvQuery>(Fixture.World);
	UEnvQueryOption* Option = NewObject<UEnvQueryOption>(Data.QueryTemplate);
	Data.QueryTemplate->GetOptionsMutable().Add(Option);
	UEnemyEnvQueryGenerator_Search* Generator = NewObject<UEnemyEnvQueryGenerator_Search>(Option);
	Generator->Mode = Data.SearchMode;
	Option->Generator = Generator;
	Option->Tests.Add(NewObject<UEnemyEnvQueryTest_FlightDestination>(Option));
	EnemySearchTesting::CaptureQuery(Data);
	Data.QueryResult = EEnemyEnvQueryResult::Running;
	FEnvQueryRequest Request(Data.QueryTemplate, Fixture.Enemy);
	if (!TestTrue(TEXT("Search request prepares with combat height enabled"), EnemySearchTesting::PrepareRequest(Data, Request))) return false;
	TestTrue(TEXT("Search does not spend combat height correction checks"), Data.HeightSnapshot.Candidates.IsEmpty() && Data.HeightSnapshot.CollisionChecks == 0);
	TSharedPtr<FEnvQueryResult> Result;
	const auto ChangeCombatHeight = [&]
	{
		Fixture.Enemy->FlightHeightSettings.HighHeight = 800;
		Fixture.Enemy->CombatHeightPreference = EEnemyFlightHeightPreference::High;
	};
	if (!FinishHeightQuery(*this, Fixture, Data, Request, Result, ChangeCombatHeight)) return false;
	if (!TestEqual(TEXT("Combat height changes cannot invalidate search selection"), Data.QueryResult, EEnemyEnvQueryResult::Success)) return false;
	TestTrue(TEXT("Search altitude stays within current height plus/minus one metre"),
		FMath::Abs(Data.Destination.Z - Data.CapturedSearchHeight) <= 100.0);
	TestTrue(TEXT("Search does not snap to the changed combat high altitude"), Data.Destination.Z < 800);
	if (Data.SearchMode == EEnemySearchQueryMode::Points)
		TestTrue(TEXT("Search still selects at most two points"), Data.SelectedSearchPoints.Num() >= 1 && Data.SelectedSearchPoints.Num() <= 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyFlightHeightChargeRegressionTest,
	"Outlier.Enemy.EQS.FlightHeight.Integration.ChargeUnchanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyFlightHeightChargeRegressionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemyFlightHeightTestWorld Fixture;
	if (!TestTrue(TEXT("Fixture initializes"), Fixture.Initialize())) return false;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASelfDestructDrone* Drone = Fixture.World->SpawnActor<ASelfDestructDrone>(
		ASelfDestructDrone::StaticClass(), FTransform(FVector(0, 1000, 400)), Spawn);
	AEnemyAIController* Controller = Fixture.World->SpawnActor<AEnemyAIController>();
	AActor* Target = Fixture.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Charge drone exists"), Drone) || !TestNotNull(TEXT("Charge controller exists"), Controller)
		|| !TestNotNull(TEXT("Charge target exists"), Target)) return false;
	UBoxComponent* Root = NewObject<UBoxComponent>(Target);
	Target->SetRootComponent(Root);
	Root->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Root->RegisterComponent();
	Target->SetActorLocation(FVector(400, 1000, 400));
	Drone->GetRoomTagComp()->AssignDefaultRoomTag(Fixture.RoomTag);
	Drone->FlightHeightSettings.bEnabled = true;
	Drone->FlightHeightSettings.HighHeight = 800;
	Drone->EnterCombatFromRoom(Target->GetActorLocation(), false);
	Controller->Possess(Drone);
	Controller->SetCachedVisibleTargetForTesting(Target);
	Drone->SetPlayerCurrentlyVisible(true);
	UCapsuleComponent* Capsule = Drone->GetCapsuleComponent();
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Capsule->SetCollisionResponseToAllChannels(ECR_Block);
	Fixture.World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
	Fixture.World->GetPhysicsScene()->Flush();
	for (EEnemyFlightHeightPreference Preference : {EEnemyFlightHeightPreference::Low,
		EEnemyFlightHeightPreference::Mid, EEnemyFlightHeightPreference::High})
	{
		Drone->CombatHeightPreference = Preference;
		TestEqual(TEXT("Height preference cannot alter charge range/straight path eligibility"),
			EnemyCharge::Assess(Drone, Controller, Target, 500, 5), EEnemyChargeEligibility::Ready);
	}
	if (!TestTrue(TEXT("Charge commits the current direct heading"),
		Drone->BeginCommittedSelfDestruct(Target, 0, 400))) return false;
	ON_SCOPE_EXIT { Drone->CancelCommittedSelfDestruct(); };
	Target->SetActorLocation(FVector(400, 1400, 800));
	Drone->CombatHeightPreference = EEnemyFlightHeightPreference::High;
	Drone->SetPlayerCurrentlyVisible(false);
	Drone->UpdateCommittedSelfDestructMovement(0.1f, 400);
	TestTrue(TEXT("Height change and sight loss do not retarget a committed charge"),
		Drone->GetCharacterMovement()->Velocity.GetSafeNormal().Equals(FVector::ForwardVector, 0.001));
	return true;
}

#endif
