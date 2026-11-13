#if WITH_DEV_AUTOMATION_TESTS

#include "FirstPerson/PlayerRelocationEQS.h"
#include <limits>
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "TimerManager.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace
{
struct FRelocationWorld
{
	UWorld* World = nullptr;
	UGameInstance* Instance = nullptr;
	ACharacter* Moving = nullptr;
	ACharacter* Anchor = nullptr;
	FRelocationWorld()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("PlayerRelocationTest")), GetTransientPackage());
		World->AddToRoot();
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		Instance = NewObject<UGameInstance>(GEngine);
		World->SetGameInstance(Instance);
		Instance->Init();
		World->CreateAISystem();
		World->InitializeActorsForPlay(FURL());
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Moving = World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FVector(-1000, 0, 100), FRotator::ZeroRotator, Spawn);
		Anchor = World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FVector(0, 0, 110), FRotator::ZeroRotator, Spawn);
		Moving->GetCharacterMovement()->SetCrouchedHalfHeight(40);
		Moving->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		Anchor->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}
	~FRelocationWorld()
	{
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		Instance->Shutdown();
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}
	AActor* Box(FVector Location, FVector Extent, FRotator Rotation = FRotator::ZeroRotator)
	{
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Rotation, Location));
		UBoxComponent* Component = NewObject<UBoxComponent>(Actor);
		Actor->SetRootComponent(Component);
		Component->SetBoxExtent(Extent);
		Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Component->SetCollisionObjectType(ECC_WorldStatic);
		Component->SetCollisionResponseToAllChannels(ECR_Block);
		Component->RegisterComponent();
		Component->SetWorldLocationAndRotation(Location, Rotation);
		World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
		World->GetPhysicsScene()->Flush();
		return Actor;
	}
	FPlayerRelocationRequest Request() const
	{
		FPlayerRelocationRequest Request;
		Request.MovingCharacter = Moving;
		Request.Anchor = Anchor;
		Request.Reference = Anchor->GetActorTransform();
		Request.TimeoutSeconds = 5;
		return Request;
	}
	void Complete(UPlayerRelocationQuery& Query)
	{
		// 실제 EQS 매니저를 구동한다. 물리 이동을 Tick하지 않아 검사 대상의 위치는 고정한다.
		UEnvQueryManager* Manager = UEnvQueryManager::GetCurrent(World);
		for (int32 Index = 0; Index < 100 && Query.GetResult().Status == EPlayerRelocationStatus::Running; ++Index)
			Manager->Tick(0.01f);
	}
};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FPlayerRelocationEQSTest, "Outlier.Player.RelocationEQS",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FPlayerRelocationEQSTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Name : {TEXT("GroundAndSnapshot"), TEXT("RearTie"), TEXT("NoFloor"), TEXT("LowCeilingPriority"),
		TEXT("CrouchIntent"), TEXT("ExpandedRange"), TEXT("AboveFloorRejected"), TEXT("TooDeepRejected"),
		TEXT("FlightAndPreferredOffset"), TEXT("OccludedFlight"), TEXT("AnchorSweep"), TEXT("AdditionalFilter"),
		TEXT("DynamicObstacle"), TEXT("FloorRemoved"), TEXT("Cancel"), TEXT("StaleContext"), TEXT("Timeout"),
		TEXT("Budget"), TEXT("InvalidInput"), TEXT("WalkableSlope"), TEXT("SteepSlope"),
		TEXT("ExplicitFlightOrder"), TEXT("ExplicitGround"), TEXT("ExplicitCrouchOrder"),
		TEXT("ExplicitFiltered"), TEXT("ExplicitFilteredNext"), TEXT("ExplicitNoFloor"),
		TEXT("ExplicitInvalid"), TEXT("ExplicitTooMany")})
	{
		Names.Add(Name);
		Commands.Add(Name);
	}
}

bool FPlayerRelocationEQSTest::RunTest(const FString& Parameters)
{
	FRelocationWorld Fixture;
	FPlayerRelocationRequest Request = Fixture.Request();
	const bool bFlight = Parameters == TEXT("FlightAndPreferredOffset") || Parameters == TEXT("OccludedFlight")
		|| Parameters == TEXT("AnchorSweep") || Parameters == TEXT("ExplicitFlightOrder");
	AActor* Floor = nullptr;
	if (!bFlight && Parameters != TEXT("NoFloor") && Parameters != TEXT("ExplicitNoFloor"))
	{
		FVector Location(0, 0, -10), Extent(600, 600, 10);
		if (Parameters == TEXT("ExpandedRange")) { Location.X = 275; Extent.X = 30; }
		if (Parameters == TEXT("AboveFloorRejected")) Location.Z = 130;
		if (Parameters == TEXT("TooDeepRejected")) Location.Z = -510;
		Floor = Fixture.Box(Location, Extent);
	}
	if (Parameters == TEXT("LowCeilingPriority") || Parameters == TEXT("CrouchIntent"))
	{
		// 최초 범위 전체는 낮은 천장, 확장 범위에는 서기 공간이 있어도 최초 앉기가 우선이다.
		Fixture.Box(FVector(0, 0, 150), FVector(220, 220, 20));
		Request.bWantsCrouch = Parameters == TEXT("CrouchIntent");
	}
	if (bFlight)
	{
		Request.Mode = EPlayerRelocationMode::Flight;
		Request.bPreferOffset = true;
		Request.bRequireAnchorSweep = Parameters == TEXT("AnchorSweep");
		if (Parameters == TEXT("OccludedFlight"))
		{
			Fixture.Box(FVector(0, 70, 110), FVector(1000, 5, 1000));
			Request.AdditionalFilter = [](const FVector& Location, float, float) { return Location.Y >= 150; };
		}
		if (Parameters == TEXT("AnchorSweep"))
			Fixture.Box(Request.Reference.GetLocation() + FVector(30, 0, 0), FVector(5, 5, 200));
	}
	if (Parameters == TEXT("AdditionalFilter"))
		Request.AdditionalFilter = [](const FVector& Location, float, float) { return Location.X >= 250; };
	if (Parameters.StartsWith(TEXT("Explicit")))
	{
		// 지정 출입구는 주변 3m 제한 밖이어도 허용하되, 일반 안전 필터는 생략하지 않는다.
		Request.ExplicitCandidates = { FVector(500, 0, 110), FVector(350, 0, 110) };
		if (Parameters == TEXT("ExplicitCrouchOrder"))
			Fixture.Box(FVector(500, 0, 150), FVector(60, 60, 20));
		if (Parameters == TEXT("ExplicitFiltered"))
			Request.AdditionalFilter = [](const FVector&, float, float) { return false; };
		if (Parameters == TEXT("ExplicitFilteredNext"))
			Request.AdditionalFilter = [](const FVector& Location, float, float) { return Location.X < 400; };
		if (Parameters == TEXT("ExplicitInvalid"))
			Request.ExplicitCandidates[0].X = std::numeric_limits<double>::quiet_NaN();
		if (Parameters == TEXT("ExplicitTooMany")) Request.MaxCandidatesPerStage = 1;
	}
	bool bContextCurrent = true;
	Request.IsContextCurrent = [&bContextCurrent]() { return bContextCurrent; };
	if (Parameters == TEXT("Budget")) Request.MaxCollisionChecks = 1;
	if (Parameters == TEXT("InvalidInput")) Request.GridSpacing = 0;
	if (Parameters == TEXT("Timeout")) Request.TimeoutSeconds = 0.01;
	if (Parameters == TEXT("WalkableSlope") || Parameters == TEXT("SteepSlope"))
	{
		Floor->Destroy();
		Fixture.Box(FVector(0, 0, -10), FVector(600, 600, 10), FRotator(Parameters == TEXT("SteepSlope") ? 60 : 20, 0, 0));
		// 기준점보다 낮은 쪽에서 실제 경사면 캡슐 접촉을 검사한다.
		Request.AdditionalFilter = [](const FVector& Location, float, float) { return Location.X <= -100; };
	}
	TStrongObjectPtr<UPlayerRelocationQuery> Query(NewObject<UPlayerRelocationQuery>());
	int32 Completions = 0;
	const FVector OriginalLocation = Fixture.Moving->GetActorLocation();
	const float OriginalHeight = Fixture.Moving->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const bool bStarted = Query->Start(Request, [&Completions](const FPlayerRelocationResult&) { ++Completions; });
	if (Parameters == TEXT("Cancel")) Query->Cancel();
	if (Parameters == TEXT("StaleContext")) bContextCurrent = false;
	if (Parameters == TEXT("Timeout")) FPlatformProcess::Sleep(0.02f);
	if (Parameters == TEXT("GroundAndSnapshot")) Fixture.Anchor->SetActorLocation(FVector(2000, 0, 110));
	Fixture.Complete(*Query);
	const FPlayerRelocationResult& Result = Query->GetResult();
	EPlayerRelocationStatus Expected = EPlayerRelocationStatus::Success;
	if (Parameters == TEXT("NoFloor") || Parameters == TEXT("AboveFloorRejected") || Parameters == TEXT("TooDeepRejected")
		|| Parameters == TEXT("OccludedFlight") || Parameters == TEXT("AnchorSweep") || Parameters == TEXT("SteepSlope")
		|| Parameters == TEXT("ExplicitFiltered") || Parameters == TEXT("ExplicitNoFloor"))
		Expected = EPlayerRelocationStatus::NoCandidate;
	if (Parameters == TEXT("Cancel")) Expected = EPlayerRelocationStatus::Cancelled;
	if (Parameters == TEXT("StaleContext") || Parameters == TEXT("InvalidInput")
		|| Parameters == TEXT("ExplicitInvalid") || Parameters == TEXT("ExplicitTooMany")) Expected = EPlayerRelocationStatus::InvalidContext;
	if (Parameters == TEXT("Timeout")) Expected = EPlayerRelocationStatus::TimedOut;
	if (Parameters == TEXT("Budget")) Expected = EPlayerRelocationStatus::BudgetExceeded;
	TestEqual(TEXT("Actual EQS terminates with the expected outcome"), Result.Status, Expected);
	TestEqual(TEXT("Completion is delivered exactly once"), Completions, 1);
	TestEqual(TEXT("Invalid configuration never starts EQS"), bStarted,
		Parameters != TEXT("InvalidInput") && Parameters != TEXT("ExplicitInvalid") && Parameters != TEXT("ExplicitTooMany"));
	TestTrue(TEXT("Candidate checks never teleport the character"), Fixture.Moving->GetActorLocation().Equals(OriginalLocation));
	TestEqual(TEXT("Candidate checks never change real posture"), Fixture.Moving->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), OriginalHeight);
	TestTrue(TEXT("Collision work stays within the request budget"), Result.CollisionChecks <= Request.MaxCollisionChecks);
	if (Result.Status == EPlayerRelocationStatus::Success)
	{
		TestTrue(TEXT("Successful result passes independent final revalidation"), Query->Revalidate());
		if (Parameters == TEXT("LowCeilingPriority") || Parameters == TEXT("CrouchIntent"))
		{
			TestEqual(TEXT("Crouch candidate uses the intended capsule"), Result.Posture, EPlayerRelocationPosture::Crouched);
			TestFalse(TEXT("Initial crouch beats expanded standing"), Result.bExpanded);
		}
		if (Parameters == TEXT("ExpandedRange") || Parameters == TEXT("AdditionalFilter"))
			TestTrue(TEXT("Only safe expanded candidates are accepted"), Result.bExpanded);
		if (Parameters == TEXT("FlightAndPreferredOffset"))
			TestTrue(TEXT("Configured flight offset beats ordinary nearest candidates"), Result.Location.Equals(FVector(0, 200, 110)));
		if (Parameters == TEXT("ExplicitFlightOrder") || Parameters == TEXT("ExplicitGround")
			|| Parameters == TEXT("ExplicitCrouchOrder"))
		{
			TestTrue(TEXT("Designated candidates preserve entrance order beyond nearby radius"),
				FMath::IsNearlyEqual(Result.Location.X, 500.0, 0.1));
			TestTrue(TEXT("Designated search generates no surrounding grid"), Result.GeneratedCandidates <= 2);
			TestFalse(TEXT("Designated candidates do not repeat an expanded stage"), Result.bExpanded);
		}
		if (Parameters == TEXT("ExplicitCrouchOrder"))
			TestEqual(TEXT("Nearer entrance crouch is checked before a later entrance standing"),
				Result.Posture, EPlayerRelocationPosture::Crouched);
		if (Parameters == TEXT("ExplicitFilteredNext"))
			TestTrue(TEXT("Rejected entrance advances to the next designated candidate"),
				FMath::IsNearlyEqual(Result.Location.X, 350.0, 0.1));
		if (Parameters == TEXT("GroundAndSnapshot"))
			TestTrue(TEXT("Moved anchor never shifts the saved search reference"), Result.Location.Size2D() <= 200);
		if (Parameters == TEXT("RearTie"))
			TestTrue(TEXT("Equidistant safe candidates prefer behind the saved yaw"), Result.Location.X < 0);
		if (Parameters == TEXT("DynamicObstacle"))
		{
			Fixture.Box(Result.Location, FVector(100, 100, 100));
			TestFalse(TEXT("New blocking geometry invalidates a stored result"), Query->Revalidate());
		}
		if (Parameters == TEXT("FloorRemoved"))
		{
			Floor->Destroy();
			TestFalse(TEXT("Missing floor invalidates a stored result"), Query->Revalidate());
		}
	}
	Query->Cancel();
	TestEqual(TEXT("Late cancellation cannot publish a second completion"), Completions, 1);
	TestFalse(TEXT("Cancelled results cannot be applied after selection has completed"), Query->Revalidate());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerRelocationTemplateGCTest, "Outlier.Player.RelocationEQS.TemplateGCLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerRelocationTemplateGCTest::RunTest(const FString& Parameters)
{
	FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(UPlayerRelocationQuery::StaticClass(), TEXT("QueryTemplate"));
	if (!TestNotNull(TEXT("Query template reference is reflected"), Property)) return false;
	TWeakObjectPtr<UEnvQuery> SharedTemplate;
	for (int32 Iteration = 0; Iteration < 3; ++Iteration)
	{
		{
			FRelocationWorld Fixture;
			auto Request = Fixture.Request();
			Request.Mode = EPlayerRelocationMode::Flight;
			TStrongObjectPtr<UPlayerRelocationQuery> Query(NewObject<UPlayerRelocationQuery>());
			int32 Completions = 0;
			if (!TestTrue(TEXT("Request starts after idle GC/world teardown"),
				Query->Start(Request, [&Completions](const FPlayerRelocationResult&) { ++Completions; }))) return false;
			CollectGarbage(RF_NoFlags, true);
			Fixture.Complete(*Query);
			TestEqual(TEXT("Query completes across active GC"), Query->GetResult().Status, EPlayerRelocationStatus::Success);
			TestEqual(TEXT("Completion is delivered once"), Completions, 1);
			UEnvQuery* Template = Cast<UEnvQuery>(Property->GetObjectPropertyValue_InContainer(Query.Get()));
			if (!TestNotNull(TEXT("Completed request has a template"), Template)) return false;
			if (Iteration > 0) TestEqual(TEXT("World recreation reuses one immutable template"), SharedTemplate.Get(), Template);
			SharedTemplate = Template;
			Query->Cancel();
		}
		// No request or world remains to retain the original template. Cooked coverage
		// additionally runs this boundary with native CDOs excluded from GC traversal.
		CollectGarbage(RF_NoFlags, true);
		if (!TestTrue(TEXT("Shared template survives idle GC after world teardown"), SharedTemplate.IsValid())) return false;
	}
	return true;
}

#endif
