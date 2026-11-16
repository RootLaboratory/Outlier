#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Drone/Partner/PartnerHackComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Interaction/ActorBodyTargeting.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorBodyTargetingTest, "Outlier.Interaction.BodyTargeting.HackRegression",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FActorBodyTargetingTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("BodyTargetingTest")));
	if (!TestNotNull(TEXT("World exists"), World))
	{
		return false;
	}
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};

	AActor* Owner = World->SpawnActor<AActor>();
	UPartnerHackComponent* Hack = NewObject<UPartnerHackComponent>(Owner);
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Cube exists"), Cube))
	{
		return false;
	}
	const auto AddBody = [&](AActor* Actor, double X)
	{
		UStaticMeshComponent* Body = NewObject<UStaticMeshComponent>(Actor);
		Actor->AddInstanceComponent(Body);
		Actor->SetRootComponent(Body);
		Body->SetStaticMesh(Cube);
		Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Body->RegisterComponent();
		Body->SetWorldLocation(FVector(X, 0.0, 0.0));
		return Body;
	};
	AActor* First = World->SpawnActor<AActor>();
	AActor* Second = World->SpawnActor<AActor>();
	UStaticMeshComponent* FirstBody = AddBody(First, 200.0);
	UStaticMeshComponent* SecondBody = AddBody(Second, 400.0);
	Hack->HackCandidateActors = {First, Second};
	const auto Select = [&]()
	{
		return Hack->ResolveCursorHackTargetAlongRay(FVector::ZeroVector, FVector::ForwardVector);
	};
	TestEqual(TEXT("Nearest precise body wins with NoCollision"), Select(), First);
	SecondBody->SetWorldLocation(FirstBody->GetComponentLocation());
	Hack->HackCandidateActors = {Second, First};
	TestEqual(TEXT("Hack preserves first encountered candidate on equal hits"), Select(), Second);
	SecondBody->SetWorldLocation(FVector(400.0, 0.0, 0.0));
	Hack->HackCandidateActors = {First, Second};

	// 실제 충돌이 없는 메시를 사용해 Bounds 경로와 정밀 Hit 우선순위를 구분한다.
	UStaticMesh* BoundsCube = DuplicateObject<UStaticMesh>(Cube, GetTransientPackage());
	BoundsCube->SetBodySetup(nullptr);
	FirstBody->SetStaticMesh(BoundsCube);
	TestEqual(TEXT("Farther precise body beats nearer Bounds"), Select(), Second);
	Hack->HackCandidateActors = {Second, First};
	TestEqual(TEXT("Later Bounds cannot replace an earlier precise hit"), Select(), Second);
	Hack->HackCandidateActors = {First, Second};
	SecondBody->SetStaticMesh(BoundsCube);
	TestEqual(TEXT("Nearest Bounds body wins"), Select(), First);
	SecondBody->SetWorldLocation(FirstBody->GetComponentLocation());
	Hack->HackCandidateActors = {Second, First};
	TestEqual(TEXT("Hack also preserves first candidate on equal Bounds"), Select(), Second);
	SecondBody->SetWorldLocation(FVector(400.0, 0.0, 0.0));
	Hack->HackCandidateActors = {First};

	FirstBody->SetHiddenInGame(true);
	TestNull(TEXT("Default hidden mesh is excluded"), Select());
	FirstBody->SetHiddenInGame(false);
	FirstBody->ComponentTags.Add(TEXT("Outlier.StencilExclude"));
	TestNull(TEXT("Stencil-excluded default mesh is excluded"), Select());
	FirstBody->ComponentTags.Reset();
	FirstBody->ComponentTags.Add(TEXT("HackCursorIgnore"));
	TestNull(TEXT("Ignored default mesh is excluded"), Select());
	FirstBody->ComponentTags.Reset();
	First->SetActorHiddenInGame(true);
	TestNull(TEXT("Hidden candidate actor is excluded"), Select());
	First->SetActorHiddenInGame(false);

	// 해킹의 명시적 몸체 태그는 기본 메시 가시성 필터와 다른 기존 정책을 유지한다.
	FirstBody->ComponentTags.Add(TEXT("HackCursorBody"));
	FirstBody->SetHiddenInGame(true);
	TestEqual(TEXT("Explicit hack body preserves hidden component eligibility"), Select(), First);
	FirstBody->ComponentTags.Add(TEXT("HackCursorIgnore"));
	TestNull(TEXT("Ignore tag overrides explicit hack body"), Select());
	FirstBody->ComponentTags.Reset();
	FirstBody->SetHiddenInGame(false);

	AActor* Wall = World->SpawnActor<AActor>();
	UBoxComponent* WallBody = NewObject<UBoxComponent>(Wall);
	Wall->SetRootComponent(WallBody);
	WallBody->SetBoxExtent(FVector(10.0, 100.0, 100.0));
	WallBody->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	WallBody->SetCollisionResponseToAllChannels(ECR_Block);
	WallBody->RegisterComponent();
	WallBody->SetWorldLocation(FVector(100.0, 0.0, 0.0));
	TestNull(TEXT("Wall still occludes Bounds hit"), Select());
	FirstBody->SetStaticMesh(Cube);
	TestNull(TEXT("Wall still occludes precise hit"), Select());

	// ShapeComponent는 기본적으로 게임 중 숨김 상태이므로 표시 가능한 명시적 몸체 조건을 설정한다.
	WallBody->SetHiddenInGame(false);
	WallBody->SetVisibility(true);
	WallBody->SetRenderInMainPass(true);
	TestTrue(TEXT("Primitive body visibility supports explicit non-mesh bodies"), ActorBodyTargeting::IsVisibleBody(WallBody));
	TestFalse(TEXT("Automatic mesh filter excludes detection boxes"), ActorBodyTargeting::IsVisibleMesh(WallBody));
	TestFalse(TEXT("Null body is excluded"), ActorBodyTargeting::IsVisibleMesh(nullptr));
	FirstBody->SetOnlyOwnerSee(true);
	TestFalse(TEXT("Owner-only mesh is excluded"), ActorBodyTargeting::IsVisibleMesh(FirstBody));
	FirstBody->SetOnlyOwnerSee(false);
	FirstBody->SetRenderInMainPass(false);
	TestFalse(TEXT("Non-main-pass mesh is excluded"), ActorBodyTargeting::IsVisibleMesh(FirstBody));
	FirstBody->SetRenderInMainPass(true);
	TestTrue(TEXT("Restoring visibility makes the same mesh eligible"), ActorBodyTargeting::IsVisibleMesh(FirstBody));
	TestTrue(TEXT("Precise rank precedes distance"), ActorBodyTargeting::IsBetterCandidate(true, 400.0, false, 100.0));
	TestFalse(TEXT("Near Bounds cannot replace precise rank"), ActorBodyTargeting::IsBetterCandidate(false, 100.0, true, 400.0));
	TestFalse(TEXT("Default ties keep existing candidate"), ActorBodyTargeting::IsBetterCandidate(true, 100.0, true, 100.0));
	TestTrue(TEXT("Caller can resolve equal hits by stable name order"), ActorBodyTargeting::IsBetterCandidate(true, 100.0, true, 100.0, -1));
	return true;
}

#endif
