#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyEnvQuery.h"
#include "Enemy/EnemyPoolSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "Misc/AutomationTest.h"
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

#endif
