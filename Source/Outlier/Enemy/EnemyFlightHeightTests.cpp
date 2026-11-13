#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/AutoTurret.h"
#include "Enemy/EnemyEnvQuery.h"
#include "Enemy/EnemyPoolSubsystem.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "Enemy/EnemyStateTreeMovementTasks.h"
#include "Enemy/EnemyStateTreeTacticalTasks.h"
#include "Enemy/EnemyStateTreeSyncTask.h"
#include "Enemy/EnemyStateTreeSelfDestructTasks.h"
#include "AbilitySystemComponent.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "Weapon/LaserWeapon.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StateTreeComponent.h"
#include "Curves/CurveFloat.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/Engine.h"
#include "TimerManager.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomTagComponent.h"
#include "Room/RoomVolume.h"
#include "Shooter/ShooterCharacter.h"
#include "Weapon/RangedWeaponBase.h"
#include "UObject/UnrealType.h"

#include <limits>

namespace
{
constexpr float HeightTestClearance = 5.0f;
constexpr int32 HeightTestMaxChecks = 512;
constexpr double HeightTestMaxMilliseconds = 1000.0;

void ObserveShooterBeforeAttack(AEnemyBase& Enemy, AActor* Target)
{
	AShooterCharacter* Shooter = Cast<AShooterCharacter>(Target);
	if (!Shooter)
	{
		return;
	}
	// 선택 정책 테스트의 주입된 Sight와 실제 TP 관측은 별개다. 발사 전에는 둘 다 준비한다.
	FCanBeSeenFromContext Context;
	Context.IgnoreActor = &Enemy;
	Context.ObserverLocation = Enemy.GetPawnViewLocation();
	FVector Seen;
	int32 Sync = 0, Async = 0;
	float Strength = 0.0f;
	Shooter->CanBeSeenFrom(Context, Seen, Sync, Async, Strength, nullptr, nullptr);
	Shooter->GetMesh()->RefreshBoneTransforms();
	Shooter->CanBeSeenFrom(Context, Seen, Sync, Async, Strength, nullptr, nullptr);
	Enemy.RefreshCombatAimPoint();
}

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
		if (!GEngine)
		{
			return;
		}
		const FName Name = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("EnemyFlightHeightTest"));
		World = UWorld::CreateWorld(EWorldType::Game, false, Name, GetTransientPackage());
		if (!World)
		{
			return;
		}
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

	AActor* SpawnBlockingBox(const FVector& Location, const FVector& Extent,
		ECollisionChannel ObjectType = ECC_WorldStatic) const
	{
		AActor* Actor = World ? World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location)) : nullptr;
		if (!Actor)
		{
			return nullptr;
		}
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
		const FVector& RoomExtent = FVector(6000, 6000, 2000), bool bGun = false)
	{
		if (!World || !World->GetPhysicsScene())
		{
			return false;
		}
		Room = World->SpawnActor<ARoomVolume>(ARoomVolume::StaticClass(), FTransform::Identity);
		if (!Room)
		{
			return false;
		}
		UBoxComponent* RoomBox = Cast<UBoxComponent>(Room->GetRootComponent());
		if (!RoomBox)
		{
			return false;
		}
		RoomBox->SetBoxExtent(RoomExtent);
		RoomBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		// 기본 정적 채널의 방 트리거도 바닥으로 오인하지 않는지 함께 검증한다.
		RoomBox->SetCollisionObjectType(ECC_WorldStatic);
		RoomBox->SetCollisionResponseToAllChannels(ECR_Overlap);
		URoomCombatSubsystem* Combat = World->GetSubsystem<URoomCombatSubsystem>();
		if (!Combat || !Combat->RegisterRoom(Room, RoomTag))
		{
			return false;
		}
		if (bFloor && !SpawnBlockingBox(FVector(0, 0, -25), FVector(10000, 10000, 25)))
		{
			return false;
		}

		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		UClass* EnemyClass = bGun ? LoadClass<AEnemyBase>(nullptr,
			TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C")) : AEnemyBase::StaticClass();
		if (!EnemyClass)
		{
			return false;
		}
		Enemy = World->SpawnActor<AEnemyBase>(EnemyClass, FTransform(Start), Spawn);
		if (!Enemy)
		{
			return false;
		}
		UClass* WeaponClass = LoadClass<ARangedWeaponBase>(nullptr,
			TEXT("/Game/Blueprints/Weapon/BP_Rifle.BP_Rifle_C"));
		ARangedWeaponBase* Weapon = WeaponClass ? World->SpawnActor<ARangedWeaponBase>(WeaponClass, Spawn) : nullptr;
		FObjectPropertyBase* CurrentWeapon = FindFProperty<FObjectPropertyBase>(AEnemyBase::StaticClass(), TEXT("CurrentWeapon"));
		FFloatProperty* Range = WeaponClass ? FindFProperty<FFloatProperty>(WeaponClass, TEXT("EffectiveRange")) : nullptr;
		if (!Weapon || !CurrentWeapon || !Range)
		{
			return false;
		}
		Weapon->OnEquipped(Enemy);
		Weapon->SetActorEnableCollision(false);
		CurrentWeapon->SetObjectPropertyValue_InContainer(Enemy, Weapon);
		// 높이 테스트는 기존 후보 위치를 유지하고, 사거리 제한은 별도 회귀 테스트에서 검증한다.
		Range->SetPropertyValue_InContainer(Weapon, 5000.0f);
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
		if (bGun)
		{
			World->BeginPlay();
			Enemy->GetStateTreeComponent()->StopLogic(TEXT("Controlled flight handoff test"));
			Enemy->GetCharacterMovement()->SetComponentTickEnabled(false);
		}
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
	UClass* TargetClass = LoadClass<AActor>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	const double TargetDistance = Mode == TEXT("Approach") ? 3000.0
		: Mode == TEXT("Orbit") ? 1500.0 : Mode == TEXT("Retreat") ? 500.0 : 1000.0;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// 단차 위 타깃도 지정 위치에서 생성한다. 캡슐 루트를 유지해야 Actor 위치와 실제 조준 중심이 일치한다.
	AActor* Target = TargetClass ? Fixture.World->SpawnActor<AActor>(TargetClass,
		FVector(TargetDistance, 0, TargetHeight), FRotator::ZeroRotator, Spawn) : nullptr;
	if (!Controller || !Target)
	{
		return false;
	}
	Target->SetActorEnableCollision(false);
	Controller->Possess(Fixture.Enemy);
	Controller->SetCachedVisibleTargetForTesting(Target);
	Fixture.Enemy->ObserveTrackedTarget(Target);
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
	else if (Data.bValidateOrbitPath)
	{
		Option->Generator = NewObject<UEnemyEnvQueryGenerator_Orbit>(Option);
	}
	else if (Data.bValidateRetreatPath)
	{
		Option->Generator = NewObject<UEnemyEnvQueryGenerator_Retreat>(Option);
	}
	else if (Data.bValidateChasePath)
	{
		Option->Generator = NewObject<UEnemyEnvQueryGenerator_Chase>(Option);
	}
	else
	{
		return false;
	}
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
	if (!Test.TestNotNull(TEXT("EQS manager exists"), Manager))
	{
		return false;
	}
	bool bCompleted = false;
	const FQueryFinishedSignature Finished = FQueryFinishedSignature::CreateLambda(
		[&Data, &OutResult, &bCompleted](TSharedPtr<FEnvQueryResult> Result)
		{
			OutResult = Result;
			bCompleted = true;
			if (Result)
			{
				EnemySearchTesting::CompletePendingQuery(Data, *Result);
			}
		});
	const int32 RequestId = Request.Execute(Data.RunMode, Finished);
	Data.RequestId = RequestId;
	if (!Test.TestTrue(TEXT("Actual asynchronous EQS request starts"), RequestId != INDEX_NONE))
	{
		return false;
	}
	// 실패/조기 종료에도 스택 참조를 잡은 Delegate를 다음 테스트까지 남기지 않는다.
	ON_SCOPE_EXIT { if (!bCompleted) Manager->AbortQuery(RequestId); };
	{
		// 에디터의 EQS는 TimeOnly에서도 자동 Tick된다. 중단 조건을 만드는 동안에는 이를 막고,
		// 조건 적용이 끝난 뒤 아래의 명시적 Tick에서 실제 완료 Delegate를 실행한다.
		const ETickableTickType TickType = Manager->GetTickableTickType();
		Manager->SetTickableTickType(ETickableTickType::Never);
		ON_SCOPE_EXIT { Manager->SetTickableTickType(TickType); };
		BeforeCompletion();
	}
	if (!Test.TestFalse(TEXT("EQS completion waits until interruption is applied"), bCompleted))
	{
		return false;
	}
	for (int32 Tick = 0; Tick < 256 && !bCompleted; ++Tick)
	{
		Manager->Tick(1.0f / 60.0f);
	}
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
	if (!TestTrue(TEXT("Fixture initializes"), Fixture.Initialize()))
	{
		return false;
	}
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
	if (!TestNotNull(TEXT("Pool subsystem exists"), Pool))
	{
		return false;
	}
	Enemy.CombatHeightPreference = EEnemyFlightHeightPreference::High;
	Enemy.PrepareForPoolSpawn(Pool);
	FEnemyPoolLeaseContext Context;
	Context.RoomTag = Fixture.RoomTag;
	Context.GameplayGeneration = 1;
	if (!TestTrue(TEXT("New lease resets combat runtime"), Enemy.BeginPoolLease(Context, FTransform(FVector(0, 0, 400)), 1)))
	{
		return false;
	}
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
	if (!TestTrue(TEXT("Fixture initializes"), Fixture.Initialize()))
	{
		return false;
	}
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AEnemyBase* Enemy = Fixture.World->SpawnActor<AEnemyBase>(AEnemyBase::StaticClass(), FTransform(FVector(0, 500, 400)), Spawn);
	if (!TestNotNull(TEXT("Noncombat Enemy spawns"), Enemy))
	{
		return false;
	}
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
	if (!TestTrue(TEXT("Fixture initializes"), Fixture.Initialize()))
	{
		return false;
	}
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
	if (!TestTrue(TEXT("Fixture initializes"), Fixture.Initialize()))
	{
		return false;
	}
	if (!TestNotNull(TEXT("Raised platform spawns under candidates"),
		Fixture.SpawnBlockingBox(FVector(1500, 0, 50), FVector(300, 300, 50))))
	{
		return false;
	}
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
	if (!TestTrue(TEXT("Fixture starts below ceiling"), Fixture.Initialize(true, FVector(0, 0, 150))))
	{
		return false;
	}
	Fixture.Enemy->CombatHeightPreference = EEnemyFlightHeightPreference::High;
	if (!TestNotNull(TEXT("Ceiling bottom is 300cm"),
		Fixture.SpawnBlockingBox(FVector(1500, 0, 325), FVector(400, 400, 25))))
	{
		return false;
	}
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
	if (!TestTrue(TEXT("Fixture initializes"), Fixture.Initialize(true, FVector(0, 0, 100))))
	{
		return false;
	}
	Fixture.Enemy->CombatHeightPreference = EEnemyFlightHeightPreference::High;
	// 기본 캡슐+여유의 유효 원점 범위는 94..97cm다. 250-50*n 표본에는 없다.
	const double CeilingBottom = 2.0 * Fixture.VerticalExtent() + 5.0;
	if (!TestNotNull(TEXT("Narrow ceiling spawns"), Fixture.SpawnBlockingBox(
		FVector(1500, 0, CeilingBottom + 25), FVector(400, 400, 25))))
	{
		return false;
	}
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
	if (!TestTrue(TEXT("Fixture initializes"), Fixture.Initialize(true, FVector(0, 0, 100))))
	{
		return false;
	}
	const double CeilingBottom = 2.0 * Fixture.VerticalExtent() - 1.0;
	if (!TestNotNull(TEXT("Ceiling leaves insufficient capsule space"), Fixture.SpawnBlockingBox(
		FVector(1500, 0, CeilingBottom + 25), FVector(400, 400, 25))))
	{
		return false;
	}
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
	if (!TestTrue(TEXT("Fixture has no static floor"), Fixture.Initialize(false)))
	{
		return false;
	}
	if (!TestNotNull(TEXT("Dynamic floor decoy spawns"),
		Fixture.SpawnBlockingBox(FVector(0, 0, -25), FVector(10000, 10000, 25), ECC_WorldDynamic)))
	{
		return false;
	}
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
		Fixture.Initialize(true, FVector(0, 0, 400), FVector(1520, 1000, 1000))))
	{
		return false;
	}
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
	if (!TestTrue(TEXT("Fixture initializes"), Fixture.Initialize()))
	{
		return false;
	}
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
	if (!TestTrue(TEXT("Fixture initializes"), Fixture.Initialize()))
	{
		return false;
	}
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
	if (!TestTrue(TEXT("Fixture initializes"), Fixture.Initialize()))
	{
		return false;
	}
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
	if (!TestTrue(TEXT("Fixture initializes"), Fixture.Initialize()))
	{
		return false;
	}
	Fixture.Approach->AngularSamples = 3;
	FEnemyFlightHeightSnapshot Snapshot;
	TestEqual(TEXT("Non-axis candidates prepare"), Fixture.Prepare(Snapshot), EFlightPathStatus::Success);
	if (!TestTrue(TEXT("Serialization has multiple candidates"), Snapshot.Candidates.Num() > 2))
	{
		return false;
	}
	Snapshot.Candidates[1].Status = EFlightPathStatus::BlockedEndpoint;
	UEnvQuery* Query = NewObject<UEnvQuery>(Fixture.World);
	UEnvQueryOption* Option = NewObject<UEnvQueryOption>(Query);
	Option->Generator = NewObject<UEnemyEnvQueryGenerator_Approach>(Option);
	Query->GetOptionsMutable().Add(Option);
	FEnvQueryRequest Request(Query, Fixture.Enemy);
	EnemyFlightHeight::SetQuerySnapshot(Request, Snapshot);
	UEnvQueryManager* Manager = UEnvQueryManager::GetCurrent(Fixture.World);
	if (!TestNotNull(TEXT("EQS manager exists"), Manager))
	{
		return false;
	}
	// 실행 없이 실제 Request -> Instance 직렬화 경로의 NamedParams를 읽는다.
	const TSharedPtr<FEnvQueryInstance> Instance = Manager->PrepareQueryInstance(Request, EEnvQueryRunMode::AllMatching);
	if (!TestTrue(TEXT("EQS request prepares an instance"), Instance.IsValid()))
	{
		return false;
	}
	int32 SerializedCount = 0;
	for (const FEnemyFlightHeightCandidate& Candidate : Snapshot.Candidates)
	{
		if (Candidate.Status != EFlightPathStatus::Success)
		{
			continue;
		}
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
	if (!TestTrue(TEXT("Fixture initializes"), Fixture.Initialize()))
	{
		return false;
	}
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
			if (Distance == 300.0)
			{
				TestTrue(TEXT("Near chase removes lane bias"), FMath::IsNearlyZero(Candidate.Location.Y, 0.01));
			}
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
		if (!TestTrue(TEXT("Orbit candidates exist"), !Candidates.IsEmpty()))
		{
			return false;
		}
		for (double DestinationZ : {150.0, 650.0})
		{
			FEnemyOrbitCandidate Candidate = Candidates[0];
			Candidate.Destination.Z = DestinationZ;
			TArray<FVector> Waypoints;
			EnemyOrbit::BuildWaypoints(Start, Center, Candidate, Waypoints);
			const double CurveLength = FMath::Sqrt(FMath::Square(double(Candidate.ArcLength))
				+ FMath::Square(DestinationZ - Start.Z));
			const int32 Segments = FMath::CeilToInt(CurveLength / 50.0);
			if (!TestEqual(TEXT("Arc has bounded subdivision"), Waypoints.Num(), Segments + 1))
			{
				return false;
			}
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
	if (!Parameters.Split(TEXT("."), &Mode, &Height))
	{
		return false;
	}
	FEnemyFlightHeightTestWorld Fixture;
	if (!TestTrue(TEXT("Fixture initializes"), Fixture.Initialize()))
	{
		return false;
	}
	Fixture.Enemy->FlightHeightSettings.bEnabled = Height != TEXT("Disabled");
	Fixture.Enemy->CombatHeightPreference = Height == TEXT("Low") ? EEnemyFlightHeightPreference::Low
		: Height == TEXT("High") ? EEnemyFlightHeightPreference::High : EEnemyFlightHeightPreference::Mid;
	FEnemyRunEnvQueryTaskInstanceData Data;
	if (!TestTrue(TEXT("Combat query inputs initialize"), MakeHeightCombatQuery(Fixture, Mode, Data)))
	{
		return false;
	}
	FEnvQueryRequest Request(Data.QueryTemplate, Fixture.Enemy);
	if (!TestTrue(TEXT("Actual Task request preparation succeeds"), EnemySearchTesting::PrepareRequest(Data, Request)))
	{
		return false;
	}
	TSharedPtr<FEnvQueryResult> Result;
	if (!FinishHeightQuery(*this, Fixture, Data, Request, Result, [] {}))
	{
		return false;
	}
	if (!TestEqual(TEXT("Live EQS result passes actual Task selection"), Data.QueryResult, EEnemyEnvQueryResult::Success)
		|| !TestTrue(TEXT("Selection exposes a destination"), Data.bHasDestination))
	{
		return false;
	}
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
		if (!TestTrue(TEXT("Selected orbit contains a curve"), Data.SelectedWaypoints.Num() >= 2))
		{
			return false;
		}
		TestEqual(TEXT("Accepted orbit direction is remembered rather than the initial random preference"),
			Fixture.Enemy->PrefersCombatOrbitLeft(), Data.bSelectedOrbitLeft);
		FEnemyRunEnvQueryTaskInstanceData NextQuery;
		NextQuery.Enemy = Fixture.Enemy;
		EnemySearchTesting::CaptureQuery(NextQuery);
		TestEqual(TEXT("Next query captures the accepted direction"), NextQuery.bCapturedOrbitLeft, Data.bSelectedOrbitLeft);
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
		EnemyFlightTesting::StartPath(Fly), EStateTreeRunStatus::Running))
	{
		return false;
	}
	if (!TestTrue(TEXT("Fly has a non-empty validated route"), Fly.PathWaypoints.Num() >= 2))
	{
		return false;
	}
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
		TEXT("PreferenceChanged"), TEXT("SightLost"), TEXT("TrackingGrace"), TEXT("TrackingExpired"), TEXT("TargetChanged"), TEXT("TargetMoved"),
		TEXT("SharedSource"), TEXT("SharedObserverHandoff"), TEXT("SharedGrace"), TEXT("SharedExpired"),
		TEXT("SharedRoomEnded"), TEXT("SharedRoomChanged"), TEXT("SharedTargetChanged"),
		TEXT("Possessed"), TEXT("Death"), TEXT("PoolReuse"), TEXT("Inactive"), TEXT("RequestReplaced")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyFlightHeightCompletionIntegrationTest::RunTest(const FString& Parameters)
{
	FEnemyFlightHeightTestWorld Fixture;
	if (!TestTrue(TEXT("Fixture initializes"), Fixture.Initialize()))
	{
		return false;
	}
	if (Parameters == TEXT("Enabled"))
	{
		Fixture.Enemy->FlightHeightSettings.bEnabled = false;
	}
	FEnemyRunEnvQueryTaskInstanceData Data;
	if (!TestTrue(TEXT("Combat query inputs initialize"), MakeHeightCombatQuery(Fixture, TEXT("Approach"), Data)))
	{
		return false;
	}
	UEnemyRoomSubsystem* Rooms = nullptr;
	AEnemyBase* Observer = nullptr;
	if (Parameters.StartsWith(TEXT("Shared")))
	{
		Rooms = Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
		Observer = Fixture.World->SpawnActor<AEnemyBase>();
		if (!TestNotNull(TEXT("Shared query Room subsystem exists"), Rooms)
			|| !TestNotNull(TEXT("Shared query observer exists"), Observer))
		{
			return false;
		}
		Observer->GetRoomTagComp()->AssignDefaultRoomTag(Fixture.RoomTag);
		Observer->EnterCombatFromRoom(Data.CapturedTarget->GetActorLocation(), false);
		Rooms->SetActiveRoomTargetForTesting(Fixture.RoomTag, Data.CapturedTarget->GetActorLocation(), false);
		Rooms->ReportRoomTargetContact(Observer, Data.CapturedTarget.Get(), Data.CapturedTarget->GetActorLocation());
		Data.CachedController->SetCachedVisibleTargetForTesting(nullptr);
		Fixture.Enemy->SetPlayerCurrentlyVisible(false);
		if (!TestEqual(TEXT("Pending query captures the same Actor through shared tracking"),
			Fixture.Enemy->GetTrackedTarget(), Data.CapturedTarget.Get()))
		{
			return false;
		}
		EnemySearchTesting::CaptureQuery(Data);
	}
	Data.TargetChangeDistance = 200;
	FEnvQueryRequest Request(Data.QueryTemplate, Fixture.Enemy);
	if (!TestTrue(TEXT("Request prepares before interruption"), EnemySearchTesting::PrepareRequest(Data, Request)))
	{
		return false;
	}
	TSharedPtr<FEnvQueryResult> Result;
	bool bPoolReused = false;
	const auto Interrupt = [&]
	{
		if (Parameters == TEXT("SharedSource"))
		{
			// 실행 중 요청은 정보 출처가 아니라 Actor/수명/방 스냅샷에 귀속된다.
			Data.CachedController->SetCachedVisibleTargetForTesting(Data.CapturedTarget.Get());
			Fixture.Enemy->SetPlayerCurrentlyVisible(true);
			Data.CachedController->SetCachedVisibleTargetForTesting(nullptr);
			Fixture.Enemy->SetPlayerCurrentlyVisible(false);
		}
		else if (Parameters == TEXT("SharedObserverHandoff"))
		{
			AEnemyBase* NextObserver = Fixture.World->SpawnActor<AEnemyBase>();
			if (!TestNotNull(TEXT("Replacement query observer exists"), NextObserver))
			{
				return;
			}
			NextObserver->GetRoomTagComp()->AssignDefaultRoomTag(Fixture.RoomTag);
			NextObserver->EnterCombatFromRoom(Data.CapturedTarget->GetActorLocation(), false);
			Rooms->RemoveRoomTargetObserver(Observer);
			Rooms->ReportRoomTargetContact(NextObserver, Data.CapturedTarget.Get(), Data.CapturedTarget->GetActorLocation());
		}
		else if (Parameters == TEXT("SharedGrace") || Parameters == TEXT("SharedExpired"))
		{
			Rooms->RemoveRoomTargetObserver(Observer);
			Fixture.Enemy->RefreshTargetTracking();
			if (Parameters == TEXT("SharedExpired"))
			{
				// 비동기 EQS 완료는 보류하고 마지막 직접 보고 시각에 따른 유예만 만료시킨다.
				Fixture.World->GetTimerManager().Tick(0.0f);
				const int32 Steps = FMath::CeilToInt((Fixture.Enemy->GetRuntimeStat().TargetTrackingGraceSeconds + 0.1f) * 32.0f);
				for (int32 Step = 0; Step < Steps; ++Step)
				{
					++GFrameCounter;
					Fixture.World->Tick(LEVELTICK_TimeOnly, 1.0f / 32.0f);
					Fixture.World->GetTimerManager().Tick(Fixture.World->GetDeltaSeconds());
				}
				TestFalse(TEXT("Shared tracking expires before EQS completion"), Fixture.Enemy->HasTrackableTarget());
			}
		}
		else if (Parameters == TEXT("SharedRoomEnded"))
		{
			Rooms->NotifyRoomCombatEnded(Fixture.RoomTag);
		}
		else if (Parameters == TEXT("SharedRoomChanged"))
		{
			Fixture.Enemy->GetRoomTagComp()->AssignDefaultRoomTag(
				FGameplayTag::RequestGameplayTag(TEXT("Room.Level01.2")));
		}
		else if (Parameters == TEXT("SharedTargetChanged"))
		{
			AActor* NextTarget = Fixture.World->SpawnActor<AActor>(Data.CapturedTarget->GetClass());
			if (!TestNotNull(TEXT("Replacement query player exists"), NextTarget))
			{
				return;
			}
			NextTarget->SetActorEnableCollision(false);
			NextTarget->SetActorLocation(Data.CapturedTarget->GetActorLocation());
			Rooms->NotifyTargetActorRemoved(Data.CapturedTarget.Get());
			Rooms->ReportRoomTargetContact(Observer, NextTarget, NextTarget->GetActorLocation());
			Fixture.Enemy->RefreshTargetTracking();
			TestEqual(TEXT("New shared Actor replaces the removed target"), Fixture.Enemy->GetTrackedTarget(), NextTarget);
		}
		else if (Parameters == TEXT("SettingsChanged"))
		{
			Fixture.Enemy->FlightHeightSettings.MidHeight += 50;
		}
		else if (Parameters == TEXT("Disabled"))
		{
			Fixture.Enemy->FlightHeightSettings.bEnabled = false;
		}
		else if (Parameters == TEXT("Enabled"))
		{
			Fixture.Enemy->FlightHeightSettings.bEnabled = true;
		}
		else if (Parameters == TEXT("PreferenceChanged"))
		{
			Fixture.Enemy->CombatHeightPreference = EEnemyFlightHeightPreference::High;
		}
		else if (Parameters == TEXT("SightLost") || Parameters == TEXT("TrackingGrace") || Parameters == TEXT("TrackingExpired"))
		{
			if (Parameters == TEXT("SightLost"))
			{
				const_cast<FEnemyStat&>(Fixture.Enemy->GetRuntimeStat()).TargetTrackingGraceSeconds = 0;
			}
			Data.CachedController->SetCachedVisibleTargetForTesting(nullptr);
			Fixture.Enemy->SetPlayerCurrentlyVisible(false);
			if (Parameters == TEXT("TrackingExpired"))
			{
				// 대기 타이머를 먼저 활성화하고 고정 만료 기준인 월드 시각도 함께 진행한다.
				// EQS 자동 Tick은 FinishHeightQuery가 차단하고 TimeOnly로 Actor 감지/이동을 보류한다.
				++GFrameCounter;
				Fixture.World->GetTimerManager().Tick(0.0f);
				const float WaitSeconds = Fixture.Enemy->GetRuntimeStat().TargetTrackingGraceSeconds + 0.1f;
				const int32 Steps = FMath::CeilToInt(WaitSeconds * 60.0f);
				for (int32 Step = 0; Step < Steps; ++Step)
				{
					++GFrameCounter;
					Fixture.World->Tick(LEVELTICK_TimeOnly, WaitSeconds / Steps);
					Fixture.World->GetTimerManager().Tick(Fixture.World->GetDeltaSeconds());
				}
				TestFalse(TEXT("Tracking expires before the pending EQS result is delivered"), Fixture.Enemy->HasTrackableTarget());
			}
		}
		else if (Parameters == TEXT("TargetChanged"))
		{
			Data.CachedController->SetCachedVisibleTargetForTesting(nullptr);
		}
		else if (Parameters == TEXT("TargetMoved"))
		{
			Data.CapturedTarget->AddActorWorldOffset(FVector(250, 0, 0));
		}
		else if (Parameters == TEXT("Possessed"))
		{
			Fixture.Enemy->SetEnemyPossessed(true);
		}
		else if (Parameters == TEXT("Death"))
		{
			Fixture.Enemy->GetAbilitySystemComponent()->AddLooseGameplayTag(
				FGameplayTag::RequestGameplayTag(TEXT("State.Dead")));
		}
		else if (Parameters == TEXT("PoolReuse"))
		{
			Fixture.Enemy->PrepareForPoolSpawn(Fixture.World->GetSubsystem<UEnemyPoolSubsystem>());
			FEnemyPoolLeaseContext Lease;
			Lease.RoomTag = Fixture.RoomTag;
			Lease.GameplayGeneration = Data.CapturedGeneration + 1;
			bPoolReused = Fixture.Enemy->BeginPoolLease(Lease,
				FTransform(Fixture.Enemy->GetActorLocation()), Data.CapturedLease + 1);
		}
		else if (Parameters == TEXT("Inactive"))
		{
			Data.bActive = false;
		}
		else if (Parameters == TEXT("RequestReplaced"))
		{
			++Data.RequestId;
		}
	};
	if (!FinishHeightQuery(*this, Fixture, Data, Request, Result, Interrupt))
	{
		return false;
	}
	if (Parameters == TEXT("TrackingGrace") || Parameters == TEXT("SharedSource")
		|| Parameters == TEXT("SharedObserverHandoff") || Parameters == TEXT("SharedGrace"))
	{
		TestTrue(TEXT("Delayed EQS result remains usable for the same target during grace"), Data.bHasDestination);
		TestEqual(TEXT("Grace does not make a valid snapshot stale"), Data.QueryResult, EEnemyEnvQueryResult::Success);
		return true;
	}
	if (Parameters == TEXT("PoolReuse"))
	{
		TestTrue(TEXT("Actual pool lease reset occurred before completion"), bPoolReused);
	}
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
	if (!Parameters.Split(TEXT("."), &Mode, &Environment))
	{
		return false;
	}
	const bool bMissingFloor = Environment == TEXT("MissingFloor");
	FEnemyFlightHeightTestWorld Fixture;
	if (!TestTrue(TEXT("Fixture initializes"), Fixture.Initialize(!bMissingFloor)))
	{
		return false;
	}
	if (Environment == TEXT("LowCeiling"))
	{
		Fixture.Enemy->FlightHeightSettings.HighHeight = 800;
		Fixture.Enemy->CombatHeightPreference = EEnemyFlightHeightPreference::High;
		if (!TestNotNull(TEXT("Low ceiling spawns"), Fixture.SpawnBlockingBox(FVector(0, 0, 675), FVector(5000, 5000, 25))))
		{
			return false;
		}
	}
	else if (Environment == TEXT("FloorStep"))
	{
		if (!TestNotNull(TEXT("Raised floor spawns"), Fixture.SpawnBlockingBox(FVector(0, 0, 25), FVector(5000, 5000, 50))))
		{
			return false;
		}
	}
	else if (Environment == TEXT("RoomBoundary"))
	{
		CastChecked<UBoxComponent>(Fixture.Room->GetRootComponent())->SetBoxExtent(FVector(2000, 2500, 2000));
	}
	FEnemyRunEnvQueryTaskInstanceData Data;
	// 단차로 바닥이 75cm 올라가면 타깃도 같은 만큼 올린다. 타깃이 바닥 아래면 선회/후퇴의 시야 검사가 정상적으로 실패한다.
	const double TargetHeight = Environment == TEXT("FloorStep") ? 125.0 : 50.0;
	if (!TestTrue(TEXT("Combat query inputs initialize"), MakeHeightCombatQuery(Fixture, Mode, Data, TargetHeight)))
	{
		return false;
	}
	if (Environment == TEXT("FloorStep"))
	{
		TestTrue(TEXT("Raised-floor target remains above the floor"), Data.CapturedOrbitCenter.Z > 75.0);
	}
	FEnvQueryRequest Request(Data.QueryTemplate, Fixture.Enemy);
	if (!TestTrue(TEXT("Environmental query prepares"), EnemySearchTesting::PrepareRequest(Data, Request)))
	{
		return false;
	}
	if (Environment == TEXT("Obstacle"))
	{
		const FEnemyFlightHeightCandidate* Candidate = Data.HeightSnapshot.Candidates.FindByPredicate(
			[](const FEnemyFlightHeightCandidate& Item) { return Item.Status == EFlightPathStatus::Success; });
		if (!TestNotNull(TEXT("Original corrected candidate exists"), Candidate))
		{
			return false;
		}
		if (!TestNotNull(TEXT("Candidate obstacle spawns"), Fixture.SpawnBlockingBox(Candidate->Location, FVector(80, 80, 80))))
		{
			return false;
		}
		// 후보가 있던 곳에 장애물을 놓은 뒤 실제 요청 준비부터 다시 진행한다.
		Request = FEnvQueryRequest(Data.QueryTemplate, Fixture.Enemy);
		if (!TestTrue(TEXT("Obstacle-aware query prepares"), EnemySearchTesting::PrepareRequest(Data, Request)))
		{
			return false;
		}
	}
	TSharedPtr<FEnvQueryResult> Result;
	if (!FinishHeightQuery(*this, Fixture, Data, Request, Result, [] {}))
	{
		return false;
	}
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
	if (!TestEqual(TEXT("Environmental query selects a safe candidate"), Data.QueryResult, EEnemyEnvQueryResult::Success))
	{
		return false;
	}
	if (Environment == TEXT("LowCeiling"))
	{
		TestTrue(TEXT("Selected capsule fits below the ceiling"), Data.Destination.Z + Fixture.VerticalExtent() <= 650.01);
	}
	if (Environment == TEXT("FloorStep"))
	{
		TestTrue(TEXT("Selected height uses the raised floor"), Data.Destination.Z >= 75.0 + Fixture.VerticalExtent() - 0.01);
	}
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
	if (!Parameters.Split(TEXT("."), &Mode, &Failure))
	{
		return false;
	}
	FEnemyFlightHeightTestWorld Fixture;
	if (!TestTrue(TEXT("Fixture initializes"), Fixture.Initialize()))
	{
		return false;
	}
	if (Failure == TEXT("InvalidSettings"))
	{
		Fixture.Enemy->FlightHeightSettings.MidHeight = -1;
	}
	FEnemyRunEnvQueryTaskInstanceData Data;
	if (!TestTrue(TEXT("Combat query inputs initialize"), MakeHeightCombatQuery(Fixture, Mode, Data)))
	{
		return false;
	}
	if (Failure == TEXT("PreparationBudget"))
	{
		Data.MaxCollisionChecks = 1;
	}
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
		if (!TestTrue(TEXT("Height preparation succeeds before selection budget is exhausted"), EnemySearchTesting::PrepareRequest(Data, Request)))
		{
			return false;
		}
		TSharedPtr<FEnvQueryResult> Result;
		// 보정에 사용한 검사만 남겨 선택 단계의 공유 예산 소진을 재현한다. 높이 보정 자체는 다시 실행하지 않는다.
		const auto ExhaustSelectionBudget = [&] { Data.MaxCollisionChecks = Data.HeightSnapshot.CollisionChecks; };
		if (!FinishHeightQuery(*this, Fixture, Data, Request, Result, ExhaustSelectionBudget))
		{
			return false;
		}
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
	if (!TestTrue(TEXT("Fixture initializes"), Fixture.Initialize()))
	{
		return false;
	}
	// 컨트롤러/직접 시야 입력을 준비한 뒤 시야를 잃은 실제 수색 회차로 전환한다.
	FEnemyRunEnvQueryTaskInstanceData Combat;
	if (!TestTrue(TEXT("Combat inputs initialize"), MakeHeightCombatQuery(Fixture, TEXT("Approach"), Combat)))
	{
		return false;
	}
	// 수색 회귀는 추적 유예가 끝난 뒤부터 검사한다. 직접 시야 캐시와 추적 대상도 함께 정리한다.
	Combat.CachedController->SetCachedVisibleTargetForTesting(nullptr);
	Fixture.Enemy->SetPlayerCurrentlyVisible(false);
	Fixture.Enemy->ClearTargetTracking();
	Fixture.Enemy->UpdateLastKnownPlayerLocation(FVector(100, 0, 400));
	// 이 검사는 전투 높이 설정의 간섭만 비교한다. 기존 두 지점의 질의 예산을 고정한다.
	const_cast<FEnemyStat&>(Fixture.Enemy->GetRuntimeStat()).SearchMaxPoints = 2;
	if (!TestTrue(TEXT("Search episode begins"), Fixture.Enemy->BeginSearchEpisode()))
	{
		return false;
	}
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
	if (!TestTrue(TEXT("Search request prepares with combat height enabled"), EnemySearchTesting::PrepareRequest(Data, Request)))
	{
		return false;
	}
	TestTrue(TEXT("Search does not spend combat height correction checks"), Data.HeightSnapshot.Candidates.IsEmpty() && Data.HeightSnapshot.CollisionChecks == 0);
	TSharedPtr<FEnvQueryResult> Result;
	const auto ChangeCombatHeight = [&]
	{
		Fixture.Enemy->FlightHeightSettings.HighHeight = 800;
		Fixture.Enemy->CombatHeightPreference = EEnemyFlightHeightPreference::High;
	};
	if (!FinishHeightQuery(*this, Fixture, Data, Request, Result, ChangeCombatHeight))
	{
		return false;
	}
	if (!TestEqual(TEXT("Combat height changes cannot invalidate search selection"), Data.QueryResult, EEnemyEnvQueryResult::Success))
	{
		return false;
	}
	TestTrue(TEXT("Search altitude stays within current height plus/minus one metre"),
		FMath::Abs(Data.Destination.Z - Data.CapturedSearchHeight) <= 100.0);
	TestTrue(TEXT("Search does not snap to the changed combat high altitude"), Data.Destination.Z < 800);
	if (Data.SearchMode == EEnemySearchQueryMode::Points)
	{
		TestTrue(TEXT("Search still selects at most two points"), Data.SelectedSearchPoints.Num() >= 1 && Data.SelectedSearchPoints.Num() <= 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyFlightHeightChargeRegressionTest,
	"Outlier.Enemy.EQS.FlightHeight.Integration.ChargeUnchanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyFlightHeightChargeRegressionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemyFlightHeightTestWorld Fixture;
	if (!TestTrue(TEXT("Fixture initializes"), Fixture.Initialize()))
	{
		return false;
	}
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASelfDestructDrone* Drone = Fixture.World->SpawnActor<ASelfDestructDrone>(
		ASelfDestructDrone::StaticClass(), FTransform(FVector(0, 1000, 400)), Spawn);
	AEnemyAIController* Controller = Fixture.World->SpawnActor<AEnemyAIController>();
	UClass* TargetClass = LoadClass<AActor>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	AActor* Target = TargetClass ? Fixture.World->SpawnActor<AActor>(TargetClass) : nullptr;
	if (!TestNotNull(TEXT("Charge drone exists"), Drone) || !TestNotNull(TEXT("Charge controller exists"), Controller)
		|| !TestNotNull(TEXT("Charge target exists"), Target))
	{
		return false;
	}
	Target->SetActorEnableCollision(false);
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
	Drone->ObserveTrackedTarget(Target);
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
		Drone->BeginCommittedSelfDestruct(Target, 0, 400)))
	{
		return false;
	}
	ON_SCOPE_EXIT { Drone->CancelCommittedSelfDestruct(); };
	Target->SetActorLocation(FVector(400, 1400, 800));
	Drone->CombatHeightPreference = EEnemyFlightHeightPreference::High;
	Drone->SetPlayerCurrentlyVisible(false);
	Drone->UpdateCommittedSelfDestructMovement(0.1f, 400);
	TestTrue(TEXT("Height change and sight loss do not retarget a committed charge"),
		Drone->GetCharacterMovement()->Velocity.GetSafeNormal().Equals(FVector::ForwardVector, 0.001));
	APartnerCharacter* Partner = Fixture.World->SpawnActor<APartnerCharacter>(APartnerCharacter::StaticClass(),
		FVector(200, 1000, 400), FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("Another visible player appears during the committed charge"), Partner))
	{
		return false;
	}
	Controller->SetVisibleTargetsForTesting({Partner});
	Drone->SetPlayerCurrentlyVisible(true);
	Drone->RefreshTargetTracking();
	TestEqual(TEXT("Direct sight cannot replace the target of an already committed charge"), Drone->GetTrackedTarget(), Target);
	Drone->UpdateCommittedSelfDestructMovement(0.1f, 400);
	TestTrue(TEXT("Direct sight cannot redirect the already committed heading"),
		Drone->GetCharacterMovement()->Velocity.GetSafeNormal().Equals(FVector::ForwardVector, 0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyOrbitDirectionContinuityTest,
	"Outlier.Enemy.Flight.OrbitDirectionContinuity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyOrbitDirectionContinuityTest::RunTest(const FString& Parameters)
{
	FEnemyFlightHeightTestWorld Fixture;
	if (!Fixture.Initialize())
	{
		return false;
	}
	Fixture.Enemy->FlightHeightSettings.bEnabled = false;
	FEnemyRunEnvQueryTaskInstanceData Query;
	if (!MakeHeightCombatQuery(Fixture, TEXT("Orbit"), Query))
	{
		return false;
	}
	const bool InitialDirection = Query.bCapturedOrbitLeft;
	TArray<FEnemyOrbitCandidate> Candidates;
	EnemyOrbit::BuildCandidates(Query.CapturedOrbitStart, Query.CapturedOrbitCenter, 350, InitialDirection, Candidates);
	TArray<FVector> ShortArc;
	EnemyOrbit::BuildWaypoints(Query.CapturedOrbitStart, Query.CapturedOrbitCenter, Candidates[2], ShortArc);
	if (!TestTrue(TEXT("Canonical short route contains an intermediate point"), ShortArc.IsValidIndex(2)))
	{
		return false;
	}
	// 같은 방향의 긴/짧은 곡선이 공유하는 앞 구간만 막는다. 반대 방향은 실제 공간 검사로 살아남아야 한다.
	Fixture.SpawnBlockingBox(ShortArc[2], FVector(30, 30, 100));
	FEnvQueryRequest Request(Query.QueryTemplate, Fixture.Enemy);
	if (!TestTrue(TEXT("Blocked-direction query prepares"), EnemySearchTesting::PrepareRequest(Query, Request)))
	{
		return false;
	}
	TSharedPtr<FEnvQueryResult> Result;
	if (!FinishHeightQuery(*this, Fixture, Query, Request, Result, [] {}))
	{
		return false;
	}
	if (!TestEqual(TEXT("Actual EQS selects an unblocked route"), Query.QueryResult, EEnemyEnvQueryResult::Success))
	{
		return false;
	}
	TestEqual(TEXT("Physical blockage selects the opposite direction"), Query.bSelectedOrbitLeft, !InitialDirection);
	TestEqual(TEXT("Accepted opposite direction replaces the random preference"),
		Fixture.Enemy->PrefersCombatOrbitLeft(), !InitialDirection);
	Fixture.Enemy->RememberCombatOrbitDirection(Fixture.Room, InitialDirection);
	TestEqual(TEXT("Unrelated target cannot overwrite remembered direction"),
		Fixture.Enemy->PrefersCombatOrbitLeft(), !InitialDirection);
	FEnemyRunEnvQueryTaskInstanceData Next;
	Next.Enemy = Fixture.Enemy;
	EnemySearchTesting::CaptureQuery(Next);
	TestEqual(TEXT("Next selection continues the actual accepted direction"), Next.bCapturedOrbitLeft, !InitialDirection);
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyCombatFlightHandoffTest,
	"Outlier.Enemy.Flight.CombatHandoff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyCombatFlightHandoffTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("Resume"), TEXT("SnapshotLimit"), TEXT("Expiry"), TEXT("Observation"),
		TEXT("BlockedReplacement"), TEXT("ControllerChanged"), TEXT("RoomChanged"), TEXT("WithCurves"),
		TEXT("LowFPS"), TEXT("Stun"), TEXT("TargetChanged"), TEXT("BudgetClock")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyCombatFlightHandoffTest::RunTest(const FString& Parameters)
{
	FEnemyFlightHeightTestWorld Fixture;
	if (!TestTrue(TEXT("Saved Gun flight world initializes"),
		Fixture.Initialize(true, FVector(0, 0, 400), FVector(6000, 6000, 2000), true)))
	{
		return false;
	}
	FEnemyRunEnvQueryTaskInstanceData Query;
	if (!TestTrue(TEXT("Visible target and registered movement room initialize"),
		MakeHeightCombatQuery(Fixture, TEXT("Orbit"), Query)))
	{
		return false;
	}
	Fixture.Enemy->GetStateTreeComponent()->StopLogic(TEXT("Controlled flight handoff test"));
	CastChecked<AEnemyAIController>(Fixture.Enemy->GetController())->SetEnemyPerceptionEnabled(false);
	// 자동 감지를 끄면 추적도 정리된다. 그 뒤에 통제된 직접 관측을 다시 주입한다.
	AActor* Target = Query.SelectedTargetActor;
	CastChecked<AEnemyAIController>(Fixture.Enemy->GetController())->SetCachedVisibleTargetForTesting(Target);
	Fixture.Enemy->ObserveTrackedTarget(Target);
	Fixture.Enemy->SetPlayerCurrentlyVisible(true);
	if (Parameters == TEXT("BudgetClock"))
	{
		FindFProperty<FStructProperty>(AEnemyBase::StaticClass(), TEXT("RuntimeStat"))
			->ContainerPtrToValuePtr<FEnemyStat>(Fixture.Enemy)->TacticalMaxSeconds = 0.01f;
	}
	const uint64 Serial = Fixture.Enemy->EnsureCombatTacticalEpisode(Fixture.Enemy->GetTrackedTarget(), Query.CapturedOrbitCenter);
	if (!TestTrue(TEXT("Production Gun owns a current tactical episode"), Serial != 0))
	{
		return false;
	}
	if (Parameters == TEXT("BudgetClock"))
	{
		TestTrue(TEXT("Selection budget begins before movement"), Fixture.Enemy->TryBeginCombatTacticalRequest());
	}
	UCurveFloat* Curve = nullptr;
	if (Parameters == TEXT("WithCurves"))
	{
		Curve = NewObject<UCurveFloat>();
		Curve->FloatCurve.AddKey(0, 0);
		Curve->FloatCurve.AddKey(1, 1);
	}
	const auto MakeRoute = [&Fixture, &Query, Serial, Curve](const FVector& Direction)
	{
		FEnemyFlyToLocationTaskInstanceData Data = MakeHeightFlightData(Query);
		Data.CapturedCombatTacticalSerial = Serial;
		Data.SpeedProfile.AccelerationCurve = Curve;
		Data.PlannedWaypoints.Reset();
		for (int32 Index = 0; Index <= 6; ++Index)
		{
			Data.PlannedWaypoints.Add(Fixture.Enemy->GetActorLocation() + Direction * (50.0f * Index));
		}
		Data.Destination = Data.LastDestination = Data.PlannedWaypoints.Last();
		return Data;
	};
	FEnemyFlyToLocationTaskInstanceData Old = MakeRoute(FVector::ForwardVector);
	const auto ExpireTimer = [&Fixture](float Seconds)
	{
		// 새 Timer는 첫 Tick에서 Pending 목록을 벗어난다. 실제 프레임 경계와 만료를 모두 진행한다.
		++GFrameCounter;
		Fixture.World->GetTimerManager().Tick(0.0f);
		++GFrameCounter;
		Fixture.World->GetTimerManager().Tick(Seconds);
	};
	ON_SCOPE_EXIT { Fixture.Enemy->ClearCombatFlightHandoff(); EnemyFlightTesting::Stop(Old); };
	if (!TestEqual(TEXT("Real flight validates the original route"),
		EnemyFlightTesting::StartPath(Old), EStateTreeRunStatus::Running))
	{
		return false;
	}
	Old.SpeedProfileState.Elapsed = 0.25f;
	Old.SpeedProfileState.TargetSpeed = Fixture.Enemy->GetRuntimeStat().MoveSpeed;
	UCharacterMovementComponent* Movement = Fixture.Enemy->GetCharacterMovement();
	Movement->Velocity = FVector(100, 0, 0);
	if (!TestTrue(TEXT("Moving task offers its validated route"), Fixture.Enemy->OfferCombatFlightHandoff(Old)))
	{
		return false;
	}
	TestFalse(TEXT("Original task relinquishes movement ownership"), Old.bOwnsMovement);
	TestFalse(TEXT("Offer retains actual flight velocity"), Movement->Velocity.IsNearlyZero());
	Fixture.Enemy->TickCombatFlightHandoff(0.016f);
	TestFalse(TEXT("Query update follows the validated remaining route"), Movement->Velocity.IsNearlyZero());
	TestTrue(TEXT("Borrowed movement stays within the query start tolerance"), Movement->MaxFlySpeed * 0.016f <= 20.01f);
	if (Parameters == TEXT("Resume") || Parameters == TEXT("WithCurves") || Parameters == TEXT("LowFPS"))
	{
		if (Parameters == TEXT("LowFPS"))
		{
			Movement->SetMovementMode(MOVE_Flying);
			const FVector Start = Fixture.Enemy->GetActorLocation();
			for (int32 Frame = 0; Frame < 12; ++Frame)
			{
				Fixture.Enemy->TickCombatFlightHandoff(1.0f / 15.0f);
				Movement->TickComponent(1.0f / 15.0f, LEVELTICK_All, nullptr);
				TestTrue(TEXT("Physical low-FPS movement stays within the snapshot tolerance"),
					FVector::Distance(Start, Fixture.Enemy->GetActorLocation()) < 25.0);
			}
			TestTrue(TEXT("Safe remaining distance eventually ends borrowed movement"), Movement->Velocity.IsNearlyZero());
			return true;
		}
		FEnemyFlyToLocationTaskInstanceData Next = MakeRoute(FVector::RightVector);
		ON_SCOPE_EXIT { EnemyFlightTesting::Stop(Next); };
		if (!TestEqual(TEXT("Replacement route is independently validated"),
			EnemyFlightTesting::StartPath(Next), EStateTreeRunStatus::Running))
		{
			return false;
		}
		TestTrue(TEXT("New route preserves the acceleration clock"), Next.SpeedProfileState.Elapsed >= 0.25f);
		TestTrue(TEXT("New route recomputes path distances"), Next.PathWaypoints.Last().Equals(Next.Destination, 0.01));
		const FVector Before = Movement->Velocity;
		EnemyFlightTesting::Stop(Old);
		TestTrue(TEXT("Late original task cleanup cannot stop the new owner"), Movement->Velocity.Equals(Before, 0.01));
		ExpireTimer(Next.RepathInterval + 0.01f);
		TestTrue(TEXT("Old expiry timer cannot stop the new owner"), Movement->Velocity.Equals(Before, 0.01));
	}
	else if (Parameters == TEXT("ControllerChanged"))
	{
		AEnemyAIController* Replacement = Fixture.World->SpawnActor<AEnemyAIController>();
		Replacement->Possess(Fixture.Enemy);
		Movement->Velocity = FVector(0, 123, 0);
		Fixture.Enemy->TickCombatFlightHandoff(0.016f);
		TestTrue(TEXT("Old handoff cannot stop a replacement controller"), Movement->Velocity.Equals(FVector(0, 123, 0)));
	}
	else
	{
		if (Parameters == TEXT("SnapshotLimit"))
		{
			Fixture.Enemy->SetActorLocation(Fixture.Enemy->GetActorLocation() + FVector(21, 0, 0));
			Fixture.Enemy->TickCombatFlightHandoff(0.016f);
		}
		else if (Parameters == TEXT("Expiry"))
		{
			ExpireTimer(Old.RepathInterval + 0.01f);
		}
		else if (Parameters == TEXT("Observation"))
		{
			Fixture.Enemy->EnterCombatObservationWait();
		}
		else if (Parameters == TEXT("BudgetClock"))
		{
			++GFrameCounter;
			Fixture.World->Tick(LEVELTICK_TimeOnly, 0.02f);
			TestFalse(TEXT("Borrowed movement cannot suspend selection budget expiry"), Fixture.Enemy->CanContinueCombatTacticalRequest());
		}
		else if (Parameters == TEXT("Stun"))
		{
			Fixture.Enemy->EnterStun();
		}
		else if (Parameters == TEXT("TargetChanged"))
		{
			Fixture.Enemy->ClearTargetTracking();
			Fixture.Enemy->TickCombatFlightHandoff(0.016f);
		}
		else if (Parameters == TEXT("RoomChanged"))
		{
			Fixture.Enemy->GetRoomTagComp()->AssignDefaultRoomTag(FGameplayTag());
			Fixture.Enemy->TickCombatFlightHandoff(0.016f);
		}
		else if (Parameters == TEXT("BlockedReplacement"))
		{
			Fixture.SpawnBlockingBox(Fixture.Enemy->GetActorLocation() + FVector(0, 150, 0), FVector(100, 20, 100));
			FEnemyFlyToLocationTaskInstanceData Next = MakeRoute(FVector::RightVector);
			TestEqual(TEXT("Replacement route rejects actual collision"),
				EnemyFlightTesting::StartPath(Next), EStateTreeRunStatus::Failed);
		}
		TestTrue(TEXT("Unsafe or expired handoff stops movement"), Movement->Velocity.IsNearlyZero());
		FEnemyFlyToLocationTaskInstanceData Next = MakeRoute(FVector::ForwardVector);
		// 만료/중단된 상태는 이후 Move에 커브 시간을 넘기지 않는다. 검증 전 호출로도 계승이 없어야 한다.
		Fixture.Enemy->ResumeCombatFlightHandoff(Next);
		TestEqual(TEXT("Expired handoff has no acceleration state to inherit"), Next.SpeedProfileState.Elapsed, 0.0f);
	}
	return true;
}

namespace
{
AEnemyBase* SpawnReservationEnemy(FEnemyFlightHeightTestWorld& Fixture, const FVector& Position)
{
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AEnemyBase* Enemy = Fixture.World->SpawnActor<AEnemyBase>(Fixture.Enemy->GetClass(), FTransform(Position), Spawn);
	AEnemyAIController* Controller = Fixture.World->SpawnActor<AEnemyAIController>();
	if (!Enemy || !Controller)
	{
		return nullptr;
	}
	Enemy->GetRoomTagComp()->AssignDefaultRoomTag(Fixture.RoomTag);
	Enemy->EnterCombatFromRoom(FVector(3000, 0, 50), false);
	Controller->Possess(Enemy);
	Controller->SetCachedVisibleTargetForTesting(Fixture.Enemy->GetTrackedTarget());
	Enemy->ObserveTrackedTarget(Fixture.Enemy->GetTrackedTarget());
	Enemy->SetPlayerCurrentlyVisible(true);
	Enemy->GetStateTreeComponent()->StopLogic(TEXT("Controlled combat reservation test"));
	Enemy->GetCharacterMovement()->SetComponentTickEnabled(false);
	Enemy->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Enemy->EnsureCombatTacticalEpisode(Enemy->GetTrackedTarget(), Enemy->GetTrackedTarget()->GetActorLocation());
	Fixture.World->GetSubsystem<UEnemyRoomSubsystem>()->RegisterEnemy(Enemy);
	return Enemy;
}

bool ReserveTestDestination(UEnemyRoomSubsystem& Rooms, AEnemyBase& Enemy, uint64 Request,
	const FVector& Destination, const TArray<FEnemyCombatCorridor>& Corridors = {})
{
	return Rooms.TryReserveCombatDestination(&Enemy, Request, Destination, Corridors,
		{Enemy.GetCapsuleComponent()->GetComponentLocation(), Destination}, 5.0f);
}

void AdvanceReservationTime(FEnemyFlightHeightTestWorld& Fixture, float Seconds)
{
	// UWorld는 큰 한 번의 DeltaTime을 제한한다. 실제 작은 프레임을 누적해 TTL/양보 시계를 검증한다.
	const int32 Steps = FMath::CeilToInt(Seconds * 60.0f);
	for (int32 Step = 0; Step < Steps; ++Step)
	{
		++GFrameCounter;
		Fixture.World->Tick(LEVELTICK_TimeOnly, Seconds / Steps);
	}
}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyCombatReservationCrowdTest,
	"Outlier.Enemy.Combat.Reservation.Crowd", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyCombatReservationCrowdTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const int32 Count : {4, 8, 16})
	{
		Names.Add(FString::FromInt(Count));
		Commands.Add(FString::FromInt(Count));
	}
}

bool FEnemyCombatReservationCrowdTest::RunTest(const FString& Parameters)
{
	FEnemyFlightHeightTestWorld Fixture;
	FEnemyRunEnvQueryTaskInstanceData Query;
	if (!Fixture.Initialize(true, FVector(0, 0, 400), FVector(6000, 6000, 2000), true)
		|| !MakeHeightCombatQuery(Fixture, TEXT("Orbit"), Query))
	{
		AddError(TEXT("Gun combat fixture could not initialize"));
		return false;
	}
	UEnemyRoomSubsystem& Rooms = *Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
	Rooms.RegisterEnemy(Fixture.Enemy);
	TArray<AEnemyBase*> Enemies{Fixture.Enemy};
	for (int32 Index = 1; Index < FCString::Atoi(*Parameters); ++Index)
	{
		AEnemyBase* Enemy = SpawnReservationEnemy(Fixture, FVector(0, Index * 250, 400));
		if (!TestNotNull(TEXT("Competing Gun spawns"), Enemy))
		{
			return false;
		}
		Enemies.Add(Enemy);
	}
	TArray<uint64> Requests;
	const FVector Destination(2000, 0, 400);
	int32 Accepted = 0;
	for (AEnemyBase* Enemy : Enemies)
	{
		const uint64 Request = Enemy->BeginCombatReservationRequest();
		Requests.Add(Request);
		Accepted += ReserveTestDestination(Rooms, *Enemy, Request, Destination);
	}
	TestEqual(TEXT("One destination has exactly one owner"), Accepted, 1);
	// 기다리는 개체는 요청 순서를 유지한다. 뒤에서 매번 새 요청을 넣어도 앞선 대기를 추월하지 못한다.
	for (int32 Winner = 1; Winner < Enemies.Num(); ++Winner)
	{
		Rooms.ReleaseCombatReservation(Enemies[Winner - 1], Requests[Winner - 1]);
		for (int32 Later = Winner + 1; Later < Enemies.Num(); ++Later)
		{
			TestFalse(TEXT("Later contender yields to earlier live demand"),
				ReserveTestDestination(Rooms, *Enemies[Later], Requests[Later], Destination));
		}
		TestTrue(TEXT("Next waiting contender makes progress after release"),
			ReserveTestDestination(Rooms, *Enemies[Winner], Requests[Winner], Destination));
	}
	const UCapsuleComponent* Capsule = Fixture.Enemy->GetCapsuleComponent();
	AddInfo(FString::Printf(TEXT("Saved Gun capsule: Radius=%.1f HalfHeight=%.1f"),
		Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyCombatReservationOwnershipTest,
	"Outlier.Enemy.Combat.Reservation.OwnershipAndReplacement", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyCombatReservationOwnershipTest::RunTest(const FString& Parameters)
{
	FEnemyFlightHeightTestWorld Fixture;
	FEnemyRunEnvQueryTaskInstanceData Query;
	if (!Fixture.Initialize(true, FVector(0, 0, 400), FVector(6000, 6000, 2000), true)
		|| !MakeHeightCombatQuery(Fixture, TEXT("Orbit"), Query))
	{
		AddError(TEXT("Gun combat fixture could not initialize"));
		return false;
	}
	UEnemyRoomSubsystem& Rooms = *Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
	AEnemyBase& Enemy = *Fixture.Enemy;
	const uint64 Old = Enemy.BeginCombatReservationRequest();
	TestTrue(TEXT("Old request reserves"), ReserveTestDestination(Rooms, Enemy, Old, FVector(1000, 0, 400)));
	TestTrue(TEXT("Old movement adopts reservation"), Enemy.AdoptCombatReservation(Old));
	const uint64 Next = Enemy.BeginCombatReservationRequest();
	TestTrue(TEXT("Old movement retains reservation during new selection"), Rooms.IsCombatReservationCurrent(&Enemy, Old));
	TestFalse(TEXT("Late old success cannot replace reservation"), ReserveTestDestination(Rooms, Enemy, Old, FVector(1500, 0, 400)));
	TestTrue(TEXT("New request replaces atomically"), ReserveTestDestination(Rooms, Enemy, Next, FVector(1500, 0, 400)));
	Enemy.AdoptCombatReservation(Next);
	Enemy.ReleaseCombatReservation(Old);
	TestTrue(TEXT("Old callback cannot release new reservation"), Rooms.IsCombatReservationCurrent(&Enemy, Next));
	FEnemyFlyToLocationTaskInstanceData PreviousMove;
	PreviousMove.Enemy = &Enemy;
	PreviousMove.CachedController = Cast<AAIController>(Enemy.GetController());
	PreviousMove.bOwnsMovement = true;
	PreviousMove.CapturedCombatTacticalSerial = Enemy.GetCombatTacticalEpisodeSerial();
	PreviousMove.CapturedCombatReservationRequest = Old;
	Enemy.GetCharacterMovement()->Velocity = FVector(100, 0, 0);
	EnemyFlightTesting::Stop(PreviousMove);
	TestEqual(TEXT("Old Exit preserves new movement velocity"), Enemy.GetVelocity(), FVector(100, 0, 0));
	TestTrue(TEXT("Old Exit preserves new reservation"), Rooms.IsCombatReservationCurrent(&Enemy, Next));
	TestFalse(TEXT("Different route cannot borrow existing corridor permit"),
		Enemy.IsCombatSegmentReserved(Next, Enemy.GetActorLocation(), FVector(0, 1000, 400)));
	Enemy.EnterCombatObservationWait();
	TestFalse(TEXT("Observation returns future reservation"), Rooms.IsCombatReservationCurrent(&Enemy, Next));
	TestFalse(TEXT("Old request cannot reserve after observation"), ReserveTestDestination(Rooms, Enemy, Next, FVector(1500, 0, 400)));
	TestTrue(TEXT("Observation preserves actual capsule collision"), Enemy.GetCapsuleComponent()->IsQueryCollisionEnabled());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyCombatReservationCorridorTest,
	"Outlier.Enemy.Combat.Reservation.CorridorAndActualOccupancy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyCombatReservationCorridorTest::RunTest(const FString& Parameters)
{
	FEnemyFlightHeightTestWorld Fixture;
	FEnemyRunEnvQueryTaskInstanceData Query;
	if (!Fixture.Initialize(true, FVector(0, 0, 400), FVector(6000, 6000, 2000), true)
		|| !MakeHeightCombatQuery(Fixture, TEXT("Orbit"), Query))
	{
		AddError(TEXT("Gun combat fixture could not initialize"));
		return false;
	}
	AEnemyBase* Other = SpawnReservationEnemy(Fixture, FVector(0, 800, 400));
	if (!TestNotNull(TEXT("Other combatant spawns"), Other))
	{
		return false;
	}
	UEnemyRoomSubsystem& Rooms = *Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
	Rooms.RegisterEnemy(Fixture.Enemy);
	const uint64 First = Fixture.Enemy->BeginCombatReservationRequest();
	const uint64 Second = Other->BeginCombatReservationRequest();
	const FEnemyCombatCorridor Corridor{FVector(300, 0, 400), FVector(700, 0, 400)};
	TestTrue(TEXT("First entrant reserves narrow span"),
		ReserveTestDestination(Rooms, *Fixture.Enemy, First, FVector(1000, 0, 400), {Corridor}));
	TestFalse(TEXT("Opposing route with separate destination still yields at narrow span"),
		ReserveTestDestination(Rooms, *Other, Second, FVector(1000, 800, 400), {Corridor}));
	Rooms.ReleaseCombatReservation(Fixture.Enemy, First);
	Fixture.Enemy->SetActorLocation(FVector(500, 0, 400));
	TestFalse(TEXT("Returning reservation does not pretend occupied passage is empty"),
		ReserveTestDestination(Rooms, *Other, Second, FVector(1000, 800, 400), {Corridor}));
	Fixture.Enemy->SetActorLocation(FVector(0, 0, 400));
	TestTrue(TEXT("Empty passage becomes available"),
		ReserveTestDestination(Rooms, *Other, Second, FVector(1000, 800, 400), {Corridor}));
	Rooms.ReleaseCombatReservation(Other, Second);
	const uint64 WideA = Fixture.Enemy->BeginCombatReservationRequest();
	const uint64 WideB = Other->BeginCombatReservationRequest();
	TestTrue(TEXT("Wide route A reserves destination"), ReserveTestDestination(Rooms, *Fixture.Enemy, WideA, FVector(1000, 0, 400)));
	TestTrue(TEXT("Wide route B does not reserve entire room"), ReserveTestDestination(Rooms, *Other, WideB, FVector(1000, 800, 400)));
	Other->SetActorLocation(FVector(500, 800, 400));
	TestTrue(TEXT("Actual forward progress renews permit"), Rooms.RefreshCombatReservation(Other, WideB));
	TestFalse(TEXT("Returned route behind cannot authorize reverse reentry"),
		Rooms.IsCombatSegmentReserved(Other, WideB, Other->GetActorLocation(), FVector(100, 800, 400)));
	AdvanceReservationTime(Fixture, 0.75f);
	TestTrue(TEXT("Stationary reservation is valid before TTL"), Rooms.RefreshCombatReservation(Other, WideB));
	AdvanceReservationTime(Fixture, 0.30f);
	TestFalse(TEXT("Stationary refresh cannot extend TTL indefinitely"), Rooms.RefreshCombatReservation(Other, WideB));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyCombatInsidePriorityTest,
	"Outlier.Enemy.Combat.Reservation.InsideOccupantPriority", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyCombatInsidePriorityTest::RunTest(const FString& Parameters)
{
	FEnemyFlightHeightTestWorld Fixture;
	FEnemyRunEnvQueryTaskInstanceData Query;
	if (!Fixture.Initialize(true, FVector(0, 0, 400), FVector(6000, 6000, 2000), true)
		|| !MakeHeightCombatQuery(Fixture, TEXT("Orbit"), Query))
	{
		AddError(TEXT("Gun combat fixture could not initialize"));
		return false;
	}
	AEnemyBase* Inside = SpawnReservationEnemy(Fixture, FVector(0, 800, 400));
	if (!TestNotNull(TEXT("Inside combatant spawns"), Inside))
	{
		return false;
	}
	UEnemyRoomSubsystem& Rooms = *Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
	Rooms.RegisterEnemy(Fixture.Enemy);
	const uint64 OutsideRequest = Fixture.Enemy->BeginCombatReservationRequest();
	const uint64 InsideRequest = Inside->BeginCombatReservationRequest();
	const FEnemyCombatCorridor Corridor{FVector(300, 0, 400), FVector(700, 0, 400)};
	TestTrue(TEXT("Outside combatant reserves future passage"),
		ReserveTestDestination(Rooms, *Fixture.Enemy, OutsideRequest, FVector(1000, 0, 400), {Corridor}));
	TestFalse(TEXT("Later outside contender waits"),
		ReserveTestDestination(Rooms, *Inside, InsideRequest, FVector(1000, 800, 400), {Corridor}));
	// 스폰/수명주기 전환으로 이미 통로 안에 있다면, 밖의 미래 허가보다 실제 이탈을 우선한다.
	Inside->SetActorLocation(FVector(500, 0, 400));
	TestTrue(TEXT("Inside occupant can acquire exit before outside future owner"),
		ReserveTestDestination(Rooms, *Inside, InsideRequest, FVector(1000, 800, 400), {Corridor}));
	TestFalse(TEXT("Outside future permit is returned atomically"), Rooms.IsCombatReservationCurrent(Fixture.Enemy, OutsideRequest));
	Rooms.ReleaseCombatReservation(Fixture.Enemy, OutsideRequest);
	TestTrue(TEXT("Delayed outside release preserves inside permit"), Rooms.IsCombatReservationCurrent(Inside, InsideRequest));
	const uint64 Retry = Fixture.Enemy->BeginCombatReservationRequest();
	TestFalse(TEXT("Outside retry cannot preempt actual occupant"),
		ReserveTestDestination(Rooms, *Fixture.Enemy, Retry, FVector(1000, 0, 400), {Corridor}));
	Rooms.ReleaseCombatReservation(Inside, InsideRequest);
	TestFalse(TEXT("Actual occupied passage remains protected after permit release"),
		ReserveTestDestination(Rooms, *Fixture.Enemy, Retry, FVector(1000, 0, 400), {Corridor}));
	Inside->SetActorLocation(FVector(0, 800, 400));
	TestTrue(TEXT("Outside contender proceeds after actual exit"),
		ReserveTestDestination(Rooms, *Fixture.Enemy, Retry, FVector(1000, 0, 400), {Corridor}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyCombatPassingSpaceTest,
	"Outlier.Enemy.Combat.Reservation.PassingSpaceUsesSavedCapsule", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyCombatPassingSpaceTest::RunTest(const FString& Parameters)
{
	FEnemyFlightHeightTestWorld Fixture;
	if (!Fixture.Initialize(true, FVector(0, 0, 400), FVector(6000, 6000, 2000), true))
	{
		AddError(TEXT("Gun collision fixture could not initialize"));
		return false;
	}
	UCapsuleComponent& Capsule = *Fixture.Enemy->GetCapsuleComponent();
	FFlightPathRequest Request;
	Request.Start = Capsule.GetComponentLocation();
	Request.Goal = Request.Start + FVector(500, 0, 0);
	Request.SearchBounds = FBox(Request.Start, Request.Goal).ExpandBy(1000);
	Request.MaxCollisionChecks = 32;
	Request.MaxSearchMilliseconds = 1000;
	TestEqual(TEXT("Wide space allows passing"), FlightPathfinding::CheckPassingSpace(*Fixture.World, Capsule, Request).Status,
		EFlightPathStatus::Success);
	const FVector HighStart = Request.Start;
	Request.Start.Z = Capsule.GetScaledCapsuleHalfHeight() + Request.Clearance + 1.0f;
	Request.Goal.Z = Request.Start.Z;
	TestEqual(TEXT("Low flight over open floor does not monopolize a wide room"),
		FlightPathfinding::CheckPassingSpace(*Fixture.World, Capsule, Request).Status, EFlightPathStatus::Success);
	Request.Start = HighStart;
	Request.Goal = HighStart + FVector(500, 0, 0);
	const double WallOffset = (Capsule.GetScaledCapsuleRadius() + Request.Clearance) * 1.5 + 10.0;
	Fixture.SpawnBlockingBox(Request.Start + FVector(250, WallOffset, 0), FVector(400, 10, 200));
	Fixture.SpawnBlockingBox(Request.Start + FVector(250, -WallOffset, 0), FVector(400, 10, 200));
	TestEqual(TEXT("One actual capsule still passes narrow opening"),
		FlightPathfinding::CheckSegment(*Fixture.World, Capsule, Request), EFlightPathStatus::Success);
	const FFlightPathResult Passing = FlightPathfinding::CheckPassingSpace(*Fixture.World, Capsule, Request);
	TestTrue(TEXT("Passing classifier identifies exclusive opening without rejecting actual travel"),
		Passing.Status == EFlightPathStatus::BlockedEndpoint || Passing.Status == EFlightPathStatus::BlockedSegment);
	TestTrue(TEXT("Classification returns bounded collision cost for scheduler accounting"),
		Passing.CollisionChecks > 0 && Passing.CollisionChecks <= 4);
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyCombatReservationSelectionTest,
	"Outlier.Enemy.Combat.Reservation.LiveSelection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyCombatReservationSelectionTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("FiniteYield"), TEXT("ReleaseWhileWaiting"), TEXT("LateCompletion")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyCombatReservationSelectionTest::RunTest(const FString& Parameters)
{
	FEnemyFlightHeightTestWorld Fixture;
	FEnemyRunEnvQueryTaskInstanceData Query;
	if (!Fixture.Initialize(true, FVector(0, 0, 400), FVector(6000, 6000, 2000), true)
		|| !MakeHeightCombatQuery(Fixture, TEXT("Orbit"), Query))
	{
		AddError(TEXT("Gun combat fixture could not initialize"));
		return false;
	}
	if (!TestTrue(TEXT("Actual Gun query owns a tactical episode"), Query.CapturedCombatSerial != 0))
	{
		return false;
	}
	Query.MaxPathCandidates = 1;
	AEnemyBase* Other = SpawnReservationEnemy(Fixture, FVector(-500, 1500, 400));
	if (!TestNotNull(TEXT("Other combatant spawns"), Other))
	{
		return false;
	}
	UEnemyRoomSubsystem& Rooms = *Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
	FEnvQueryRequest Request(Query.QueryTemplate, Fixture.Enemy);
	if (!TestTrue(TEXT("Gun request prepares"), EnemySearchTesting::PrepareRequest(Query, Request)))
	{
		return false;
	}
	TSharedPtr<FEnvQueryResult> Result;
	uint64 BlockingRequest = 0;
	uint64 ReplacementRequest = 0;
	const FVector Candidate = Query.HeightSnapshot.Candidates[0].Location;
	if (!FinishHeightQuery(*this, Fixture, Query, Request, Result, [&]
	{
		if (Parameters == TEXT("LateCompletion"))
		{
			ReplacementRequest = Fixture.Enemy->BeginCombatReservationRequest();
			TestTrue(TEXT("New same-lease request acquires destination before old completion"),
				ReserveTestDestination(Rooms, *Fixture.Enemy, ReplacementRequest, FVector(1000, 800, 400)));
			Fixture.Enemy->AdoptCombatReservation(ReplacementRequest);
		}
		else
		{
			BlockingRequest = Other->BeginCombatReservationRequest();
			TestTrue(TEXT("Another valid owner acquires first candidate"), ReserveTestDestination(Rooms, *Other, BlockingRequest, Candidate));
		}
	}))
	{
		return false;
	}
	if (Parameters == TEXT("LateCompletion"))
	{
		TestFalse(TEXT("Old real EQS completion cannot publish destination"), Query.bHasDestination);
		TestTrue(TEXT("Old real completion preserves newer reservation"), Rooms.IsCombatReservationCurrent(Fixture.Enemy, ReplacementRequest));
		return true;
	}
	for (int32 Step = 0; Step < 512 && Query.CombatYieldStartedAt < 0.0
		&& Query.QueryResult == EEnemyEnvQueryResult::Running; ++Step)
	{
		++GFrameCounter;
		EnemySearchTesting::ResumeCombatSelection(Query);
	}
	if (!TestTrue(TEXT("Real path selection enters bounded contention wait"), Query.CombatYieldStartedAt >= 0.0))
	{
		AddInfo(FString::Printf(TEXT("Selection result=%s Path=%s"), *UEnum::GetValueAsString(Query.QueryResult),
			*UEnum::GetValueAsString(Query.SelectionPathStatus)));
		return false;
	}
	TestFalse(TEXT("Crowding is not cached as physical Orbit failure"), Fixture.Enemy->IsCombatTacticalCandidateFailure(TEXT("Orbit"), Candidate));
	if (Parameters == TEXT("ReleaseWhileWaiting"))
	{
		Rooms.ReleaseCombatReservation(Other, BlockingRequest);
		AdvanceReservationTime(Fixture, 0.55f);
		TestEqual(TEXT("Waiting selection succeeds after competing owner releases"),
			EnemySearchTesting::ResumeCombatSelection(Query), EStateTreeRunStatus::Succeeded);
		TestTrue(TEXT("Resumed candidate owns reservation before publication"),
			Rooms.IsCombatReservationCurrent(Fixture.Enemy, Query.CombatReservationRequest));
		FEnemyFlyToLocationTaskInstanceData Fly = MakeHeightFlightData(Query);
		Fly.CapturedCombatTacticalSerial = Query.CapturedCombatSerial;
		ON_SCOPE_EXIT { EnemyFlightTesting::Stop(Fly); };
		if (!TestEqual(TEXT("Reserved real Orbit enters movement"), EnemyFlightTesting::StartPath(Fly), EStateTreeRunStatus::Running))
		{
			return false;
		}
		TestEqual(TEXT("Fly captures exact selection owner"), Fly.CapturedCombatReservationRequest, Query.CombatReservationRequest);
		const TArray<FVector> Route = Fly.PathWaypoints;
		EStateTreeRunStatus MoveStatus = EStateTreeRunStatus::Running;
		for (int32 Index = 1; Index < Route.Num() && MoveStatus == EStateTreeRunStatus::Running; ++Index)
		{
			Fixture.Enemy->SetActorLocation(Route[Index]);
			MoveStatus = EnemyFlightTesting::TickPath(Fly, 1.0f / 60.0f);
		}
		TestEqual(TEXT("Reserved movement consumes route and arrives"), MoveStatus, EStateTreeRunStatus::Succeeded);
		TestFalse(TEXT("Arrival returns future reservation"), Rooms.IsCombatReservationCurrent(Fixture.Enemy, Query.CombatReservationRequest));
	}
	else
	{
		AdvanceReservationTime(Fixture, 1.05f);
		TestEqual(TEXT("Unchanged contention ends within one second"),
			EnemySearchTesting::ResumeCombatSelection(Query), EStateTreeRunStatus::Failed);
		TestEqual(TEXT("Yield expiry stays distinct from physical and budget failure"), Query.QueryResult, EEnemyEnvQueryResult::ReservationConflict);
		TestFalse(TEXT("Contention cannot claim all Orbit routes physically blocked"), Query.bNoOrbitDestination);
	}
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyCombatReservationLifetimeTest,
	"Outlier.Enemy.Combat.Reservation.Lifetime", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyCombatReservationLifetimeTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("Stun"), TEXT("Possession"), TEXT("Death"), TEXT("ControllerChanged"), TEXT("PoolReuse"), TEXT("ArenaReset")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyCombatReservationLifetimeTest::RunTest(const FString& Parameters)
{
	FEnemyFlightHeightTestWorld Fixture;
	FEnemyRunEnvQueryTaskInstanceData Query;
	if (!Fixture.Initialize(true, FVector(0, 0, 400), FVector(6000, 6000, 2000), true)
		|| !MakeHeightCombatQuery(Fixture, TEXT("Orbit"), Query))
	{
		AddError(TEXT("Gun combat fixture could not initialize"));
		return false;
	}
	UEnemyRoomSubsystem& Rooms = *Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
	AEnemyBase& Enemy = *Fixture.Enemy;
	const uint64 Request = Enemy.BeginCombatReservationRequest();
	const FVector Destination(1000, 0, 400);
	TestTrue(TEXT("Old lifetime acquires reservation"), ReserveTestDestination(Rooms, Enemy, Request, Destination));
	Enemy.AdoptCombatReservation(Request);
	if (Parameters == TEXT("Stun"))
	{
		Enemy.EnterStun();
	}
	else if (Parameters == TEXT("Possession"))
	{
		Enemy.SetEnemyPossessed(true);
	}
	else if (Parameters == TEXT("Death"))
	{
		Enemy.BeginDeathForPoolTesting();
	}
	else if (Parameters == TEXT("ControllerChanged"))
	{
		AEnemyAIController* NewController = Fixture.World->SpawnActor<AEnemyAIController>();
		NewController->Possess(&Enemy);
	}
	else if (Parameters == TEXT("PoolReuse"))
	{
		Enemy.PrepareForPoolSpawn(Fixture.World->GetSubsystem<UEnemyPoolSubsystem>());
		FEnemyPoolLeaseContext Lease;
		Lease.RoomTag = Fixture.RoomTag;
		Lease.GameplayGeneration = Query.CapturedGeneration + 1;
		TestTrue(TEXT("Actual new pool lifetime begins"), Enemy.BeginPoolLease(Lease, FTransform(Enemy.GetActorLocation()), Query.CapturedLease + 1));
	}
	else
	{
		Rooms.ResetRuntimeCombatState();
	}
	TestFalse(TEXT("Lifetime interruption invalidates reservation"), Rooms.IsCombatReservationCurrent(&Enemy, Request));
	TestFalse(TEXT("Late old success cannot reacquire after interruption"), ReserveTestDestination(Rooms, Enemy, Request, Destination));
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyCombatTargetSelectionTest,
	"Outlier.Enemy.Combat.TargetSelection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyCombatTargetSelectionTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("MinimumHold"), TEXT("DistanceGain"), TEXT("PeriodicBoundary"),
		TEXT("SameTargetContinuity"), TEXT("RetargetHandoff"), TEXT("CumulativeGoalMovement"), TEXT("BurstProtection"), TEXT("BurstInvalidTarget"),
		TEXT("HigherPriority"), TEXT("LowerPriority"), TEXT("VisibleOverHidden"),
		TEXT("VisibleOverShared"), TEXT("VisibleDuringBurst"), TEXT("VisibleDuringNoise"), TEXT("VisibleRecoveryHandoff")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyCombatTargetSelectionTest::RunTest(const FString& Parameters)
{
	FEnemyFlightHeightTestWorld Fixture;
	FEnemyRunEnvQueryTaskInstanceData Query;
	if (!Fixture.Initialize(true, FVector(0, 0, 400), FVector(6000, 6000, 2000), true)
		|| !MakeHeightCombatQuery(Fixture, TEXT("Orbit"), Query))
	{
		AddError(TEXT("Saved Gun target-selection fixture could not initialize"));
		return false;
	}
	AEnemyBase& Enemy = *Fixture.Enemy;
	AEnemyAIController& Controller = *CastChecked<AEnemyAIController>(Enemy.GetController());
	UEnemyRoomSubsystem& Rooms = *Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
	// 실제 감지와 StateTree의 자율 진행만 멈추고, 채택·회복·예약은 생산 코드로 검사한다.
	Controller.SetEnemyPerceptionEnabled(false);
	Controller.SetActorTickEnabled(false);
	Controller.SetCachedVisibleTargetForTesting(Query.CapturedTarget.Get());
	Enemy.ObserveTrackedTarget(Query.CapturedTarget.Get());
	Enemy.SetPlayerCurrentlyVisible(true);
	Enemy.RefreshTargetTracking();
	Enemy.GetStateTreeComponent()->StopLogic(TEXT("Controlled Slice 7 target-selection test"));
	AActor* Original = Enemy.GetTrackedTarget();
	if (!TestNotNull(TEXT("Initial directly visible target is adopted"), Original))
	{
		return false;
	}
	Original->SetActorLocation(FVector(1500, 0, 400));
	Enemy.RefreshTargetTrackingAtSelectionBoundary();
	FEnemyStat& Stat = const_cast<FEnemyStat&>(Enemy.GetRuntimeStat());
	Stat.TargetReevaluationInterval = 0.5f;
	Stat.TargetMinimumHoldSeconds = 1.0f;
	Stat.TargetSwitchDistanceGain = 200.0f;
	const uint64 InitialEpisode = Enemy.EnsureCombatTacticalEpisode(Original, Original->GetActorLocation());
	if (!TestTrue(TEXT("The Gun owns a tactical episode"), InitialEpisode != 0))
	{
		return false;
	}
	const FVector Destination(1000, 500, 400);
	const FVector FailedCandidate(1200, -500, 400);
	if (Parameters.StartsWith(TEXT("Visible")))
	{
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		APartnerCharacter* Partner = Fixture.World->SpawnActor<APartnerCharacter>(APartnerCharacter::StaticClass(),
			FVector(600, 500, 400), FRotator::ZeroRotator, Spawn);
		if (!TestNotNull(TEXT("A directly visible Partner spawns"), Partner))
		{
			return false;
		}
		Partner->SetActorEnableCollision(false);
		if (Parameters == TEXT("VisibleDuringBurst"))
		{
			Enemy.SetActorEnableCollision(false);
			Enemy.RememberTargetAttackPreparation();
			ObserveShooterBeforeAttack(Enemy, Original);
			Controller.SetControlRotation((Enemy.GetCombatAimPoint(Original) - Enemy.GetCombatShotOrigin()).Rotation());
			if (!TestTrue(TEXT("The hidden-target case starts from an actual active burst"), Enemy.StartAttackTarget(Original)))
			{
				return false;
			}
		}
		if (Parameters == TEXT("VisibleRecoveryHandoff"))
		{
			Enemy.RememberTargetAttackPreparation();
			Enemy.RememberTargetAttackRecovery(2.0f);
			const uint64 Reservation = Enemy.BeginCombatReservationRequest();
			if (!TestTrue(TEXT("The old target reserves a real destination"), ReserveTestDestination(Rooms, Enemy, Reservation, Destination)))
			{
				return false;
			}
			Enemy.AdoptCombatReservation(Reservation);
		}
		if (Parameters == TEXT("VisibleOverShared"))
		{
			Rooms.SetActiveRoomTargetForTesting(Fixture.RoomTag, Original->GetActorLocation(), false);
			Rooms.ReportRoomTargetContact(&Enemy, Original, Original->GetActorLocation());
			FEnemyRoomTargetObservationSnapshot Shared;
			if (!TestTrue(TEXT("Higher-priority hidden Shooter has a live room observation"),
				Rooms.TryGetRoomTargetObservation(&Enemy, Original, Shared)))
			{
				return false;
			}
		}
		if (Parameters == TEXT("VisibleDuringNoise"))
		{
			Controller.SetVisibleTargetsForTesting({});
			Enemy.SetPlayerCurrentlyVisible(false);
			AdvanceReservationTime(Fixture, 0.05f);
			if (!TestTrue(TEXT("A new hidden-Shooter sound is recorded"), Enemy.RecordHeardNoise(
				Original, FVector(200, -500, 400), Fixture.World->GetTimeSeconds())))
			{
				return false;
			}
			FVector NoiseDestination;
			if (!TestTrue(TEXT("The sound owns an actual investigation"), Enemy.BeginNoiseInvestigation(NoiseDestination)))
			{
				return false;
			}
		}
		// 실제 시야 집합에서 Shooter를 빼고 Partner만 남긴다. 종류 순위/유예/버스트 경계를 기다리지 않는다.
		Controller.SetVisibleTargetsForTesting({Partner});
		Enemy.SetPlayerCurrentlyVisible(true);
		Enemy.RefreshTargetTracking();
		TestEqual(TEXT("Visible Partner replaces hidden Shooter on the ordinary perception evaluation"),
			Enemy.GetTrackedTarget(), static_cast<AActor*>(Partner));
		TestEqual(TEXT("Combat intent uses the same adopted target"), Enemy.GetPerceptionGoal().Target.Get(), static_cast<AActor*>(Partner));
		FEnemyStateTreeSyncTask Sync;
		FEnemyStateTreeSyncTaskInstanceData Synced;
		Synced.Enemy = &Enemy;
		Sync.SyncFromEnemy(Synced);
		TestEqual(TEXT("StateTree attack and movement target agree"), Synced.TargetActor.Get(), static_cast<AActor*>(Partner));
		TestEqual(TEXT("StateTree perception target agrees"), Synced.PerceptionGoalTarget.Get(), static_cast<AActor*>(Partner));
		TestTrue(TEXT("StateTree visibility describes the selected Partner"), Synced.bPlayerCurrentlyVisible);
		TestFalse(TEXT("Old Shooter cannot authorize another shot"), Enemy.CanAttackTrackedTarget(Original));
		TestFalse(TEXT("The old weapon attack cannot continue after replacement"), Enemy.GetCurrentWeapon()->IsAttacking());
		TestFalse(TEXT("Visible handoff cancels the old noise investigation"), Enemy.IsLocationInvestigationActive());
		TestFalse(TEXT("The old tactical episode cannot apply delayed results"), Enemy.IsCombatTacticalEpisodeCurrent(InitialEpisode));
		if (Parameters == TEXT("VisibleRecoveryHandoff"))
		{
			TestFalse(TEXT("Partner needs its own preparation"), Enemy.IsTargetAttackPreparationComplete());
			TestTrue(TEXT("The original recovery deadline is preserved"), FMath::IsNearlyEqual(Enemy.GetTargetAttackRecoveryRemaining(), 2.0f));
		}
		return true;
	}
	if (Parameters == TEXT("HigherPriority") || Parameters == TEXT("LowerPriority"))
	{
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		APartnerCharacter* Partner = Fixture.World->SpawnActor<APartnerCharacter>(APartnerCharacter::StaticClass(),
			FVector(300, 0, 400), FRotator::ZeroRotator, Spawn);
		if (!TestNotNull(TEXT("A lower-priority Partner candidate spawns"), Partner))
		{
			return false;
		}
		Partner->SetActorEnableCollision(false);
		if (Parameters == TEXT("HigherPriority"))
		{
			Controller.SetVisibleTargetsForTesting({Partner});
			Enemy.ObserveTrackedTarget(Partner);
			Enemy.RefreshTargetTrackingAtSelectionBoundary();
			TestEqual(TEXT("Partner is initially the current target"), Enemy.GetTrackedTarget(), static_cast<AActor*>(Partner));
			Controller.SetVisibleTargetsForTesting({Partner, Original});
			Enemy.RefreshTargetTrackingAtSelectionBoundary();
			TestEqual(TEXT("Higher-priority Shooter bypasses both hold and distance gain at a safe boundary"), Enemy.GetTrackedTarget(), Original);
		}
		else
		{
			AdvanceReservationTime(Fixture, 1.01f);
			Controller.SetVisibleTargetsForTesting({Original, Partner});
			Enemy.RefreshTargetTrackingAtSelectionBoundary();
			TestEqual(TEXT("A much nearer Partner cannot downgrade the current Shooter priority"), Enemy.GetTrackedTarget(), Original);
		}
		return true;
	}
	if (Parameters == TEXT("CumulativeGoalMovement"))
	{
		Enemy.EnterCombatObservationWait();
		const uint64 ObservingEpisode = Enemy.GetCombatTacticalEpisodeSerial();
		TestTrue(TEXT("Failure exhaustion enters observation"), Enemy.IsCombatObservationWaiting());
		Original->AddActorWorldOffset(FVector(100, 0, 0));
		Enemy.RefreshTargetTrackingAtSelectionBoundary();
		TestEqual(TEXT("Small accepted-target jitter preserves the episode"), Enemy.GetCombatTacticalEpisodeSerial(), ObservingEpisode);
		TestTrue(TEXT("Small jitter cannot refill an exhausted search budget"), Enemy.IsCombatObservationWaiting());
		Original->AddActorWorldOffset(FVector(101, 0, 0));
		Enemy.RefreshTargetTrackingAtSelectionBoundary();
		TestNotEqual(TEXT("Cumulative goal displacement over 200cm starts a new episode"), Enemy.GetCombatTacticalEpisodeSerial(), ObservingEpisode);
		TestFalse(TEXT("A meaningfully changed goal receives a fresh finite budget"), Enemy.IsCombatObservationWaiting());
		return true;
	}
	if (Parameters == TEXT("SameTargetContinuity"))
	{
		Enemy.RememberTargetAttackPreparation();
		Enemy.RememberTargetAttackRecovery(1.0f);
		Enemy.RecordCombatTacticalCandidateFailure(TEXT("Orbit"), FailedCandidate, FBox(FailedCandidate - FVector(50), FailedCandidate + FVector(50)));
		const uint64 Reservation = Enemy.BeginCombatReservationRequest();
		if (!TestTrue(TEXT("Current episode reserves a destination"), ReserveTestDestination(Rooms, Enemy, Reservation, Destination)))
		{
			return false;
		}
		Enemy.AdoptCombatReservation(Reservation);
		for (int32 Index = 0; Index < 3; ++Index)
		{
			Enemy.RefreshTargetTrackingAtSelectionBoundary();
		}
		TestEqual(TEXT("Repeated same-target selection keeps the episode"), Enemy.GetCombatTacticalEpisodeSerial(), InitialEpisode);
		TestTrue(TEXT("Repeated same-target selection keeps the preparation"), Enemy.IsTargetAttackPreparationComplete());
		TestTrue(TEXT("Repeated same-target selection keeps the recovery deadline"), FMath::IsNearlyEqual(Enemy.GetTargetAttackRecoveryRemaining(), 1.0f));
		TestTrue(TEXT("Repeated same-target selection keeps future movement ownership"), Rooms.IsCombatReservationCurrent(&Enemy, Reservation));
		TestTrue(TEXT("Repeated same-target selection keeps physical failure evidence"), Enemy.IsCombatTacticalCandidateFailure(TEXT("Orbit"), FailedCandidate));
		return true;
	}
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Candidate = Fixture.World->SpawnActor<AActor>(Original->GetClass(), FVector(600, 0, 400), FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("A second live player candidate spawns"), Candidate))
	{
		return false;
	}
	Candidate->SetActorEnableCollision(false);
	if (Parameters == TEXT("MinimumHold"))
	{
		Controller.SetVisibleTargetsForTesting({Original, Candidate});
		Enemy.RefreshTargetTrackingAtSelectionBoundary();
		TestEqual(TEXT("A much nearer equal-priority candidate cannot bypass the initial hold"), Enemy.GetTrackedTarget(), Original);
		AdvanceReservationTime(Fixture, 0.99f);
		Enemy.RefreshTargetTrackingAtSelectionBoundary();
		TestEqual(TEXT("Selection boundaries do not shorten the one-second hold"), Enemy.GetTrackedTarget(), Original);
		AdvanceReservationTime(Fixture, 0.02f);
		Enemy.RefreshTargetTrackingAtSelectionBoundary();
		TestEqual(TEXT("A nearer candidate is adopted after the original hold deadline"), Enemy.GetTrackedTarget(), Candidate);
		return true;
	}
	AdvanceReservationTime(Fixture, 1.01f);
	if (Parameters == TEXT("DistanceGain"))
	{
		Candidate->SetActorLocation(FVector(1301, 0, 400));
		Controller.SetVisibleTargetsForTesting({Original, Candidate});
		Enemy.RefreshTargetTrackingAtSelectionBoundary();
		TestEqual(TEXT("199cm distance gain cannot replace an equal-priority target"), Enemy.GetTrackedTarget(), Original);
		Candidate->SetActorLocation(FVector(1300, 0, 400));
		Enemy.RefreshTargetTrackingAtSelectionBoundary();
		TestEqual(TEXT("The approved 200cm gain admits an equal-priority replacement"), Enemy.GetTrackedTarget(), Candidate);
		return true;
	}
	if (Parameters == TEXT("PeriodicBoundary"))
	{
		Enemy.RefreshTargetTrackingAtSelectionBoundary();
		Controller.SetVisibleTargetsForTesting({Original, Candidate});
		AdvanceReservationTime(Fixture, 0.49f);
		Enemy.RefreshTargetTracking();
		TestEqual(TEXT("Ordinary controller refresh respects the reevaluation interval"), Enemy.GetTrackedTarget(), Original);
		AdvanceReservationTime(Fixture, 0.02f);
		Enemy.RefreshTargetTracking();
		TestEqual(TEXT("A non-firing Gun adopts the better target within half a second"), Enemy.GetTrackedTarget(), Candidate);
		return true;
	}
	if (Parameters == TEXT("BurstProtection") || Parameters == TEXT("BurstInvalidTarget"))
	{
		Enemy.SetActorEnableCollision(false);
		Enemy.RememberTargetAttackPreparation();
		ObserveShooterBeforeAttack(Enemy, Original);
		Controller.SetControlRotation((Enemy.GetCombatAimPoint(Original) - Enemy.GetCombatShotOrigin()).Rotation());
		FBoolProperty* InfiniteAmmo = FindFProperty<FBoolProperty>(Enemy.GetCurrentWeapon()->GetClass(), TEXT("bInfiniteAmmo"));
		if (!TestNotNull(TEXT("Actual weapon ammunition policy can be inspected"), InfiniteAmmo))
		{
			return false;
		}
		InfiniteAmmo->SetPropertyValue_InContainer(Enemy.GetCurrentWeapon(), false);
		const int32 Ammo = Enemy.GetCurrentWeapon()->GetCurrentAmmo();
		if (!TestTrue(TEXT("Prepared Gun begins a real normal burst"), Enemy.StartAttackTarget(Original)))
		{
			return false;
		}
		TestTrue(TEXT("Burst protection begins with real ammunition consumption"), Enemy.GetCurrentWeapon()->GetCurrentAmmo() < Ammo);
		Controller.SetVisibleTargetsForTesting({Original, Candidate});
		if (Parameters == TEXT("BurstInvalidTarget"))
		{
			// 유효한 공격만 보호한다. 대상 소멸은 남은 버스트 시간과 무관하게 기존 중단 규칙을 따른다.
			Original->Destroy();
			Enemy.RefreshTargetTrackingAtSelectionBoundary();
			TestEqual(TEXT("An invalid burst target is replaced immediately without waiting for the burst boundary"), Enemy.GetTrackedTarget(), Candidate);
			TestFalse(TEXT("The invalid target cannot retain the old attack"), Enemy.GetCurrentWeapon()->IsAttacking());
			return true;
		}
		Enemy.RefreshTargetTrackingAtSelectionBoundary();
		TestEqual(TEXT("A better candidate cannot interrupt the active burst"), Enemy.GetTrackedTarget(), Original);
		Enemy.PauseTargetAttack();
		AdvanceReservationTime(Fixture, 0.49f);
		Enemy.RefreshTargetTrackingAtSelectionBoundary();
		TestEqual(TEXT("A brief firing hold preserves the existing burst target"), Enemy.GetTrackedTarget(), Original);
		AdvanceReservationTime(Fixture, 0.02f);
		Enemy.RefreshTargetTrackingAtSelectionBoundary();
		TestEqual(TEXT("A blocked burst cannot postpone target selection indefinitely"), Enemy.GetTrackedTarget(), Candidate);
		return true;
	}
	Enemy.RememberTargetAttackPreparation();
	Enemy.RememberTargetAttackRecovery(2.0f);
	Enemy.RecordCombatTacticalCandidateFailure(TEXT("Orbit"), FailedCandidate, FBox(FailedCandidate - FVector(50), FailedCandidate + FVector(50)));
	const uint64 Reservation = Enemy.BeginCombatReservationRequest();
	if (!TestTrue(TEXT("Old target has an actual reserved route"), ReserveTestDestination(Rooms, Enemy, Reservation, Destination)))
	{
		return false;
	}
	Enemy.AdoptCombatReservation(Reservation);
	Controller.SetVisibleTargetsForTesting({Original, Candidate});
	Enemy.RefreshTargetTrackingAtSelectionBoundary();
	TestEqual(TEXT("The safe boundary adopts the new target"), Enemy.GetTrackedTarget(), Candidate);
	TestFalse(TEXT("Target replacement returns the previous route reservation"), Rooms.IsCombatReservationCurrent(&Enemy, Reservation));
	TestFalse(TEXT("Target replacement invalidates old asynchronous tactical ownership"), Enemy.IsCombatTacticalEpisodeCurrent(InitialEpisode));
	TestFalse(TEXT("New target requires its own first preparation"), Enemy.IsTargetAttackPreparationComplete());
	TestTrue(TEXT("Target replacement preserves the original common recovery deadline"), FMath::IsNearlyEqual(Enemy.GetTargetAttackRecoveryRemaining(), 2.0f));
	TestTrue(TEXT("New target can begin a fresh finite tactical budget"), Enemy.TryBeginCombatTacticalRequest());
	// B의 탐색 결과를 A로 재사용하지 않되, A로 돌아왔을 때 아직 유효한 물리 실패는 되살린다.
	AdvanceReservationTime(Fixture, 1.01f);
	Original->SetActorLocation(FVector(300, 0, 400));
	Controller.SetCachedVisibleTargetForTesting(Original);
	Enemy.RefreshTargetTrackingAtSelectionBoundary();
	TestEqual(TEXT("A subsequent safe boundary can return to the original target"), Enemy.GetTrackedTarget(), Original);
	Original->SetActorLocation(FVector(1500, 0, 400));
	Enemy.EnsureCombatTacticalEpisode(Original, Original->GetActorLocation());
	TestTrue(TEXT("A-B-A cannot erase still-valid physical failure evidence for A"), Enemy.IsCombatTacticalCandidateFailure(TEXT("Orbit"), FailedCandidate));
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyVisibleTurretTargetTest,
	"Outlier.Enemy.Combat.TargetSelection.Turret", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyVisibleTurretTargetTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("NormalRecovery"), TEXT("CommittedLaser")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyVisibleTurretTargetTest::RunTest(const FString& Parameters)
{
	FEnemyFlightHeightTestWorld Fixture;
	FEnemyRunEnvQueryTaskInstanceData Query;
	if (!Fixture.Initialize(true, FVector(0, 0, 400), FVector(6000, 6000, 2000), true)
		|| !MakeHeightCombatQuery(Fixture, TEXT("Orbit"), Query))
	{
		AddError(TEXT("Turret target-selection fixture could not initialize"));
		return false;
	}
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAutoTurret* Turret = Fixture.World->SpawnActor<AAutoTurret>(AAutoTurret::StaticClass(),
		FVector(0, 1000, 400), FRotator::ZeroRotator, Spawn);
	APartnerCharacter* Partner = Fixture.World->SpawnActor<APartnerCharacter>();
	AEnemyAIController* AI = Fixture.World->SpawnActor<AEnemyAIController>();
	if (!TestNotNull(TEXT("Turret spawns"), Turret) || !TestNotNull(TEXT("Partner spawns"), Partner)
		|| !TestNotNull(TEXT("Turret controller spawns"), AI))
	{
		return false;
	}
	Turret->GetOutlierAbilitySystemComponent()->InitializeForPawn(Turret);
	Turret->GetOutlierAbilitySystemComponent()->InitializeVitalityToSelf(100.0f);
	Turret->SetTurretLifecycleState(EAutoTurretLifecycleState::Active);
	Turret->GetRoomTagComp()->AssignDefaultRoomTag(Fixture.RoomTag);
	Turret->EnterCombatFromRoom(Query.CapturedTarget->GetActorLocation(), false);
	AI->Possess(Turret);
	AI->SetEnemyPerceptionEnabled(false);
	AI->SetActorTickEnabled(false);
	Turret->GetStateTreeComponent()->StopLogic(TEXT("Controlled turret target-selection test"));
	const bool bLaser = Parameters == TEXT("CommittedLaser");
	AWeaponBase* Weapon = bLaser ? Fixture.World->SpawnActor<ALaserWeapon>() : Fixture.Enemy->GetCurrentWeapon();
	if (!TestNotNull(TEXT("Actual weapon exists"), Weapon))
	{
		return false;
	}
	Weapon->OnEquipped(Turret);
	FindFProperty<FObjectPropertyBase>(AEnemyBase::StaticClass(), TEXT("CurrentWeapon"))->SetObjectPropertyValue_InContainer(Turret, Weapon);
	AActor* Shooter = Query.CapturedTarget.Get();
	AI->SetVisibleTargetsForTesting({Shooter});
	Turret->SetPlayerCurrentlyVisible(true);
	Turret->RefreshTargetTracking();
	if (!TestEqual(TEXT("Turret initially tracks Shooter"), Turret->GetTrackedTarget(), Shooter))
	{
		return false;
	}
	Turret->RememberTargetAttackPreparation();
	Turret->RememberTargetAttackRecovery(2.0f);
	Turret->SetAttackPhase(bLaser ? EEnemyAttackPhase::Telegraph : EEnemyAttackPhase::Firing);
	AI->SetVisibleTargetsForTesting({Partner});
	Turret->RefreshTargetTracking();
	TestEqual(TEXT("Only the committed laser retains its hidden target"), Turret->GetTrackedTarget(), bLaser ? Shooter : static_cast<AActor*>(Partner));
	TestEqual(TEXT("Turret intent agrees with actual tracking"), Turret->GetPerceptionGoal().Target.Get(), Turret->GetTrackedTarget());
	TestTrue(TEXT("Target handoff preserves the common recovery deadline"), FMath::IsNearlyEqual(Turret->GetTargetAttackRecoveryRemaining(), 2.0f));
	if (bLaser)
	{
		Turret->SetAttackPhase(EEnemyAttackPhase::Idle);
		Turret->RefreshTargetTracking();
		TestEqual(TEXT("Laser completion adopts the currently visible Partner"), Turret->GetTrackedTarget(), static_cast<AActor*>(Partner));
	}
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyCombatNavigationHeightTest,
	"Outlier.Enemy.Combat.Navigation.MixedCandidateHeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyCombatNavigationHeightTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("MixedDirections"), TEXT("MissingFloor"), TEXT("RoomEdge"),
		TEXT("BudgetPreservesIndices"), TEXT("PartialBudgetPreservesSuccess"), TEXT("Disabled")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyCombatNavigationHeightTest::RunTest(const FString& Parameters)
{
	FEnemyFlightHeightTestWorld Fixture;
	const bool bRoomEdge = Parameters == TEXT("RoomEdge");
	if (!TestTrue(TEXT("Mixed navigation fixture initializes"), Fixture.Initialize(
		Parameters != TEXT("MissingFloor"), FVector(0, 0, 400),
		bRoomEdge ? FVector(1000, 1000, 1000) : FVector(6000, 6000, 2000))))
	{
		return false;
	}
	const FVector Start = Fixture.Enemy->GetActorLocation();
	const FVector Target(1000, 0, 50);
	// 접근·측면·후퇴를 한 배열로 넣어도 기존 접근/후퇴 전용 필터가 다른 용도의 후보를 제거하면 안 된다.
	TArray<FVector> Locations = {FVector(500, 0, 400), FVector(0, 600, 400), FVector(-400, 0, 400)};
	if (bRoomEdge)
	{
		Locations = {FVector(200, 0, 400), FVector(980, 0, 400), FVector(0, 980, 400)};
	}
	if (Parameters == TEXT("Disabled"))
	{
		Fixture.Enemy->FlightHeightSettings.bEnabled = false;
		Fixture.Enemy->FlightHeightSettings.CorrectionStep = 0.0f;
	}
	int32 MaxChecks = HeightTestMaxChecks;
	if (Parameters == TEXT("BudgetPreservesIndices"))
	{
		MaxChecks = 1;
	}
	else if (Parameters == TEXT("PartialBudgetPreservesSuccess"))
	{
		FEnemyFlightHeightSnapshot First;
		if (!TestEqual(TEXT("Single candidate establishes its real trace cost"),
			EnemyFlightHeight::PrepareCombatCandidates(*Fixture.Enemy, {Locations[0]}, Start, Target,
				HeightTestClearance, HeightTestMaxChecks, HeightTestMaxMilliseconds, First), EFlightPathStatus::Success))
		{
			return false;
		}
		MaxChecks = First.CollisionChecks;
	}
	FEnemyFlightHeightSnapshot Snapshot;
	const EFlightPathStatus Status = EnemyFlightHeight::PrepareCombatCandidates(*Fixture.Enemy, Locations,
		Start, Target, HeightTestClearance, MaxChecks, HeightTestMaxMilliseconds, Snapshot);
	const bool bBudget = Parameters.Contains(TEXT("Budget"));
	TestEqual(TEXT("Aggregate result separates budget shortage from completed geometry checks"), Status,
		bBudget ? EFlightPathStatus::BudgetExceeded : EFlightPathStatus::Success);
	if (!TestEqual(TEXT("Every input keeps its corresponding output index"), Snapshot.Candidates.Num(), Locations.Num()))
	{
		return false;
	}
	TestTrue(TEXT("Height checks stay within the caller's shared allowance"), Snapshot.CollisionChecks <= MaxChecks);
	for (int32 Index = 0; Index < Locations.Num(); ++Index)
	{
		const FEnemyFlightHeightCandidate& Candidate = Snapshot.Candidates[Index];
		TestTrue(TEXT("Height adjustment preserves the candidate's navigation direction"),
			FVector::DistSquared2D(Candidate.Location, Locations[Index]) < 0.01);
		if (Parameters == TEXT("Disabled"))
		{
			TestEqual(TEXT("Disabled height does not approve or alter a new height"), Candidate.Status, EFlightPathStatus::Success);
			TestTrue(TEXT("Disabled height preserves the original location"), Candidate.Location.Equals(Locations[Index]));
		}
		else if (Parameters == TEXT("MissingFloor"))
		{
			TestEqual(TEXT("Unverified floor cannot become an approved navigation waypoint"), Candidate.Status, EFlightPathStatus::Unavailable);
		}
		else if (bBudget)
		{
			const bool bCompletedFirst = Parameters == TEXT("PartialBudgetPreservesSuccess") && Index == 0;
			TestEqual(TEXT("Budget stop preserves completed success and marks unverified remainder"), Candidate.Status,
				bCompletedFirst ? EFlightPathStatus::Success : EFlightPathStatus::BudgetExceeded);
		}
		else if (bRoomEdge && Index > 0)
		{
			TestEqual(TEXT("Mixed selection cannot bypass the capsule room boundary"), Candidate.Status, EFlightPathStatus::BlockedEndpoint);
		}
		else
		{
			TestEqual(TEXT("Safe approach, flank and retreat candidates remain available"), Candidate.Status, EFlightPathStatus::Success);
			TestTrue(TEXT("Candidate uses its locally verified floor height"), FMath::IsNearlyEqual(Candidate.Location.Z, 150.0, 0.01));
		}
	}
	if (Parameters == TEXT("Disabled"))
	{
		TestEqual(TEXT("Opt-out does not spend collision checks"), Snapshot.CollisionChecks, 0);
	}
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyCombatNavigationObservationTest,
	"Outlier.Enemy.Combat.Navigation.LastDirectObservation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyCombatNavigationObservationTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("Shooter"), TEXT("Partner")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyCombatNavigationObservationTest::RunTest(const FString& Parameters)
{
	FEnemyFlightHeightTestWorld Fixture;
	FEnemyRunEnvQueryTaskInstanceData Query;
	if (!Fixture.Initialize(true, FVector(0, 0, 400), FVector(6000, 6000, 2000), true)
		|| !MakeHeightCombatQuery(Fixture, TEXT("Orbit"), Query))
	{
		AddError(TEXT("Saved Gun observation fixture could not initialize"));
		return false;
	}
	AEnemyBase& Enemy = *Fixture.Enemy;
	AEnemyAIController& Controller = *CastChecked<AEnemyAIController>(Enemy.GetController());
	Controller.SetEnemyPerceptionEnabled(false);
	Controller.SetActorTickEnabled(false);
	Enemy.GetStateTreeComponent()->StopLogic(TEXT("Controlled F1 last-direct-observation test"));
	AActor* Target = Query.CapturedTarget.Get();
	if (Parameters == TEXT("Partner"))
	{
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Target = Fixture.World->SpawnActor<APartnerCharacter>(APartnerCharacter::StaticClass(),
			FVector(1500, 0, 400), FRotator::ZeroRotator, Spawn);
	}
	if (!TestNotNull(TEXT("Selected player target exists"), Target))
	{
		return false;
	}
	Target->SetActorEnableCollision(false);
	Enemy.SetActorEnableCollision(false);
	Controller.SetVisibleTargetsForTesting({Target});
	Enemy.SetPlayerCurrentlyVisible(true);
	Enemy.ObserveTrackedTarget(Target);
	const FVector ObservedLocation = Target->GetActorLocation();
	TestTrue(TEXT("Directly visible target publishes its observed navigation position"),
		Enemy.GetCombatNavigationTargetLocation(Target).Equals(ObservedLocation));
	Enemy.RememberTargetAttackPreparation();
	ObserveShooterBeforeAttack(Enemy, Target);
	Controller.SetControlRotation((Enemy.GetCombatAimPoint(Target) - Enemy.GetCombatShotOrigin()).Rotation());
	if (!TestTrue(TEXT("The case starts from a real, otherwise valid active burst"), Enemy.StartAttackTarget(Target)))
	{
		return false;
	}
	ON_SCOPE_EXIT { Enemy.StopCurrentWeaponAttack(); };
	// 실제 Actor가 움직여도 직접 시야가 없으면 추적 유예는 위치를 읽거나 새 탄을 승인할 권한이 아니다.
	Controller.SetVisibleTargetsForTesting({});
	Enemy.SetPlayerCurrentlyVisible(false);
	Target->SetActorLocation(ObservedLocation + FVector(500, 300, 100));
	Enemy.ObserveTrackedTarget(Target);
	TestTrue(TEXT("Hidden target movement cannot overwrite the last own observation"),
		Enemy.GetCombatNavigationTargetLocation(Target).Equals(ObservedLocation));
	TestTrue(TEXT("Investigation's remembered location also remains the actual observation"),
		Enemy.GetLastKnownPlayerLocation().Equals(ObservedLocation));
	TestFalse(TEXT("Active burst cannot authorize another shot before direct re-exposure"), Enemy.CanAttackTrackedTarget(Target));
	Controller.SetVisibleTargetsForTesting({Target});
	Enemy.SetPlayerCurrentlyVisible(true);
	Enemy.ObserveTrackedTarget(Target);
	TestTrue(TEXT("Own direct re-exposure may update the navigation position again"),
		Enemy.GetCombatNavigationTargetLocation(Target).Equals(Target->GetActorLocation()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyCombatNavigationHiddenRouteTest,
	"Outlier.Enemy.Combat.Navigation.HiddenRouteUsesObservedPosition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyCombatNavigationHiddenRouteTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemyFlightHeightTestWorld Fixture;
	FEnemyRunEnvQueryTaskInstanceData Query;
	if (!Fixture.Initialize(true, FVector(0, 0, 400), FVector(6000, 6000, 2000), true)
		|| !MakeHeightCombatQuery(Fixture, TEXT("Orbit"), Query))
	{
		AddError(TEXT("Saved Gun hidden-route fixture could not initialize"));
		return false;
	}
	AEnemyBase& Enemy = *Fixture.Enemy;
	AEnemyAIController& Controller = *CastChecked<AEnemyAIController>(Enemy.GetController());
	Controller.SetEnemyPerceptionEnabled(false);
	Controller.SetActorTickEnabled(false);
	Enemy.GetStateTreeComponent()->StopLogic(TEXT("Controlled F1 hidden-route selection test"));
	AActor* Target = Query.CapturedTarget.Get();
	// Perception 비활성화가 비운 직접 시야 집합을 복원한 뒤 실제 추적 갱신으로 시야 상실 유예를 준비한다.
	Controller.SetVisibleTargetsForTesting({Target});
	Enemy.SetPlayerCurrentlyVisible(true);
	Enemy.ObserveTrackedTarget(Target);
	Enemy.RefreshTargetTracking();
	const FVector ObservedLocation = Target->GetActorLocation();
	Controller.SetVisibleTargetsForTesting({});
	Enemy.SetPlayerCurrentlyVisible(false);
	Target->SetActorLocation(ObservedLocation + FVector(1200, 900, 100));
	Enemy.ObserveTrackedTarget(Target);
	if (!TestEqual(TEXT("A real direct-sight loss retains its target for the configured tracking grace"), Enemy.GetTrackedTarget(), Target))
	{
		return false;
	}
	// 완전히 숨은 대상은 마지막 관측점으로 경로만 검증한다. 눈앞에 벽이 있어도 이동 후보를 사격 후보로 승격하지 않는다.
	if (!TestNotNull(TEXT("Cover blocks the direct observed-position line"),
		Fixture.SpawnBlockingBox(FVector(1000, 0, 500), FVector(30, 200, 500))))
	{
		return false;
	}
	EnemySearchTesting::CaptureQuery(Query);
	if (!TestTrue(TEXT("Hidden Gun uses the mixed combat navigation selector"), Query.bUnifiedCombatNavigation))
	{
		return false;
	}
	TestTrue(TEXT("Selection snapshots the own observed position instead of the hidden Actor"),
		Query.SelectedTargetLocation.Equals(ObservedLocation));
	FEnvQueryInstance Result;
	Result.Owner = &Enemy;
	Result.ItemType = UEnvQueryItemType_Point::StaticClass();
	Result.ItemTypeVectorCDO = GetMutableDefault<UEnvQueryItemType_Point>();
	Result.ValueSize = GetDefault<UEnvQueryItemType_Point>()->GetValueSize();
	Result.MarkAsFinishedWithoutIssues();
	Result.AddItemData<UEnvQueryItemType_Point>(ObservedLocation);
	EnemySearchTesting::CompleteQuery(Query, Result);
	if (!TestTrue(TEXT("Completed query starts finite native candidate checking"), Query.bCombatSelectionPending))
	{
		return false;
	}
	for (bool bProgressOnly : Query.CombatCandidatesProgressOnly)
	{
		TestTrue(TEXT("Hidden-target candidates cannot grant attack eligibility"), bProgressOnly);
	}
	EStateTreeRunStatus Status = EStateTreeRunStatus::Running;
	for (int32 Step = 0; Step < 512 && Status == EStateTreeRunStatus::Running; ++Step)
	{
		++GFrameCounter;
		Status = EnemySearchTesting::ResumeCombatSelection(Query);
	}
	if (!TestEqual(TEXT("Verified hidden-position approach finishes within bounded scheduler turns"), Status, EStateTreeRunStatus::Succeeded))
	{
		AddInfo(FString::Printf(TEXT("Selection=%s Path=%s Checked=%d"),
			*UEnum::GetValueAsString(Query.QueryResult), *UEnum::GetValueAsString(Query.SelectionPathStatus), Query.CheckedPathCandidates));
		return false;
	}
	TestTrue(TEXT("Selection publishes a verified route as well as a destination"), Query.bHasDestination && Query.SelectedWaypoints.Num() >= 2);
	TestTrue(TEXT("A safe waypoint does not replace the actual observation"), Enemy.GetCombatNavigationTargetLocation(Target).Equals(ObservedLocation));
	TestFalse(TEXT("Route selection cannot authorize firing at the hidden player"), Enemy.CanAttackTrackedTarget(Target));
	Enemy.ReleaseCombatReservation(Query.CombatReservationRequest);
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyCombatNavigationVisibleCoverTest,
	"Outlier.Enemy.Combat.Navigation.VisibleCoverRoute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyCombatNavigationVisibleCoverTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("BlockedIntermediateSight"), TEXT("KeepValidRouteWithinGain"), TEXT("ReservationFallback")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyCombatNavigationVisibleCoverTest::RunTest(const FString& Parameters)
{
	FEnemyFlightHeightTestWorld Fixture;
	FEnemyRunEnvQueryTaskInstanceData Query;
	if (!Fixture.Initialize(true, FVector(0, 0, 400), FVector(6000, 6000, 2000), true)
		|| !MakeHeightCombatQuery(Fixture, TEXT("Orbit"), Query))
	{
		AddError(TEXT("Saved Gun visible-cover fixture could not initialize"));
		return false;
	}
	AEnemyBase& Enemy = *Fixture.Enemy;
	AEnemyAIController& Controller = *CastChecked<AEnemyAIController>(Enemy.GetController());
	Controller.SetEnemyPerceptionEnabled(false);
	Controller.SetActorTickEnabled(false);
	Enemy.GetStateTreeComponent()->StopLogic(TEXT("Controlled F1 visible-cover route test"));
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APartnerCharacter* Target = Fixture.World->SpawnActor<APartnerCharacter>(APartnerCharacter::StaticClass(),
		FVector(1500, 0, 400), FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("Directly visible Partner spawns"), Target))
	{
		return false;
	}
	Target->SetActorEnableCollision(false);
	Controller.SetVisibleTargetsForTesting({Target});
	Enemy.SetPlayerCurrentlyVisible(true);
	Enemy.ObserveTrackedTarget(Target);
	Enemy.RefreshTargetTracking();
	Enemy.FlightHeightSettings.bEnabled = false;
	FEnemyStat& Stat = const_cast<FEnemyStat&>(Enemy.GetRuntimeStat());
	Stat.TacticalSightBlockDuration = 0.0f;
	Stat.TacticalPathChangeGain = 0.3f;
	Enemy.GetCharacterMovement()->MaxFlySpeed = 1000.0f;
	Enemy.GetCharacterMovement()->MaxAcceleration = 100000.0f;
	if (!TestNotNull(TEXT("Cover blocks the current muzzle and straight movement route"),
		Fixture.SpawnBlockingBox(FVector(750, 0, 500), FVector(30, 250, 500))))
	{
		return false;
	}
	Enemy.UpdateCombatTacticalSightBlock();
	if (!TestTrue(TEXT("Actual current muzzle blockage activates repositioning"), Enemy.NeedsCombatTacticalReposition()))
	{
		return false;
	}
	Query.MaxSearchNodes = 4096;
	Query.MaxCollisionChecks = 16384;
	EnemySearchTesting::CaptureQuery(Query);
	if (!TestTrue(TEXT("Visible muzzle obstruction selects unified navigation"), Query.bUnifiedCombatNavigation && Query.bNavigationTargetVisible))
	{
		return false;
	}
	FEnvQueryInstance Result;
	Result.Owner = &Enemy;
	Result.ItemType = UEnvQueryItemType_Point::StaticClass();
	Result.ItemTypeVectorCDO = GetMutableDefault<UEnvQueryItemType_Point>();
	Result.ValueSize = GetDefault<UEnvQueryItemType_Point>()->GetValueSize();
	Result.MarkAsFinishedWithoutIssues();
	Result.AddItemData<UEnvQueryItemType_Point>(FVector(1100, 0, 400));
	EnemySearchTesting::CompleteQuery(Query, Result);
	if (!TestTrue(TEXT("Production selector begins shared-budget native checking"), Query.bCombatSelectionPending))
	{
		return false;
	}
	// 후보 생성은 별도 검사한다. 여기서는 고정된 실제 공격 위치로 경로·비용·예약 연결을 재현한다.
	const bool bKeep = Parameters == TEXT("KeepValidRouteWithinGain");
	const bool bReservation = Parameters == TEXT("ReservationFallback");
	const FVector First = bReservation ? FVector(1050, 0, 400) : FVector(1100, 0, 400);
	Query.CombatCandidates = {{First, 0.0f, false}};
	if (bKeep)
	{
		// 기존 경로는 안전하지만 조금 돌아간다. 더 짧은 대안의 이득을 0.3초 안에 두고 유지 정책을 검사한다.
		Enemy.GetCharacterMovement()->MaxFlySpeed = 4000.0f;
		Query.CombatCandidates.Add({FVector(900, 0, 400), 0.0f, false});
		Query.CurrentCombatCandidate = 0;
		Query.CurrentCombatRoute = {Enemy.GetActorLocation(), FVector(600, 600, 400), FVector(900, 600, 400), First};
	}
	else if (bReservation)
	{
		Query.CombatCandidates.Add({FVector(1100, 600, 400), 0.0f, false});
	}
	Query.CombatCandidatesProgressOnly.Init(false, Query.CombatCandidates.Num());
	Query.CombatCandidatesPassedQuery.Init(true, Query.CombatCandidates.Num());
	Query.CombatCandidatesHeightLimited.Init(false, Query.CombatCandidates.Num());
	Query.ValidatedCombatPaths.SetNum(Query.CombatCandidates.Num());
	Query.MaxPathCandidates = Query.CombatCandidates.Num();
	UEnemyRoomSubsystem& Rooms = *Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
	AEnemyBase* Other = nullptr;
	uint64 OtherRequest = 0;
	if (bReservation)
	{
		Other = SpawnReservationEnemy(Fixture, FVector(-500, 1500, 400));
		if (!TestNotNull(TEXT("Competing valid combatant spawns"), Other))
		{
			return false;
		}
		OtherRequest = Other->BeginCombatReservationRequest();
		if (!TestTrue(TEXT("Another owner reserves the otherwise faster destination"), ReserveTestDestination(Rooms, *Other, OtherRequest, First)))
		{
			return false;
		}
	}
	ON_SCOPE_EXIT
	{
		Enemy.ReleaseCombatReservation(Query.CombatReservationRequest);
		if (Other)
		{
			Rooms.ReleaseCombatReservation(Other, OtherRequest);
		}
	};
	EStateTreeRunStatus Status = EStateTreeRunStatus::Running;
	for (int32 Step = 0; Step < 1024 && Status == EStateTreeRunStatus::Running; ++Step)
	{
		++GFrameCounter;
		Status = EnemySearchTesting::ResumeCombatSelection(Query);
	}
	if (!TestEqual(TEXT("Visible cover selector reaches a bounded successful result"), Status, EStateTreeRunStatus::Succeeded))
	{
		AddInfo(FString::Printf(TEXT("Selection=%s Path=%s Checked=%d"),
			*UEnum::GetValueAsString(Query.QueryResult), *UEnum::GetValueAsString(Query.SelectionPathStatus), Query.CheckedPathCandidates));
		return false;
	}
	TestTrue(TEXT("Selected route retains verified movement geometry"), Query.SelectedWaypoints.Num() >= 2);
	if (!bReservation)
	{
		TestTrue(TEXT("Blocked direct route retains real cover-bypass waypoints"), Query.SelectedWaypoints.Num() > 2);
	}
	int32 Checks = 32;
	TestTrue(TEXT("The selected endpoint has its own actual weapon path"), Enemy.HasCombatAttackPathFrom(Target, Query.Destination, Checks));
	bool bBlockedIntermediateSight = false;
	for (int32 Index = 0; Index < Query.SelectedWaypoints.Num() - 1; ++Index)
	{
		Checks = 32;
		bBlockedIntermediateSight |= !Enemy.HasCombatAttackPathFrom(Target, Query.SelectedWaypoints[Index], Checks);
	}
	TestTrue(TEXT("Safe cover bypass survives blocked sight before its attack endpoint"), bBlockedIntermediateSight);
	if (bKeep)
	{
		AddInfo(FString::Printf(TEXT("CurrentSeconds=%.6f BestSeconds=%.6f Gain=%.6f AlternativeValidated=%d"),
			Query.CurrentCombatSeconds, Query.BestCombatSeconds, Query.CurrentCombatSeconds - Query.BestCombatSeconds,
			Query.ValidatedCombatPaths.IsValidIndex(1) && Query.ValidatedCombatPaths[1].IsValid()));
		TestTrue(TEXT("The alternative completed its actual path validation"),
			Query.ValidatedCombatPaths.IsValidIndex(1) && Query.ValidatedCombatPaths[1].IsValid());
		TestTrue(TEXT("A valid current route remains selected inside the approved 0.3-second gain"), Query.Destination.Equals(First));
		TestTrue(TEXT("The alternative is genuinely faster but lacks the required replacement gain"),
			Query.CurrentCombatSeconds > Query.BestCombatSeconds
			&& Query.CurrentCombatSeconds - Query.BestCombatSeconds < 0.3);
	}
	else if (bReservation)
	{
		TestTrue(TEXT("Reservation conflict uses the validated alternative instead of waiting for the first destination"),
			Query.Destination.Equals(Query.CombatCandidates[1].Destination));
		TestFalse(TEXT("Reservation conflict is not a physical failure"), Enemy.IsCombatTacticalCandidateFailure(TEXT("CombatNavigation"), First));
	}
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyCombatNavigationCacheLifetimeTest,
	"Outlier.Enemy.Combat.Navigation.CachedRouteLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyCombatNavigationCacheLifetimeTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("SameTargetEpisode"), TEXT("TargetChanged"), TEXT("PoolReuse")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyCombatNavigationCacheLifetimeTest::RunTest(const FString& Parameters)
{
	FEnemyFlightHeightTestWorld Fixture;
	FEnemyRunEnvQueryTaskInstanceData Query;
	if (!Fixture.Initialize(true, FVector(0, 0, 400), FVector(6000, 6000, 2000), true)
		|| !MakeHeightCombatQuery(Fixture, TEXT("Orbit"), Query))
	{
		AddError(TEXT("Saved Gun cached-route fixture could not initialize"));
		return false;
	}
	AEnemyBase& Enemy = *Fixture.Enemy;
	AEnemyAIController& Controller = *CastChecked<AEnemyAIController>(Enemy.GetController());
	Controller.SetEnemyPerceptionEnabled(false);
	Controller.SetActorTickEnabled(false);
	AActor* Target = Query.CapturedTarget.Get();
	Controller.SetVisibleTargetsForTesting({Target});
	Enemy.SetPlayerCurrentlyVisible(true);
	Enemy.ObserveTrackedTarget(Target);
	Enemy.RefreshTargetTracking();
	Enemy.GetStateTreeComponent()->StopLogic(TEXT("Controlled F1 cached-route lifetime test"));
	const uint64 Serial = Enemy.EnsureCombatTacticalEpisode(Target, Target->GetActorLocation());
	const uint64 Reservation = Enemy.BeginCombatReservationRequest();
	UEnemyRoomSubsystem& Rooms = *Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
	const FVector Destination(1000, 500, 400);
	const TArray<FVector> Route = {Enemy.GetActorLocation(), FVector(500, 500, 400), Destination};
	if (!TestTrue(TEXT("The original episode owns a real destination reservation"),
		ReserveTestDestination(Rooms, Enemy, Reservation, Destination) && Enemy.AdoptCombatReservation(Reservation)))
	{
		return false;
	}
	Enemy.RememberCombatNavigationPath(Reservation, Destination, Route, true);
	TArray<FVector> ReadRoute;
	bool bProgressOnly = false;
	if (!TestTrue(TEXT("Live selection can read its exact route and movement-only purpose"),
		Enemy.GetCombatNavigationPath(Reservation, Destination, ReadRoute, &bProgressOnly) && bProgressOnly))
	{
		return false;
	}
	if (Parameters == TEXT("SameTargetEpisode"))
	{
		Target->SetActorLocation(Target->GetActorLocation() + FVector(300, 0, 0));
		Enemy.ObserveTrackedTarget(Target);
		TestTrue(TEXT("A materially new observation starts another episode for the same target"),
			Enemy.EnsureCombatTacticalEpisode(Target, Target->GetActorLocation()) != Serial);
	}
	else if (Parameters == TEXT("TargetChanged"))
	{
		APartnerCharacter* NewTarget = Fixture.World->SpawnActor<APartnerCharacter>();
		if (!TestNotNull(TEXT("Different player target spawns"), NewTarget))
		{
			return false;
		}
		Controller.SetVisibleTargetsForTesting({NewTarget});
		Enemy.ObserveTrackedTarget(NewTarget);
	}
	else
	{
		Enemy.PrepareForPoolSpawn(Fixture.World->GetSubsystem<UEnemyPoolSubsystem>());
		FEnemyPoolLeaseContext Lease;
		Lease.RoomTag = Fixture.RoomTag;
		Lease.GameplayGeneration = Query.CapturedGeneration + 1;
		if (!TestTrue(TEXT("Actual pool reuse starts another lease and generation"),
			Enemy.BeginPoolLease(Lease, FTransform(Enemy.GetActorLocation()), Query.CapturedLease + 1)))
		{
			return false;
		}
	}
	TestFalse(TEXT("An old selection cannot supply movement permission after the boundary"),
		Enemy.GetCombatNavigationPath(Reservation, Destination, ReadRoute));
	FVector ReadDestination;
	bProgressOnly = false;
	const bool bHasComparisonRoute = Enemy.GetCombatNavigationHandoff(ReadRoute, ReadDestination, &bProgressOnly);
	if (Parameters == TEXT("SameTargetEpisode") || Parameters == TEXT("TargetChanged"))
	{
		TestTrue(TEXT("Geometry survives only as a candidate that needs fresh validation"), bHasComparisonRoute);
		TestTrue(TEXT("Candidate fallback preserves its destination and movement-only purpose"),
			ReadDestination.Equals(Destination) && bProgressOnly && ReadRoute.Num() >= 2);
		TestEqual(TEXT("Only a different target requires unconditional valid-route retention"),
			Enemy.IsCombatNavigationRetargetPending(), Parameters == TEXT("TargetChanged"));
	}
	else
	{
		TestFalse(TEXT("Another pool lifetime cannot inherit the old comparison route"), bHasComparisonRoute);
	}
	Enemy.ReleaseCombatReservation(Reservation);
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyCombatNavigationRetargetRouteTest,
	"Outlier.Enemy.Combat.Navigation.RetargetRoute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyCombatNavigationRetargetRouteTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("KeepValidRoute"), TEXT("NewSightBlocked"), TEXT("RouteBlocked"),
		TEXT("ReservationConflict"), TEXT("StaleSelection"), TEXT("OldProgressNeedsNewSight"), TEXT("HiddenWrongDirection"),
		TEXT("VelocityBeforeApproval"), TEXT("VelocityReverse"), TEXT("VelocityDifferentDestination")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyCombatNavigationRetargetRouteTest::RunTest(const FString& Parameters)
{
	FEnemyFlightHeightTestWorld Fixture;
	FEnemyRunEnvQueryTaskInstanceData Query;
	if (!Fixture.Initialize(true, FVector(0, 0, 400), FVector(6000, 6000, 2000), true)
		|| !MakeHeightCombatQuery(Fixture, TEXT("Orbit"), Query))
	{
		AddError(TEXT("Saved Gun retarget-route fixture could not initialize"));
		return false;
	}
	AEnemyBase& Enemy = *Fixture.Enemy;
	AEnemyAIController& Controller = *CastChecked<AEnemyAIController>(Enemy.GetController());
	Controller.SetEnemyPerceptionEnabled(false);
	Controller.SetActorTickEnabled(false);
	Enemy.GetStateTreeComponent()->StopLogic(TEXT("Controlled F2 retarget-route test"));
	Enemy.FlightHeightSettings.bEnabled = false;
	Enemy.GetCharacterMovement()->MaxFlySpeed = 1000.0f;
	Enemy.GetCharacterMovement()->MaxAcceleration = 100000.0f;
	const_cast<FEnemyStat&>(Enemy.GetRuntimeStat()).TacticalSightBlockDuration = 0.0f;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APartnerCharacter* First = Fixture.World->SpawnActor<APartnerCharacter>(APartnerCharacter::StaticClass(),
		FVector(1500, 0, 400), FRotator::ZeroRotator, Spawn);
	APartnerCharacter* Second = Fixture.World->SpawnActor<APartnerCharacter>(APartnerCharacter::StaticClass(),
		Parameters == TEXT("HiddenWrongDirection") ? FVector(-1500, 0, 400) : FVector(1500, 200, 400),
		FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("First directly observed target exists"), First)
		|| !TestNotNull(TEXT("Second directly observed target exists"), Second))
	{
		return false;
	}
	First->SetActorEnableCollision(false);
	Second->SetActorEnableCollision(false);
	Controller.SetVisibleTargetsForTesting({First});
	Enemy.SetPlayerCurrentlyVisible(true);
	Enemy.ObserveTrackedTarget(First);
	Enemy.RefreshTargetTracking();
	UEnemyRoomSubsystem& Rooms = *Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
	const FVector Destination(1100, 0, 400);
	const TArray<FVector> Route = {Enemy.GetActorLocation(), FVector(600, 600, 400), FVector(900, 600, 400), Destination};
	const uint64 OldRequest = Enemy.BeginCombatReservationRequest();
	if (!TestTrue(TEXT("A real old-target reservation exists before retargeting"),
		ReserveTestDestination(Rooms, Enemy, OldRequest, Destination) && Enemy.AdoptCombatReservation(OldRequest)))
	{
		return false;
	}
	Enemy.RememberCombatNavigationPath(OldRequest, Destination, Route, Parameters == TEXT("OldProgressNeedsNewSight"));
	const FVector PreviousVelocity = Parameters == TEXT("VelocityReverse") ? FVector(-5000, -5000, 0) : FVector(5000, 5000, 0);
	Enemy.GetCharacterMovement()->Velocity = PreviousVelocity;
	// 감지만 바꾼 것이 아니라 실제 대상 교체 경계를 통과한다. 옛 예약 권한은 여기서 끝난다.
	Controller.SetVisibleTargetsForTesting({Second});
	Enemy.ObserveTrackedTarget(Second);
	Enemy.RefreshTargetTracking();
	TArray<FVector> OldRead;
	TestFalse(TEXT("Retarget cannot reuse the old reservation as movement permission"),
		Enemy.GetCombatNavigationPath(OldRequest, Destination, OldRead));
	if (!TestTrue(TEXT("Retarget preserves geometry for fresh candidate validation"), Enemy.IsCombatNavigationRetargetPending()))
	{
		return false;
	}
	if (Parameters == TEXT("VelocityBeforeApproval"))
	{
		FVector Velocity;
		TestFalse(TEXT("Geometry-only handoff cannot restore velocity before new-target validation"),
			Enemy.ConsumeCombatNavigationRetargetVelocity(Destination, Velocity));
		return true;
	}
	// 아직 승인되지 않은 경로로 관성이 진행하지 않도록 실제 Move의 중단 상태를 재현한다.
	Enemy.GetCharacterMovement()->StopMovementImmediately();
	if (Parameters == TEXT("HiddenWrongDirection"))
	{
		Controller.SetVisibleTargetsForTesting({});
		Enemy.SetPlayerCurrentlyVisible(false);
		Enemy.RefreshTargetTracking();
	}
	// 현재 총구와 직진은 엄폐물에 막힌다. 기존 경로의 우회 구간은 실제로 통과 가능하다.
	if (!TestNotNull(TEXT("Cover exists"), Fixture.SpawnBlockingBox(FVector(750, 0, 500), FVector(30, 250, 500))))
	{
		return false;
	}
	const bool bSightBlocked = Parameters == TEXT("NewSightBlocked") || Parameters == TEXT("OldProgressNeedsNewSight");
	if (bSightBlocked)
	{
		Fixture.SpawnBlockingBox(FVector(1300, 100, 500), FVector(20, 250, 500));
	}
	if (Parameters == TEXT("RouteBlocked") || Parameters == TEXT("VelocityDifferentDestination"))
	{
		Fixture.SpawnBlockingBox(FVector(900, 600, 400), FVector(40, 40, 100));
	}
	Enemy.UpdateCombatTacticalSightBlock();
	Query.MaxSearchNodes = 4096;
	Query.MaxCollisionChecks = 16384;
	EnemySearchTesting::CaptureQuery(Query);
	FEnvQueryInstance Result;
	Result.Owner = &Enemy;
	Result.ItemType = UEnvQueryItemType_Point::StaticClass();
	Result.ItemTypeVectorCDO = GetMutableDefault<UEnvQueryItemType_Point>();
	Result.ValueSize = GetDefault<UEnvQueryItemType_Point>()->GetValueSize();
	Result.MarkAsFinishedWithoutIssues();
	Result.AddItemData<UEnvQueryItemType_Point>(Destination);
	EnemySearchTesting::CompleteQuery(Query, Result);
	if (Parameters == TEXT("HiddenWrongDirection"))
	{
		TestEqual(TEXT("A route away from the new hidden observation is not a retained candidate"), Query.CurrentCombatCandidate, INDEX_NONE);
		Enemy.ReleaseCombatReservation(Query.CombatReservationRequest);
		return true;
	}
	if (!TestTrue(TEXT("Fresh new-target selector prioritizes the old geometry"), Query.bPreserveCurrentCombatRoute && Query.CurrentCombatCandidate == 0))
	{
		return false;
	}
	TestFalse(TEXT("Visible new target does not inherit an old movement-only endpoint approval"), Query.CombatCandidatesProgressOnly[0]);
	// 대안은 훨씬 짧거나 새로 열린 위치다. 유효한 기존 경로를 비용 순위로 교체하지 않는지 검사한다.
	const FVector Alternative = bSightBlocked ? FVector(1100, 800, 400)
		: Parameters == TEXT("KeepValidRoute") ? FVector(100, 600, 400) : FVector(1100, -600, 400);
	Query.CombatCandidates = {{Destination, 0.0f, false}, {Alternative, 0.0f, false}};
	Query.CombatCandidatesProgressOnly.Init(false, 2);
	Query.CombatCandidatesPassedQuery.Init(true, 2);
	Query.CombatCandidatesHeightLimited.Init(false, 2);
	Query.ValidatedCombatPaths.SetNum(2);
	Query.MaxPathCandidates = 2;
	AEnemyBase* Other = nullptr;
	uint64 OtherRequest = 0;
	if (Parameters == TEXT("ReservationConflict"))
	{
		Other = SpawnReservationEnemy(Fixture, FVector(-500, 1500, 400));
		if (!TestNotNull(TEXT("Competing reservation owner exists"), Other))
		{
			return false;
		}
		OtherRequest = Other->BeginCombatReservationRequest();
		if (!TestTrue(TEXT("A new owner can occupy the released old destination"), ReserveTestDestination(Rooms, *Other, OtherRequest, Destination)))
		{
			return false;
		}
	}
	ON_SCOPE_EXIT
	{
		Enemy.ReleaseCombatReservation(Query.CombatReservationRequest);
		if (Other)
		{
			Rooms.ReleaseCombatReservation(Other, OtherRequest);
		}
	};
	if (Parameters == TEXT("StaleSelection"))
	{
		Controller.SetVisibleTargetsForTesting({First});
		Enemy.ObserveTrackedTarget(First);
		++GFrameCounter;
		TestEqual(TEXT("Another target change rejects the old in-flight candidate evaluation"),
			EnemySearchTesting::ResumeCombatSelection(Query), EStateTreeRunStatus::Failed);
		TestFalse(TEXT("Stale candidate never publishes movement permission"), Query.bHasDestination);
		return true;
	}
	EStateTreeRunStatus Status = EStateTreeRunStatus::Running;
	for (int32 Step = 0; Step < 1024 && Status == EStateTreeRunStatus::Running; ++Step)
	{
		++GFrameCounter;
		Status = EnemySearchTesting::ResumeCombatSelection(Query);
	}
	if (!TestEqual(TEXT("Retarget path selection completes within bounded shared-budget turns"), Status, EStateTreeRunStatus::Succeeded))
	{
		AddInfo(FString::Printf(TEXT("Selection=%s Path=%s Checked=%d"),
			*UEnum::GetValueAsString(Query.QueryResult), *UEnum::GetValueAsString(Query.SelectionPathStatus), Query.CheckedPathCandidates));
		return false;
	}
	const bool bKeep = Parameters == TEXT("KeepValidRoute") || Parameters == TEXT("VelocityReverse");
	TestTrue(TEXT("Only a freshly valid and unoccupied old route survives retargeting"),
		Query.Destination.Equals(bKeep ? Destination : Alternative));
	TestFalse(TEXT("Successful adoption consumes the pending geometry-only handoff"), Enemy.IsCombatNavigationRetargetPending());
	if (bKeep)
	{
		TestEqual(TEXT("Valid old route is adopted before searching for faster alternatives"), Query.CheckedPathCandidates, 1);
		TestEqual(TEXT("Retarget retains the actual safe cover-bypass geometry"), Query.SelectedWaypoints.Num(), Route.Num());
	}
	FEnemyFlyToLocationTaskInstanceData Move;
	Move.Enemy = &Enemy;
	Move.Destination = Query.Destination;
	Move.bCombatNavigationPath = true;
	Move.PathWaypoints = Query.SelectedWaypoints;
	EnemyFlightTesting::PrepareSpeedLimits(Move);
	const FVector ResumedVelocity = Enemy.GetVelocity();
	if (bKeep && Parameters != TEXT("VelocityReverse"))
	{
		TestTrue(TEXT("Fresh validated route restores forward speed without exceeding its new movement limit"),
			ResumedVelocity.Size() > 0.0 && ResumedVelocity.Size() <= Enemy.GetCharacterMovement()->MaxFlySpeed + 0.01);
	}
	else if (Parameters == TEXT("VelocityReverse") || Parameters == TEXT("VelocityDifferentDestination"))
	{
		TestTrue(TEXT("Reverse velocity or a different destination never inherits old inertia"), ResumedVelocity.IsNearlyZero());
	}
	FVector ConsumedVelocity;
	TestFalse(TEXT("Retarget velocity can only be consumed once"),
		Enemy.ConsumeCombatNavigationRetargetVelocity(Query.Destination, ConsumedVelocity));
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyCombatF3AlternativesExhaustionTest,
	"Outlier.Enemy.Combat.F3Attempt.AlternativesExhaustion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyCombatF3AlternativesExhaustionTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("PhysicalExhaustion"), TEXT("CandidateLimit"), TEXT("BudgetLimit"),
		TEXT("ReservationConflict"), TEXT("SafeProgressPath")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyCombatF3AlternativesExhaustionTest::RunTest(const FString& Parameters)
{
	FEnemyFlightHeightTestWorld Fixture;
	FEnemyRunEnvQueryTaskInstanceData Query;
	if (!Fixture.Initialize(true, FVector(0, 0, 400), FVector(6000, 6000, 2000), true)
		|| !MakeHeightCombatQuery(Fixture, TEXT("Orbit"), Query))
	{
		AddError(TEXT("F3 physical alternatives fixture could not initialize"));
		return false;
	}
	AEnemyBase& Enemy = *Fixture.Enemy;
	AEnemyAIController& Controller = *CastChecked<AEnemyAIController>(Enemy.GetController());
	Controller.SetEnemyPerceptionEnabled(false);
	Controller.SetActorTickEnabled(false);
	Enemy.GetStateTreeComponent()->StopLogic(TEXT("Controlled F3 alternatives exhaustion test"));
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APartnerCharacter* Target = Fixture.World->SpawnActor<APartnerCharacter>(APartnerCharacter::StaticClass(),
		FVector(1500, 0, 400), FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("Own directly observed target spawns"), Target))
	{
		return false;
	}
	Target->SetActorEnableCollision(false);
	Controller.SetVisibleTargetsForTesting({Target});
	Enemy.SetPlayerCurrentlyVisible(true);
	Enemy.ObserveTrackedTarget(Target);
	Enemy.RefreshTargetTracking();
	Enemy.FlightHeightSettings.bEnabled = false;
	FEnemyStat& Stat = const_cast<FEnemyStat&>(Enemy.GetRuntimeStat());
	Stat.TacticalSightBlockDuration = 0.0f;
	if (!TestNotNull(TEXT("Real environment blocks the current muzzle"),
		Fixture.SpawnBlockingBox(FVector(750, 0, 500), FVector(30, 250, 500))))
	{
		return false;
	}
	Enemy.RefreshCombatAimPoint();
	Enemy.UpdateCombatTacticalSightBlock();
	if (!TestTrue(TEXT("Current obstruction requires combat navigation"), Enemy.NeedsCombatTacticalReposition()))
	{
		return false;
	}
	TestFalse(TEXT("Muzzle obstruction alone cannot grant a speculative attempt"), Enemy.PrepareTargetAttackAttempt(0.5f));
	Query.MaxSearchNodes = 4096;
	Query.MaxCollisionChecks = 16384;
	EnemySearchTesting::CaptureQuery(Query);
	FEnvQueryInstance Result;
	Result.Owner = &Enemy;
	Result.ItemType = UEnvQueryItemType_Point::StaticClass();
	Result.ItemTypeVectorCDO = GetMutableDefault<UEnvQueryItemType_Point>();
	Result.ValueSize = GetDefault<UEnvQueryItemType_Point>()->GetValueSize();
	Result.MarkAsFinishedWithoutIssues();
	Result.AddItemData<UEnvQueryItemType_Point>(FVector(1100, 0, 400));
	EnemySearchTesting::CompleteQuery(Query, Result);
	if (!TestTrue(TEXT("Production unified selector starts its actual candidate checks"),
		Query.bUnifiedCombatNavigation && Query.bNavigationTargetVisible && Query.bCombatSelectionPending))
	{
		return false;
	}
	const bool bReservation = Parameters == TEXT("ReservationConflict");
	const bool bProgress = Parameters == TEXT("SafeProgressPath");
	// 후보 생성과 분리해 공격 위치와 안전 경유점을 포함한 유한 집합을 실제 충돌로 평가한다.
	Query.CombatCandidates = bReservation
		? TArray<FEnemyOrbitCandidate>{{FVector(1100, 600, 400), 0.0f, false}}
		: TArray<FEnemyOrbitCandidate>{{FVector(500, 600, 400), 0.0f, false},
			{FVector(500, -600, 400), 0.0f, false}, {FVector(0, 500, 400), 0.0f, false}};
	Query.CombatCandidatesProgressOnly.Init(false, Query.CombatCandidates.Num());
	if (!bReservation)
	{
		Query.CombatCandidatesProgressOnly.Last() = true;
		for (int32 Index = 0; Index < Query.CombatCandidates.Num(); ++Index)
		{
			if (bProgress && Index == Query.CombatCandidates.Num() - 1)
			{
				continue;
			}
			if (!TestNotNull(TEXT("Candidate has a real physical endpoint blocker"),
				Fixture.SpawnBlockingBox(Query.CombatCandidates[Index].Destination, FVector(100, 100, 150))))
			{
				return false;
			}
		}
	}
	Query.CombatCandidatesPassedQuery.Init(true, Query.CombatCandidates.Num());
	Query.CombatCandidatesHeightLimited.Init(false, Query.CombatCandidates.Num());
	Query.ValidatedCombatPaths.SetNum(Query.CombatCandidates.Num());
	Query.MaxPathCandidates = Parameters == TEXT("CandidateLimit") ? 1 : Query.CombatCandidates.Num();
	if (Parameters == TEXT("BudgetLimit"))
	{
		Query.CombatRemainingChecks = 0;
	}
	UEnemyRoomSubsystem& Rooms = *Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
	AEnemyBase* Other = nullptr;
	uint64 OtherRequest = 0;
	if (bReservation)
	{
		Other = SpawnReservationEnemy(Fixture, FVector(-500, 1500, 400));
		if (!TestNotNull(TEXT("Competing combat reservation owner spawns"), Other))
		{
			return false;
		}
		OtherRequest = Other->BeginCombatReservationRequest();
		if (!TestTrue(TEXT("Otherwise reachable attack position is reserved by another owner"),
			ReserveTestDestination(Rooms, *Other, OtherRequest, Query.CombatCandidates[0].Destination)))
		{
			return false;
		}
	}
	ON_SCOPE_EXIT
	{
		Enemy.ReleaseCombatReservation(Query.CombatReservationRequest);
		if (Other)
		{
			Rooms.ReleaseCombatReservation(Other, OtherRequest);
		}
	};
	EStateTreeRunStatus Status = EStateTreeRunStatus::Running;
	for (int32 Step = 0; Step < 1024 && Status == EStateTreeRunStatus::Running; ++Step)
	{
		++GFrameCounter;
		Status = EnemySearchTesting::ResumeCombatSelection(Query);
	}
	AddInfo(FString::Printf(TEXT("Selection=%s Path=%s Checked=%d Complete=%d/%d"),
		*UEnum::GetValueAsString(Query.QueryResult), *UEnum::GetValueAsString(Query.SelectionPathStatus),
		Query.CheckedPathCandidates, Query.NextCombatCandidate, Query.CombatCandidates.Num()));
	if (bProgress)
	{
		TestEqual(TEXT("Verified safe progress remains an executable alternative"), Status, EStateTreeRunStatus::Succeeded);
		TestTrue(TEXT("Progress path retains actual collision-checked geometry"), Query.SelectedWaypoints.Num() >= 2);
	}
	else
	{
		TestEqual(TEXT("Blocked selection terminates within its bounded checks"), Status, EStateTreeRunStatus::Failed);
		const EEnemyEnvQueryResult Expected = Parameters == TEXT("CandidateLimit") ? EEnemyEnvQueryResult::CandidateLimitReached
			: Parameters == TEXT("BudgetLimit") ? EEnemyEnvQueryResult::PathBudgetExceeded
			: bReservation ? EEnemyEnvQueryResult::ReservationConflict : EEnemyEnvQueryResult::NoReachableDestination;
		TestEqual(TEXT("Physical impossibility stays distinct from limited or occupied alternatives"), Query.QueryResult, Expected);
	}
	const bool bPhysical = Parameters == TEXT("PhysicalExhaustion");
	if (bPhysical)
	{
		TestEqual(TEXT("Every attack and progress candidate was actually examined"), Query.NextCombatCandidate, Query.CombatCandidates.Num());
		for (const FEnemyOrbitCandidate& Candidate : Query.CombatCandidates)
		{
			TestTrue(TEXT("Each candidate retains actual physical failure evidence"),
				Enemy.IsCombatTacticalCandidateFailure(TEXT("AttackPosition"), Candidate.Destination));
		}
	}
	TestEqual(TEXT("Only complete physical exhaustion grants an environment-blocked attempt"),
		Enemy.PrepareTargetAttackAttempt(0.5f), bPhysical);
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyCombatF6HeightFailureClassificationTest,
	"Outlier.Enemy.Combat.F6.HeightFailureClassification",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyCombatF6HeightFailureClassificationTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("PhysicalHeight"), TEXT("BudgetHeight"), TEXT("UnavailableHeight")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyCombatF6HeightFailureClassificationTest::RunTest(const FString& Parameters)
{
	const bool bPhysical = Parameters == TEXT("PhysicalHeight");
	const bool bBudget = Parameters == TEXT("BudgetHeight");
	FEnemyFlightHeightTestWorld Fixture;
	FEnemyRunEnvQueryTaskInstanceData Query;
	// 바닥 탐색 원점을 천장 아래에 둔다. 후보의 수직 공간만 검사하며 이 시작점으로 이동/사격을 강제하지 않는다.
	if (!Fixture.Initialize(Parameters != TEXT("UnavailableHeight"), FVector(0, 0, 1), FVector(6000, 6000, 2000), true)
		|| !MakeHeightCombatQuery(Fixture, TEXT("Orbit"), Query))
	{
		AddError(TEXT("Height classification fixture could not initialize"));
		return false;
	}
	AEnemyBase& Enemy = *Fixture.Enemy;
	AEnemyAIController& Controller = *CastChecked<AEnemyAIController>(Enemy.GetController());
	Controller.SetEnemyPerceptionEnabled(false);
	Controller.SetActorTickEnabled(false);
	Enemy.GetStateTreeComponent()->StopLogic(TEXT("Controlled F6 physical height proof"));
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APartnerCharacter* Target = Fixture.World->SpawnActor<APartnerCharacter>(APartnerCharacter::StaticClass(),
		FVector(1500, 0, 400), FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("Own directly observed target spawns"), Target))
	{
		return false;
	}
	Target->SetActorEnableCollision(false);
	Controller.SetVisibleTargetsForTesting({Target});
	Enemy.SetPlayerCurrentlyVisible(true);
	Enemy.ObserveTrackedTarget(Target);
	Enemy.RefreshTargetTracking();
	const_cast<FEnemyStat&>(Enemy.GetRuntimeStat()).TacticalSightBlockDuration = 0.0f;
	if (!TestNotNull(TEXT("Dynamic wall blocks bullets without supplying a static floor"),
		Fixture.SpawnBlockingBox(FVector(750, 0, 500), FVector(30, 250, 500), ECC_WorldDynamic)))
	{
		return false;
	}
	if (bPhysical)
	{
		const double CeilingBottom = 2.0 * Fixture.VerticalExtent() - 1.0;
		if (!TestNotNull(TEXT("Actual ceiling leaves no safe candidate capsule height"), Fixture.SpawnBlockingBox(
			FVector(0, 0, CeilingBottom + 25.0), FVector(6000, 6000, 25))))
		{
			return false;
		}
	}
	Enemy.RefreshCombatAimPoint();
	Enemy.UpdateCombatTacticalSightBlock();
	Query.MaxSearchNodes = 4096;
	Query.MaxCollisionChecks = bBudget ? 1 : 16384;
	EnemySearchTesting::CaptureQuery(Query);
	FEnvQueryInstance Result;
	Result.Owner = &Enemy;
	Result.ItemType = UEnvQueryItemType_Point::StaticClass();
	Result.ItemTypeVectorCDO = GetMutableDefault<UEnvQueryItemType_Point>();
	Result.ValueSize = GetDefault<UEnvQueryItemType_Point>()->GetValueSize();
	Result.MarkAsFinishedWithoutIssues();
	Result.AddItemData<UEnvQueryItemType_Point>(FVector(1100, 0, 400));
	EnemySearchTesting::CompleteQuery(Query, Result);
	if (!TestTrue(TEXT("Production unified selection produced the actual height snapshot"),
		Query.bUnifiedCombatNavigation && Query.bCombatSelectionPending && !Query.CombatCandidates.IsEmpty()
		&& Query.HeightSnapshot.Candidates.Num() == Query.CombatCandidates.Num()))
	{
		return false;
	}
	const EFlightPathStatus ExpectedHeight = bPhysical ? EFlightPathStatus::BlockedEndpoint
		: bBudget ? EFlightPathStatus::BudgetExceeded : EFlightPathStatus::Unavailable;
	for (int32 Index = 0; Index < Query.CombatCandidates.Num(); ++Index)
	{
		const FVector Candidate = Query.CombatCandidates[Index].Destination;
		TestEqual(TEXT("Actual collision/budget result retains its distinct height status"),
			Query.HeightSnapshot.Candidates[Index].Status, ExpectedHeight);
		TestEqual(TEXT("Only a completed physical height rejection is cached as failure"),
			Enemy.IsCombatTacticalCandidateFailure(TEXT("AttackPosition"), Candidate), bPhysical);
		TestEqual(TEXT("Unavailable height or budget remains limited"), Query.CombatCandidatesHeightLimited[Index], !bPhysical);
	}
	TestEqual(TEXT("Height proof never starts A* at the unsafe original altitude"), Query.CheckedPathCandidates, 0);
	TestEqual(TEXT("Bounded height-only selection terminates"), EnemySearchTesting::ResumeCombatSelection(Query), EStateTreeRunStatus::Failed);
	TestEqual(TEXT("Physical rejection stays distinct from unknown alternatives"), Query.QueryResult,
		bPhysical ? EEnemyEnvQueryResult::NoReachableDestination : EEnemyEnvQueryResult::PathBudgetExceeded);
	if (!bPhysical)
	{
		TestFalse(TEXT("Unavailable floor or budget never authorizes attempt fire"), Enemy.PrepareTargetAttackAttempt(0.5f));
	}
	return !HasAnyErrors();
}

#endif
