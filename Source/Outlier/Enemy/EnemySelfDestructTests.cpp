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
	if (!TestTrue(TEXT("AI commits direction"), Drone->BeginCommittedSelfDestructDirection(FVector::ForwardVector, 0, 400, 1000))) return false;
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
	FChargeTestWorld Fixture;
	if (!TestNotNull(TEXT("World"), Fixture.World)) return false;
	ASelfDestructDrone* Drone = Fixture.World->SpawnActor<ASelfDestructDrone>();
	AActor* Wall = Fixture.AddBox(FVector(200, 0, 150), FVector(20, 200, 200));
	if (!TestNotNull(TEXT("Drone"), Drone) || !TestNotNull(TEXT("Wall"), Wall)) return false;
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
	Drone->NotifyHit(Capsule, Wall, Cast<UPrimitiveComponent>(Wall->GetRootComponent()), true,
		FVector(180, 0, 150), -FVector::ForwardVector, FVector::ZeroVector, Hit);
	TestFalse(TEXT("Ordinary chase contact does not detonate"), Drone->bDeathHandling);
	if (!TestTrue(TEXT("Charge commits"), Drone->BeginCommittedSelfDestructDirection(FVector::ForwardVector, 1, 400, 1000))) return false;
	Hit.bBlockingHit = false;
	Drone->NotifyHit(Capsule, Wall, Cast<UPrimitiveComponent>(Wall->GetRootComponent()), true,
		FVector(180, 0, 150), -FVector::ForwardVector, FVector::ZeroVector, Hit);
	TestFalse(TEXT("Nonblocking touch does not detonate"), Drone->bDeathHandling);
	Drone->CancelCommittedSelfDestruct();
	Hit.bBlockingHit = true;
	Drone->NotifyHit(Capsule, Wall, Cast<UPrimitiveComponent>(Wall->GetRootComponent()), true,
		FVector(180, 0, 150), -FVector::ForwardVector, FVector::ZeroVector, Hit);
	TestFalse(TEXT("Cancelled charge no longer detonates on contact"), Drone->bDeathHandling);
	if (!TestTrue(TEXT("A new charge can commit after cancellation"),
		Drone->BeginCommittedSelfDestructDirection(FVector::ForwardVector, 1, 400, 1000))) return false;
	// 실제 캡슐 Sweep을 사용한다. 콜백을 직접 호출하는 테스트만으로 이동 연결 완료를 주장하지 않는다.
	Fixture.World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
	Fixture.World->GetPhysicsScene()->Flush();
	Drone->GetCharacterMovement()->MoveUpdatedComponent(FVector(400, 0, 0), FQuat::Identity, true, &Hit);
	TestTrue(TEXT("Charge movement hits the wall"), Hit.bBlockingHit);
	TestTrue(TEXT("Blocking movement immediately enters death/explosion"), Drone->bDeathHandling);
	TestTrue(TEXT("Blocking movement consumes remaining health"), Drone->GetCurrentHealth() <= 0.0f);
	TestTrue(TEXT("Blocking movement submits an actual explosion"), Explosion->HasDetonated());
	TestFalse(TEXT("Detonation clears commitment"), Drone->IsSelfDestructCommitted());
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
