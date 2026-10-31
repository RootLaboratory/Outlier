#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/EnemyStateTreeSelfDestructTasks.h"
#include "Enemy/EnemyEnvQuery.h"
#include "Enemy/SelfDestructDrone.h"
#include "Curves/CurveFloat.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AbilitySystemComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Explosion/ExplosionComponent.h"
#include "Misc/AutomationTest.h"
#include "Physics/Experimental/PhysScene_Chaos.h"

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
	enum class ECollisionCase { CloseWall, Pawn, Floor };
	// 근접 환경, Pawn 채널 목표, 바닥을 실제 Sweep으로 검사한다.
	for (const ECollisionCase CollisionCase : {ECollisionCase::CloseWall, ECollisionCase::Pawn, ECollisionCase::Floor})
	{
		FChargeTestWorld Fixture;
		if (!TestNotNull(TEXT("World"), Fixture.World)) return false;
		ASelfDestructDrone* Drone = Fixture.World->SpawnActor<ASelfDestructDrone>();
		const bool bFloor = CollisionCase == ECollisionCase::Floor;
		const FVector TargetLocation = bFloor ? FVector(0, 0, -50)
			: FVector(CollisionCase == ECollisionCase::CloseWall ? 80 : 300, 0, 150);
		AActor* BlockingTarget = Fixture.AddBox(TargetLocation,
			bFloor ? FVector(200, 200, 20) : FVector(20, 200, 200));
		if (!TestNotNull(TEXT("Drone"), Drone) || !TestNotNull(TEXT("BlockingTarget"), BlockingTarget)) return false;
		if (CollisionCase == ECollisionCase::Pawn)
		{
			CastChecked<UPrimitiveComponent>(BlockingTarget->GetRootComponent())->SetCollisionObjectType(ECC_Pawn);
		}
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
		FHitResult Hit;
		Hit.bBlockingHit = true;
		Drone->NotifyHit(Capsule, BlockingTarget, Cast<UPrimitiveComponent>(BlockingTarget->GetRootComponent()), true,
			FVector(180, 0, 150), -FVector::ForwardVector, FVector::ZeroVector, Hit);
		TestFalse(TEXT("Ordinary chase contact does not detonate"), Drone->bDeathHandling);
		if (!TestTrue(TEXT("AI charge commits"), Drone->BeginCommittedSelfDestruct(BlockingTarget, 2, 400))) return false;
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
		if (!TestTrue(TEXT("A new charge can commit after cancellation"),
			Drone->BeginCommittedSelfDestruct(BlockingTarget, 2, 400))) return false;
		// AI가 계산한 속도로 캡슐 Sweep을 수행한다. 목표 앞 정지로 실제 접촉을 놓치는 회귀를 막는다.
		Fixture.World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
		Fixture.World->GetPhysicsScene()->Flush();
		for (int32 Step = 0; Step < 8 && !Drone->bDeathHandling; ++Step)
		{
			Drone->UpdateCommittedSelfDestructMovement(0.1f, 400);
			Drone->GetCharacterMovement()->MoveUpdatedComponent(
				Drone->GetCharacterMovement()->Velocity * 0.1f, FQuat::Identity, true, &Hit);
		}
		TestTrue(TEXT("AI movement hits the blocking target"), Hit.bBlockingHit);
		TestTrue(TEXT("Blocking movement immediately enters death/explosion"), Drone->bDeathHandling);
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
