#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/EnemyStateTreeSelfDestructTasks.h"
#include "Enemy/EnemyEnvQuery.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/SelfDestructDrone.h"
#include "Curves/CurveFloat.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AbilitySystemComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineGlobals.h"
#include "Explosion/ExplosionComponent.h"
#include "Explosion/ExplosiveProp.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sound/SoundWave.h"
#include "Misc/AutomationTest.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Shooter/ShooterCharacter.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "GameplayTags/OutlierGameplayTags.h"

namespace
{
struct FChargeTestWorld
{
	UWorld* World = nullptr;
	FChargeTestWorld()
	{
		if (!GEngine) return;
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("EnemyChargeTest")), GetTransientPackage());
		if (!World) return;
		World->AddToRoot();
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
	}
	~FChargeTestWorld()
	{
		if (!World) return;
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}
	AActor* AddBox(const FVector& Location, const FVector& Extent)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		if (!Actor) return nullptr;
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
		Actor->SetRootComponent(Box);
		Box->SetBoxExtent(Extent);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->RegisterComponent();
		Box->SetWorldLocation(Location);
		World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
		World->GetPhysicsScene()->Flush();
		return Actor;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyChargeTrackingGraceTest,
	"Outlier.Enemy.SelfDestruct.TrackingGrace", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyChargeTrackingGraceTest::RunTest(const FString& Parameters)
{
	FChargeTestWorld Fixture;
	if (!TestNotNull(TEXT("Authority world exists"), Fixture.World)) return false;
	Fixture.World->CreateAISystem();
	UClass* TargetClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	if (!TestNotNull(TEXT("Concrete player class exists"), TargetClass)) return false;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASelfDestructDrone* Drone = Fixture.World->SpawnActor<ASelfDestructDrone>(
		ASelfDestructDrone::StaticClass(), FTransform(FVector(0, 0, 400)), Spawn);
	AShooterCharacter* Target = Fixture.World->SpawnActor<AShooterCharacter>(TargetClass,
		FTransform(FVector(400, 0, 400)), Spawn);
	AEnemyAIController* Controller = Fixture.World->SpawnActor<AEnemyAIController>();
	if (!TestNotNull(TEXT("Drone exists"), Drone) || !TestNotNull(TEXT("Player exists"), Target)
		|| !TestNotNull(TEXT("Controller exists"), Controller)) return false;
	Drone->GetCharacterMovement()->SetComponentTickEnabled(false);
	Target->GetCharacterMovement()->SetComponentTickEnabled(false);
	Target->SetActorEnableCollision(false);
	Drone->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Drone->GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Block);
	Controller->Possess(Drone);
	Drone->EnterCombatFromRoom(Target->GetActorLocation(), false);
	const_cast<FEnemyStat&>(Drone->GetRuntimeStat()).TargetTrackingGraceSeconds = 0.1f;
	Controller->SetCachedVisibleTargetForTesting(Target);
	Drone->ObserveTrackedTarget(Target);
	Drone->SetPlayerCurrentlyVisible(true);
	if (!TestEqual(TEXT("Real player is tracked before sight loss"), Drone->GetTrackedTarget(), static_cast<AActor*>(Target))) return false;
	TestEqual(TEXT("Direct sight can enter charge"), EnemyCharge::Assess(Drone, Controller, Target, 500, 5), EEnemyChargeEligibility::Ready);

	Controller->SetCachedVisibleTargetForTesting(nullptr);
	Drone->SetPlayerCurrentlyVisible(false);
	TestFalse(TEXT("Tracking grace does not fake actual sight"), Drone->IsPlayerCurrentlyVisible());
	TestEqual(TEXT("Valid grace can enter charge without a visible cache"),
		EnemyCharge::Assess(Drone, Controller, Target, 500, 5), EEnemyChargeEligibility::Ready);
	AActor* Wall = Fixture.AddBox(FVector(200, 0, 400), FVector(10, 100, 100));
	if (!TestNotNull(TEXT("Blocking wall exists"), Wall)) return false;
	TestEqual(TEXT("Grace still requires a clear capsule path"),
		EnemyCharge::Assess(Drone, Controller, Target, 500, 5), EEnemyChargeEligibility::BlockedPath);
	Wall->SetActorEnableCollision(false);
	Target->SetActorLocation(FVector(501, 0, 400));
	TestEqual(TEXT("Grace does not extend charge range"),
		EnemyCharge::Assess(Drone, Controller, Target, 500, 5), EEnemyChargeEligibility::OutOfRange);
	Target->SetActorLocation(FVector(400, 0, 400));
	TestEqual(TEXT("Explicit stale target cannot replace the tracked player"),
		EnemyCharge::Assess(Drone, Controller, Wall, 500, 5), EEnemyChargeEligibility::NoVisibleTarget);

	// 만료 콜백 실행 여부와 무관하게 현재 월드 시각으로 Commit 자격을 거부해야 한다.
	// 오래된 Sight 캐시와 전이에서 전달된 타깃을 함께 남겨도 유예를 연장할 수 없다.
	Controller->SetCachedVisibleTargetForTesting(Target);
	for (int32 Step = 0; Step < 8; ++Step)
	{
		++GFrameCounter;
		Fixture.World->Tick(LEVELTICK_TimeOnly, 1.0f / 60.0f);
	}
	TestNull(TEXT("Tracking expires before a new charge can commit"), Drone->GetTrackedTarget());
	TestEqual(TEXT("Expired tracking rejects an explicit target and stale visible cache"),
		EnemyCharge::Assess(Drone, Controller, Target, 500, 5), EEnemyChargeEligibility::NoVisibleTarget);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyChargePresentationStateTest,
	"Outlier.Enemy.SelfDestruct.PresentationState", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyChargePresentationStateTest::RunTest(const FString& Parameters)
{
	USoundWave* Sound = LoadObject<USoundWave>(nullptr,
		TEXT("/Game/Audio/Raw/Enemy/Enemy_Drone_Explode_Sign.Enemy_Drone_Explode_Sign"));
	if (!TestNotNull(TEXT("Canonical charge sound"), Sound)) return false;
	TestTrue(TEXT("Sound uses the provided full-length source"), FMath::IsNearlyEqual(Sound->Duration, 4.029208f, 0.002f));
	TestFalse(TEXT("Charge wave does not loop"), Sound->bLooping);
	FChargeTestWorld Fixture;
	if (!TestNotNull(TEXT("World"), Fixture.World)) return false;
	ASelfDestructDrone* Drone = Fixture.World->SpawnActor<ASelfDestructDrone>();
	if (!TestNotNull(TEXT("Drone"), Drone)) return false;
	TestTrue(TEXT("Server metadata matches the imported sound"), FMath::IsNearlyEqual(Drone->ChargeSoundDuration, Sound->Duration, 0.002f));
	TestTrue(TEXT("Direction commit succeeds"), Drone->BeginCommittedSelfDestructDirection(FVector::ForwardVector, 0, 400, 500));
	TestTrue(TEXT("Commit locks gameplay before presentation duration is known"), Drone->IsSelfDestructCommitted());
	TestFalse(TEXT("Zero duration does not play audio"), Drone->ChargeState.Playback.IsValid());
	const uint32 CommitRevision = Drone->ChargeState.Revision;
	Drone->SetCommittedChargeDuration(1.0f);
	TestTrue(TEXT("Actual countdown publishes a new revision"), Drone->ChargeState.Revision > CommitRevision);
	TestEqual(TEXT("Countdown uses update duration"), Drone->ChargeState.Playback.Duration, 1.0f);
	TestTrue(TEXT("Countdown has a supported audio window"), Drone->ChargeState.Playback.IsValid());
	const uint32 ActiveRevision = Drone->ChargeState.Revision;
	Drone->CancelCommittedSelfDestruct();
	TestFalse(TEXT("Cancel clears shared active state"), Drone->IsSelfDestructCommitted());
	TestTrue(TEXT("Cancel invalidates the previous revision"), Drone->ChargeState.Revision > ActiveRevision);
	Drone->SetCommittedChargeDuration(2.0f);
	TestFalse(TEXT("Stale update cannot restart canceled charge"), Drone->IsSelfDestructCommitted());
	TestTrue(TEXT("Next charge can commit"), Drone->BeginCommittedSelfDestructDirection(FVector::ForwardVector, 2, 400, 500));
	Drone->PrepareForPoolIdle();
	TestFalse(TEXT("Pool idle ends charge"), Drone->IsSelfDestructCommitted());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyChargeEmissiveMaterialTest,
	"Outlier.Enemy.SelfDestruct.EmissiveMaterial", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyChargeEmissiveMaterialTest::RunTest(const FString& Parameters)
{
	FChargeTestWorld Fixture;
	if (!TestNotNull(TEXT("World"), Fixture.World)) return false;
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr,
		TEXT("/Game/Characters/Enemy/VECDrone/Meshes/SM_SuicideDroneBarrel.SM_SuicideDroneBarrel"));
	if (!TestNotNull(TEXT("Actual mounted explosive mesh"), Mesh)) return false;
	AExplosiveProp* First = Fixture.World->SpawnActor<AExplosiveProp>();
	AExplosiveProp* Second = Fixture.World->SpawnActor<AExplosiveProp>();
	if (!TestNotNull(TEXT("First explosive"), First) || !TestNotNull(TEXT("Second explosive"), Second)) return false;
	for (AExplosiveProp* Prop : {First, Second})
	{
		Prop->ExplosiveMesh->SetStaticMesh(Mesh);
		Prop->FirstPersonExplosiveMesh->SetStaticMesh(Mesh);
		Prop->InitializeChargeEmissive();
		if (!TestNotNull(TEXT("Actual material supports charge emissive"), Prop->ChargeEmissiveMID.Get())) return false;
		TestTrue(TEXT("First and third person share the same MID"),
			Prop->ExplosiveMesh->GetMaterial(0) == Prop->FirstPersonExplosiveMesh->GetMaterial(0));
	}
	TestTrue(TEXT("Different explosives have independent MIDs"), First->ChargeEmissiveMID != Second->ChargeEmissiveMID);
	float Strength = 0.0f;
	// 2초 차징처럼 사운드 중간에서 시작해도 기본 밝기에서 먼저 소등한다.
	const float SoundOffset = 4.029208f - 2.0f;
	First->ApplyChargeEmissive(SoundOffset, 0.0f);
	First->ChargeEmissiveMID->GetScalarParameterValue(FMaterialParameterInfo(First->EmissiveParameterName), Strength);
	TestEqual(TEXT("Charge starts at the material default"), Strength, First->IdleEmissiveStrength);
	First->ApplyChargeEmissive(SoundOffset + 0.04f, 0.04f);
	First->ChargeEmissiveMID->GetScalarParameterValue(FMaterialParameterInfo(First->EmissiveParameterName), Strength);
	TestTrue(TEXT("Initial fade lowers the default brightness"), FMath::IsNearlyEqual(Strength, First->IdleEmissiveStrength * 0.5f));
	// 마지막 소등 키 뒤에는 최고 밝기로 다시 상승한다. 3.94f 리터럴은
	// 3.86f + 0.08f 키보다 조금 뒤이므로 커브 생성과 같은 계산으로 경계를 검사한다.
	const float LastPulseEnd = 3.86f + 0.08f;
	for (const float SoundTime : {2.2f, 2.5f, 3.65f, LastPulseEnd})
	{
		First->ApplyChargeEmissive(SoundTime, SoundTime - SoundOffset);
		First->ChargeEmissiveMID->GetScalarParameterValue(FMaterialParameterInfo(First->EmissiveParameterName), Strength);
		TestTrue(FString::Printf(TEXT("Pulse valley at %.9f seconds turns fully off (strength %.9f)"),
			SoundTime, Strength), FMath::IsNearlyZero(Strength));
	}
	First->ApplyChargeEmissive(2.65f, 2.65f - SoundOffset);
	First->ChargeEmissiveMID->GetScalarParameterValue(FMaterialParameterInfo(First->EmissiveParameterName), Strength);
	TestTrue(TEXT("A warning pulse raises emissive"), Strength > First->IdleEmissiveStrength);
	Second->ChargeEmissiveMID->GetScalarParameterValue(FMaterialParameterInfo(Second->EmissiveParameterName), Strength);
	TestEqual(TEXT("Other explosive remains idle"), Strength, Second->IdleEmissiveStrength);
	First->ApplyChargeEmissive(4.029208f, 2.0f);
	First->ChargeEmissiveMID->GetScalarParameterValue(FMaterialParameterInfo(First->EmissiveParameterName), Strength);
	TestEqual(TEXT("Final pulse holds peak brightness"), Strength, First->PeakEmissiveStrength);
	First->ResetChargeEmissive();
	First->ChargeEmissiveMID->GetScalarParameterValue(FMaterialParameterInfo(First->EmissiveParameterName), Strength);
	TestEqual(TEXT("Cancel restores material default"), Strength, First->IdleEmissiveStrength);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyChargeRangeTest,
	"Outlier.Enemy.SelfDestruct.ChargeRange", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyChargeRangeTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Five metre boundary is inclusive"), EnemyCharge::IsWithinChargeRange(FVector::ZeroVector, FVector(500, 0, 0), 500));
	TestFalse(TEXT("Beyond five metres cannot charge"), EnemyCharge::IsWithinChargeRange(FVector::ZeroVector, FVector(500.1, 0, 0), 500));
	TestTrue(TEXT("Three dimensional boundary is inclusive"), EnemyCharge::IsWithinChargeRange(FVector::ZeroVector, FVector(300, 0, 400), 500));
	TestFalse(TEXT("Horizontal range alone does not permit charging"), EnemyCharge::IsWithinChargeRange(FVector::ZeroVector, FVector(300, 0, 401), 500));
	TestFalse(TEXT("Invalid entry distance is rejected"), EnemyCharge::IsWithinChargeRange(FVector::ZeroVector, FVector::ZeroVector, 0));
	TestFalse(TEXT("Editor configuration cannot expand the spec boundary"), EnemyCharge::IsWithinChargeRange(FVector::ZeroVector, FVector(600, 0, 0), 600));
	TestEqual(TEXT("Invalid drone cannot charge"), EnemyCharge::Assess(nullptr, nullptr, nullptr, 500, 5), EEnemyChargeEligibility::Invalid);
#if WITH_EDITORONLY_DATA
	TestFalse(TEXT("Assessment does not prevent child completion"), FEnemySelfDestructAssessmentTask().bConsideredForCompletion);
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyChargeSpeedProfileTest,
	"Outlier.Enemy.SelfDestruct.SpeedProfile", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyChargeSpeedProfileTest::RunTest(const FString& Parameters)
{
	FChargeTestWorld Fixture;
	if (!TestNotNull(TEXT("World"), Fixture.World)) return false;
	ASelfDestructDrone* Drone = Fixture.World->SpawnActor<ASelfDestructDrone>();
	if (!TestNotNull(TEXT("Drone"), Drone)) return false;
	UCurveFloat* Curve = NewObject<UCurveFloat>(Drone);
	Curve->FloatCurve.AddKey(0, 0);
	Curve->FloatCurve.AddKey(1, 1);
	Drone->AIChargeSpeedProfile.AccelerationCurve = Curve;
	Drone->AIChargeSpeedProfile.AccelerationDuration = 1.0f;
	Drone->AIChargeSpeedProfile.DecelerationCurve = Curve;
	Drone->AIChargeSpeedProfile.DecelerationDistance = 10000;
	UCharacterMovementComponent* Movement = Drone->GetCharacterMovement();
	Movement->Velocity = FVector::ZeroVector;
	AActor* Target = Fixture.AddBox(Drone->GetActorLocation() + FVector(300, 0, 0), FVector(10));
	if (!TestNotNull(TEXT("Target"), Target)) return false;
	Target->SetActorEnableCollision(false);
	if (!TestTrue(TEXT("AI commits toward target"), Drone->BeginCommittedSelfDestruct(Target, 2, 400))) return false;
	Drone->UpdateCommittedSelfDestructMovement(0.25f, 400);
	TestTrue(TEXT("AI accelerates without applying approach braking"), Movement->Velocity.Equals(FVector(100, 0, 0)));
	Drone->CancelCommittedSelfDestruct();
	TestTrue(TEXT("Cancel immediately stops charge"), Movement->Velocity.IsNearlyZero());
	TestEqual(TEXT("Cancel clears profile clock"), Drone->ChargeSpeedState.TargetSpeed, -1.0f);
	Drone->SetEnemyPossessed(true);
	if (!TestTrue(TEXT("Possessed drone can commit"), Drone->BeginCommittedSelfDestructDirection(FVector::ForwardVector, 0, 400, 1000))) return false;
	Drone->UpdateCommittedSelfDestructMovement(0.25f, 400);
	TestTrue(TEXT("Possessed charge keeps original speed"), Movement->Velocity.Equals(FVector(400, 0, 0)));
	Drone->CancelCommittedSelfDestruct();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyChargeContinuousMovementTest,
	"Outlier.Enemy.SelfDestruct.ContinuousMovement", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyChargeContinuousMovementTest::RunTest(const FString& Parameters)
{
	FChargeTestWorld Fixture;
	if (!TestNotNull(TEXT("World"), Fixture.World)) return false;
	ASelfDestructDrone* Drone = Fixture.World->SpawnActor<ASelfDestructDrone>();
	AActor* Target = Fixture.AddBox(FVector(300, 0, 150), FVector(10));
	if (!TestNotNull(TEXT("Drone"), Drone) || !TestNotNull(TEXT("Target"), Target)) return false;
	Target->SetActorEnableCollision(false);
	UCharacterMovementComponent* Movement = Drone->GetCharacterMovement();
	if (!TestNotNull(TEXT("Movement"), Movement)) return false;

	// 근거리와 기존 정지 지점을 포함해 같은 드론을 재커밋하며 이전 제한이 남지 않는지도 확인한다.
	for (const float TargetDistance : {50.0f, 300.0f})
	{
		Drone->SetActorLocation(FVector(0, 0, 150));
		Target->SetActorLocation(FVector(TargetDistance, 0, 150));
		if (!TestTrue(TEXT("AI commits at close range"), Drone->BeginCommittedSelfDestruct(Target, 2, 400))) return false;
		// 목표가 회피해도 다시 추적하지 않고 진입 때 확정한 방향으로 계속 이동한다.
		Target->SetActorLocation(FVector(1000, 1000, 150));
		for (int32 Step = 0; Step < 8; ++Step)
		{
			if (!TestTrue(TEXT("AI charge remains active"), Drone->UpdateCommittedSelfDestructMovement(0.25f, 400))) return false;
			TestTrue(TEXT("AI never brakes at the initial target or old distance limit"),
				Movement->Velocity.Equals(FVector(400, 0, 0)));
			Movement->MoveUpdatedComponent(Movement->Velocity * 0.25f, Drone->GetActorQuat(), true);
		}
		TestTrue(TEXT("AI moves beyond five metres while the task owns the countdown"),
			Drone->GetActorLocation().Equals(FVector(800, 0, 150), 0.1f));
		Drone->CancelCommittedSelfDestruct();
		TestTrue(TEXT("Cancel clears movement"), Movement->Velocity.IsNearlyZero());
	}

	// 빙의 경로는 5m 경계 직전 프레임부터 속도를 제한하고, 경계에서 전진을 멈춘다.
	Drone->SetActorLocation(FVector(0, 0, 150));
	Drone->SetEnemyPossessed(true);
	if (!TestTrue(TEXT("Possessed charge commits"),
		Drone->BeginCommittedSelfDestructDirection(FVector::ForwardVector, 2, 400, 500))) return false;
	Drone->SetActorLocation(FVector(450, 0, 150));
	Drone->UpdateCommittedSelfDestructMovement(0.25f, 400);
	TestTrue(TEXT("Possessed charge clamps the last step to the remaining distance"),
		Movement->Velocity.Equals(FVector(200, 0, 0)));
	Movement->MoveUpdatedComponent(Movement->Velocity * 0.25f, Drone->GetActorQuat(), true);
	Drone->UpdateCommittedSelfDestructMovement(0.25f, 400);
	TestTrue(TEXT("Possessed charge stops at five metres"), Movement->Velocity.IsNearlyZero());
	Drone->CancelCommittedSelfDestruct();
	Drone->SetEnemyPossessed(false);
	Target->SetActorLocation(Drone->GetActorLocation() + FVector(50, 0, 0));
	if (!TestTrue(TEXT("AI can commit after a limited charge"), Drone->BeginCommittedSelfDestruct(Target, 2, 400))) return false;
	Movement->MoveUpdatedComponent(FVector(600, 0, 0), Drone->GetActorQuat(), true);
	Drone->UpdateCommittedSelfDestructMovement(0.25f, 400);
	TestTrue(TEXT("Previous possessed limit does not leak into the next AI charge"),
		Movement->Velocity.Equals(FVector(400, 0, 0)));
	Drone->CancelCommittedSelfDestruct();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyChargeSweepTest,
	"Outlier.Enemy.SelfDestruct.StraightPath", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyChargeSweepTest::RunTest(const FString& Parameters)
{
	FChargeTestWorld Fixture;
	if (!TestNotNull(TEXT("Physics world"), Fixture.World)) return false;
	AActor* Owner = Fixture.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Capsule owner"), Owner)) return false;
	UCapsuleComponent* Capsule = NewObject<UCapsuleComponent>(Owner);
	Owner->SetRootComponent(Capsule);
	Capsule->SetCapsuleSize(30, 50);
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Capsule->SetCollisionObjectType(ECC_Pawn);
	Capsule->SetCollisionResponseToAllChannels(ECR_Block);
	Capsule->RegisterComponent();
	Capsule->SetWorldLocation(FVector(0, 0, 150));
	AActor* Target = Fixture.AddBox(FVector(400, 0, 150), FVector(30));
	if (!TestNotNull(TEXT("Blocking target"), Target)) return false;
	TestTrue(TEXT("Intended target collision is allowed"), EnemyCharge::IsStraightPathClear(*Fixture.World, *Capsule, *Target, 5));
	AActor* Wall = Fixture.AddBox(FVector(200, 0, 150), FVector(20, 200, 200));
	if (!TestNotNull(TEXT("Wall"), Wall)) return false;
	TestFalse(TEXT("Intervening wall prevents entry"), EnemyCharge::IsStraightPathClear(*Fixture.World, *Capsule, *Target, 5));
	Wall->SetActorEnableCollision(false);
	TestTrue(TEXT("Nonblocking contact permits entry"), EnemyCharge::IsStraightPathClear(*Fixture.World, *Capsule, *Target, 5));
	Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TestFalse(TEXT("Spawn presentation without collision cannot charge"), EnemyCharge::IsStraightPathClear(*Fixture.World, *Capsule, *Target, 5));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyChargeCollisionTest,
	"Outlier.Enemy.SelfDestruct.CollisionDetonation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyChargeCollisionTest::RunTest(const FString& Parameters)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	enum class ECollisionCase { CloseWall, Pawn, Floor, Enemy, Shooter, Partner, PossessedEnemy, ReleasedEnemy, NormalDeath };
	// 실제 이동 Sweep으로 환경/일반 AI와 플레이어 세 종류를 구분한다. Pawn 채널 자체는 자격이 아니다.
	for (const ECollisionCase CollisionCase : {ECollisionCase::CloseWall, ECollisionCase::Pawn, ECollisionCase::Floor,
		ECollisionCase::Enemy, ECollisionCase::Shooter, ECollisionCase::Partner, ECollisionCase::PossessedEnemy,
		ECollisionCase::ReleasedEnemy, ECollisionCase::NormalDeath})
	{
		FChargeTestWorld Fixture;
		if (!TestNotNull(TEXT("World"), Fixture.World)) return false;
		AddInfo(FString::Printf(TEXT("[ChargeCollision] Case=%d"), static_cast<int32>(CollisionCase)));
		ASelfDestructDrone* Drone = Fixture.World->SpawnActor<ASelfDestructDrone>();
		const bool bFloor = CollisionCase == ECollisionCase::Floor;
		const FVector TargetLocation = bFloor ? FVector(0, 0, -50)
			: FVector(CollisionCase == ECollisionCase::CloseWall ? 80 : 300, 0, 150);
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* BlockingTarget = nullptr;
		const bool bPlayerContact = CollisionCase == ECollisionCase::Shooter || CollisionCase == ECollisionCase::Partner
			|| CollisionCase == ECollisionCase::PossessedEnemy;
		if (CollisionCase == ECollisionCase::Shooter || CollisionCase == ECollisionCase::Partner)
		{
			UClass* PlayerClass = LoadClass<ACharacter>(nullptr, CollisionCase == ECollisionCase::Shooter
				? TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C")
				: TEXT("/Game/Blueprints/Partner/BP_PartnerCharacter.BP_PartnerCharacter_C"));
			if (!TestNotNull(TEXT("Concrete player class loads"), PlayerClass)) return false;
			BlockingTarget = Fixture.World->SpawnActor<ACharacter>(PlayerClass, FTransform(TargetLocation), Spawn);
		}
		else if (CollisionCase == ECollisionCase::Enemy || CollisionCase == ECollisionCase::PossessedEnemy
			|| CollisionCase == ECollisionCase::ReleasedEnemy)
		{
			AEnemyBase* TargetEnemy = Fixture.World->SpawnActor<AEnemyBase>(
				AEnemyBase::StaticClass(), FTransform(TargetLocation), Spawn);
			if (!TestNotNull(TEXT("Enemy collision target spawns"), TargetEnemy)) return false;
			TargetEnemy->SetEnemyPossessed(CollisionCase != ECollisionCase::Enemy);
			if (CollisionCase == ECollisionCase::ReleasedEnemy) TargetEnemy->SetEnemyPossessed(false);
			BlockingTarget = TargetEnemy;
		}
		else if (CollisionCase == ECollisionCase::Pawn)
		{
			BlockingTarget = Fixture.World->SpawnActor<APawn>();
			if (!TestNotNull(TEXT("Plain Pawn spawns"), BlockingTarget)) return false;
			UBoxComponent* Box = NewObject<UBoxComponent>(BlockingTarget);
			BlockingTarget->SetRootComponent(Box);
			Box->SetBoxExtent(FVector(20, 200, 200));
			Box->RegisterComponent();
			BlockingTarget->SetActorLocation(TargetLocation);
		}
		else BlockingTarget = Fixture.AddBox(TargetLocation,
			bFloor ? FVector(200, 200, 20) : FVector(20, 200, 200));
		if (!TestNotNull(TEXT("Drone"), Drone) || !TestNotNull(TEXT("BlockingTarget"), BlockingTarget)) return false;
		BlockingTarget->SetActorEnableCollision(true);
		BlockingTarget->SetActorLocation(TargetLocation);
		BlockingTarget->SetActorTickEnabled(false);
		if (ACharacter* Character = Cast<ACharacter>(BlockingTarget))
		{
			Character->GetCharacterMovement()->SetComponentTickEnabled(false);
			Character->GetCapsuleComponent()->SetCapsuleSize(30, 50);
			// 플레이어 BP의 보조 충돌이 결과를 가리지 않도록 본체 접촉만 검사한다.
			TInlineComponentArray<UPrimitiveComponent*> Primitives(Character);
			for (UPrimitiveComponent* Primitive : Primitives) Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		UPrimitiveComponent* TargetRoot = Cast<UPrimitiveComponent>(BlockingTarget->GetRootComponent());
		if (!TestNotNull(TEXT("Collision target has a primitive root"), TargetRoot)) return false;
		TargetRoot->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		TargetRoot->SetCollisionResponseToAllChannels(ECR_Block);
		if (CollisionCase == ECollisionCase::Pawn) TargetRoot->SetCollisionObjectType(ECC_Pawn);
		// BP/DataTable이 없는 네이티브 테스트 액터에도 살아 있는 체력과 유효한 폭발 설정을 준비한다.
		// BeginPlay 이후에 체력을 보정하면 초기 체력 0으로 이미 사망한 상태를 되돌리게 된다.
		Drone->RuntimeStat.Health = 100.0f;
		UExplosionComponent* Explosion = Drone->GetExplosionComponent();
		if (!TestNotNull(TEXT("Explosion component"), Explosion)) return false;
		UDataTable* ExplosionTable = NewObject<UDataTable>(Drone);
		ExplosionTable->RowStruct = FExplosionProfileRow::StaticStruct();
		ExplosionTable->AddRow(TEXT("CollisionTest"), FExplosionProfileRow());
		FDataTableRowHandle ExplosionRow;
		ExplosionRow.DataTable = ExplosionTable;
		ExplosionRow.RowName = TEXT("CollisionTest");
		Explosion->SetExplosionProfileRow(ExplosionRow);
		// 이 테스트는 충돌/피해/폭발 요청을 검증하며 비동기 GameplayCue 연출은 실행하지 않는다.
		if (UAbilitySystemComponent* AbilitySystem = Drone->GetAbilitySystemComponent())
			AbilitySystem->bSuppressGameplayCues = true;
		// 충돌 후 피해가 사망으로 이어지려면 BeginPlay의 GAS 초기화/체력 감시가 먼저 실행되어야 한다.
		Drone->DispatchBeginPlay();
		if (!TestTrue(TEXT("Drone begins play before collision"), Drone->HasActorBegunPlay())
			|| !TestTrue(TEXT("Damage receiver has initialized GAS actor info"),
				Drone->GetAbilitySystemComponent() && Drone->GetAbilitySystemComponent()->GetAvatarActor() == Drone)
			|| !TestTrue(TEXT("Drone starts with positive health"), Drone->GetCurrentHealth() > 0.0f)) return false;
		UCapsuleComponent* Capsule = Drone->GetCapsuleComponent();
		Capsule->SetCapsuleSize(30, 50);
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Capsule->SetCollisionResponseToAllChannels(ECR_Block);
		Drone->SetActorLocation(FVector(0, 0, 150));
		if (CollisionCase == ECollisionCase::NormalDeath)
		{
			// 충돌 필터와 무관하게 일반 치명 피해도 기존 사망 폭발을 실행해야 한다.
			FOutlierDamageRequest Damage;
			Damage.DamageAmount = 1000.0f;
			Damage.DamageTag = OutlierGameplayTags::Damage::Explosion();
			Drone->ReceiveOutlierDamage(Damage);
			TestTrue(TEXT("Ordinary lethal damage kills an uncommitted drone"), Drone->IsDead());
			TestTrue(TEXT("Ordinary death retains the actual explosion"), Explosion->HasDetonated());
			continue;
		}
		FHitResult Hit;
		Hit.bBlockingHit = true;
		Drone->NotifyHit(Capsule, BlockingTarget, Cast<UPrimitiveComponent>(BlockingTarget->GetRootComponent()), true,
			FVector(180, 0, 150), -FVector::ForwardVector, FVector::ZeroVector, Hit);
		TestFalse(TEXT("Ordinary chase contact does not detonate"), Drone->bDeathHandling);
		if (!TestTrue(TEXT("AI charge commits"), Drone->BeginCommittedSelfDestruct(BlockingTarget, 2, 400))) return false;
		Drone->NotifyHit(Drone->GetMesh(), BlockingTarget, TargetRoot, true,
			Drone->GetActorLocation(), -FVector::ForwardVector, FVector::ZeroVector, Hit);
		TestFalse(TEXT("Mesh contact cannot replace the actual capsule collision"), Drone->bDeathHandling);
		AActor* AttachedActor = Fixture.AddBox(FVector(5000, 0, 150), FVector(10));
		if (!TestNotNull(TEXT("Attached actor"), AttachedActor)) return false;
		AttachedActor->AttachToActor(Drone, FAttachmentTransformRules::KeepWorldTransform);
		Drone->NotifyHit(Capsule, AttachedActor, Cast<UPrimitiveComponent>(AttachedActor->GetRootComponent()), true,
			Drone->GetActorLocation(), -FVector::ForwardVector, FVector::ZeroVector, Hit);
		TestFalse(TEXT("Attached actor without an owner does not detonate the drone"), Drone->bDeathHandling);
		Hit.bBlockingHit = false;
		Drone->NotifyHit(Capsule, BlockingTarget, Cast<UPrimitiveComponent>(BlockingTarget->GetRootComponent()), true,
			FVector(180, 0, 150), -FVector::ForwardVector, FVector::ZeroVector, Hit);
		TestFalse(TEXT("Nonblocking touch does not detonate"), Drone->bDeathHandling);
		Drone->CancelCommittedSelfDestruct();
		Hit.bBlockingHit = true;
		Drone->NotifyHit(Capsule, BlockingTarget, Cast<UPrimitiveComponent>(BlockingTarget->GetRootComponent()), true,
			FVector(180, 0, 150), -FVector::ForwardVector, FVector::ZeroVector, Hit);
		TestFalse(TEXT("Cancelled charge no longer detonates on contact"), Drone->bDeathHandling);
		// 선택한 목표와 다른 플레이어도 실제 접촉하면 폭발 대상이다. 목표는 장애물 뒤의 별도 Actor로 둔다.
		AActor* ChargeTarget = Fixture.AddBox(TargetLocation + (TargetLocation - Drone->GetActorLocation()) * 2, FVector(1));
		if (!TestNotNull(TEXT("Separate committed target exists"), ChargeTarget)) return false;
		ChargeTarget->SetActorEnableCollision(false);
		if (!TestTrue(TEXT("A new charge can commit after cancellation"),
			Drone->BeginCommittedSelfDestruct(ChargeTarget, 2, 400))) return false;
		// AI가 계산한 속도로 캡슐 Sweep을 수행한다. 목표 앞 정지로 실제 접촉을 놓치는 회귀를 막는다.
		Fixture.World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
		Fixture.World->GetPhysicsScene()->Flush();
		bool bHitTarget = false;
		for (int32 Step = 0; Step < 8 && !Drone->bDeathHandling; ++Step)
		{
			Drone->UpdateCommittedSelfDestructMovement(0.1f, 400);
			// 앞선 수동 NotifyHit 값이 실제 Sweep의 성공으로 오인되지 않도록 매번 비운다.
			Hit = FHitResult();
			Drone->GetCharacterMovement()->MoveUpdatedComponent(
				Drone->GetCharacterMovement()->Velocity * 0.1f, FQuat::Identity, true, &Hit);
			bHitTarget |= Hit.bBlockingHit && Hit.GetActor() == BlockingTarget;
		}
		TestTrue(TEXT("Actual AI movement hits the intended blocking target"), bHitTarget);
		if (!bPlayerContact)
		{
			TestFalse(TEXT("Environment and nonplayer contact cannot detonate"), Explosion->HasDetonated());
			TestFalse(TEXT("Environment and nonplayer contact leaves the drone alive"), Drone->IsDead());
			TestTrue(TEXT("Nonplayer blocking contact retains the committed charge"), Drone->IsSelfDestructCommitted());
			TestTrue(TEXT("Actual sweep cannot pass through the blocking target"),
				FVector::DotProduct(Drone->GetActorLocation() - TargetLocation,
					(TargetLocation - FVector(0, 0, 150)).GetSafeNormal()) < 0.0f);
			// 실제 시간 만료 전이는 통합 테스트에서 검사한다. 여기서는 공통 폭발 요청 경로의 보존을 확인한다.
			Drone->TriggerSelfDestruct();
		}
		TestTrue(TEXT("Player contact or explicit detonation enters death/explosion"), Drone->bDeathHandling);
		TestTrue(TEXT("Blocking movement consumes remaining health"), Drone->GetCurrentHealth() <= 0.0f);
		TestTrue(TEXT("Blocking movement submits an actual explosion"), Explosion->HasDetonated());
		TestFalse(TEXT("Detonation clears commitment"), Drone->IsSelfDestructCommitted());
		Drone->TriggerSelfDestruct();
		Drone->NotifyHit(Capsule, BlockingTarget, Cast<UPrimitiveComponent>(BlockingTarget->GetRootComponent()), true,
			Drone->GetActorLocation(), -FVector::ForwardVector, FVector::ZeroVector, Hit);
		TestFalse(TEXT("Collision and timeout requests cannot enqueue a second explosion"),
			Explosion->DetonateAt(Drone->GetActorLocation()));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyChaseCandidatesTest,
	"Outlier.Enemy.EQS.ChaseCandidates", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyChaseCandidatesTest::RunTest(const FString& Parameters)
{
	const FVector Target(1000, 0, 100);
	TArray<FVector> Candidates;
	EnemyChase::BuildCandidates(FVector(0, 0, 300), Target, -1, Candidates);
	if (!TestTrue(TEXT("Small bounded candidate set"), Candidates.Num() > 0 && Candidates.Num() <= 27)) return false;
	TestTrue(TEXT("Left lane is preferred"), Candidates[0].Y < 0);
	for (const FVector& Candidate : Candidates)
	{
		TestTrue(TEXT("Lateral offset never exceeds 1.5 metres"), FMath::Abs(Candidate.Y) <= 150);
		TestTrue(TEXT("Approach endpoint is inside charge range"), EnemyCharge::IsWithinChargeRange(Candidate, Target, 500));
	}
	EnemyChase::BuildCandidates(FVector(700, 0, 300), Target, -1, Candidates);
	TestTrue(TEXT("Near target lateral and height biases disappear"), Candidates[0].Equals(FVector(650, 0, 100)));
	EnemyChase::BuildCandidates(FVector(0, 0, 300), Target, 1, Candidates);
	TestTrue(TEXT("Right lane is preferred"), Candidates[0].Y > 0);
	EnemyChase::BuildCandidates(FVector(0, 0, 300), Target, 0, Candidates);
	TestEqual(TEXT("Centre lane is preferred"), Candidates[0].Y, 0.0);
	EnemyChase::BuildCandidates(FVector::ZeroVector, Target, 2, Candidates);
	TestTrue(TEXT("Invalid lane does not generate destinations"), Candidates.IsEmpty());
	return true;
}

#endif
