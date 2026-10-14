#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/EnemyStateTreeSelfDestructTasks.h"
#include "Enemy/EnemyEnvQuery.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
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
