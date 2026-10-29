#if WITH_DEV_AUTOMATION_TESTS

#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterCharacterMovementComponent.h"
#include "Shooter/ShooterMovementComponent.h"
#include "Weapon/WeaponBase.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/Engine.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/CoreNet.h"

namespace ShooterSlideSimulationTests
{
enum class ETerrain : uint8
{
	Flat,
	Slope,
	SmallStep,
	Cliff
};

struct FFixture
{
	UWorld* World = nullptr;
	AShooterCharacter* Shooter = nullptr;
	UShooterMovementComponent* Input = nullptr;
	UShooterCharacterMovementComponent* Movement = nullptr;
	float Friction = 0.0f;
	float Braking = 0.0f;
	float BrakingFactor = 0.0f;
	float Acceleration = 0.0f;
	float CrouchSpeed = 0.0f;
	FVector Start = FVector::ZeroVector;

	bool Initialize(FAutomationTestBase& Test, ETerrain Terrain = ETerrain::Flat)
	{
		UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
			TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
		if (!Test.TestNotNull(TEXT("Concrete Shooter class"), ShooterClass)) return false;
		const FName Name = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("SlideSimulation"),
			EUniqueObjectNameOptions::GloballyUnique);
		FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
		World = UWorld::CreateWorld(EWorldType::Game, false, Name, GetTransientPackage());
		if (!Test.TestNotNull(TEXT("Slide simulation world"), World))
		{
			GEngine->DestroyWorldContext(World);
			return false;
		}
		World->AddToRoot();
		Context.SetCurrentWorld(World);

		// 평지는 상주 레벨에 둔다. 숨긴 스트리밍 레벨을 지면으로 간주하지 않는다.
		AActor* Floor = World->SpawnActor<AActor>();
		if (!Test.TestNotNull(TEXT("Persistent floor actor"), Floor)) return false;
		UBoxComponent* Box = NewObject<UBoxComponent>(Floor);
		Floor->SetRootComponent(Box);
		Box->SetBoxExtent(FVector(10000.0, 10000.0, 50.0));
		Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetMobility(EComponentMobility::Static);
		Box->SetWorldLocation(FVector(0.0, 0.0, -50.0));
		if (Terrain == ETerrain::Slope)
		{
			// X 진행 방향으로 10도 오르막을 둔다. 원점의 윗면 높이는 평지와 같은 0이다.
			Box->SetWorldRotation(FRotator(10.0, 0.0, 0.0));
			Box->SetWorldLocation(FVector(0.0, 0.0, -50.0 / FMath::Cos(FMath::DegreesToRadians(10.0))));
		}
		else if (Terrain == ETerrain::Cliff)
		{
			// 시작 지점에는 실제 바닥이 있고 X=120 이후에는 충돌이 없다. 강제 Falling 전환으로 대체하지 않는다.
			Box->SetWorldLocation(FVector(-9880.0, 0.0, -50.0));
		}
		Box->RegisterComponent();
		if (Terrain == ETerrain::SmallStep)
		{
			AActor* StepActor = World->SpawnActor<AActor>();
			if (!Test.TestNotNull(TEXT("Small step actor"), StepActor)) return false;
			UBoxComponent* StepBox = NewObject<UBoxComponent>(StepActor);
			StepActor->SetRootComponent(StepBox);
			StepBox->SetBoxExtent(FVector(1000.0, 1000.0, 5.0));
			StepBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			StepBox->SetCollisionResponseToAllChannels(ECR_Block);
			StepBox->SetCollisionObjectType(ECC_WorldStatic);
			StepBox->SetMobility(EComponentMobility::Static);
			// X=100에서 높이 10cm의 턱이 시작하고 Slide가 끝날 때까지 이어진다.
			StepBox->SetWorldLocation(FVector(1100.0, 0.0, 5.0));
			StepBox->RegisterComponent();
		}

		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Shooter = World->SpawnActor<AShooterCharacter>(ShooterClass, FVector(0.0, 0.0, 200.0), FRotator::ZeroRotator, Spawn);
		if (!Test.TestNotNull(TEXT("Slide simulation Shooter"), Shooter)) return false;
		Movement = Cast<UShooterCharacterMovementComponent>(Shooter->GetCharacterMovement());
		Input = Shooter->FindComponentByClass<UShooterMovementComponent>();
		if (!Test.TestNotNull(TEXT("Slide CMC"), Movement)
			|| !Test.TestNotNull(TEXT("Slide input component"), Input)) return false;
		if (Terrain == ETerrain::Cliff)
		{
			// UE는 에디터의 BeginPlay 전/첫 1초에 낙하를 보류한다. 이 월드는 Tick하지 않아 그 상태가 영구히 남는다.
			// Actor 전체 BeginPlay 대신 물리 테스트 월드만 정상 플레이 이후로 둔다. 낙하 자체는 실제 바닥 검사로 결정한다.
			World->SetBegunPlay(true);
			World->TimeSeconds = 2.0;
			if (!Test.TestTrue(TEXT("Cliff fixture is past the editor startup falling guard"),
				World->HasBegunPlay() && World->GetTimeSeconds() >= 1.0f)) return false;
		}
		Movement->bRunPhysicsWithNoController = true;
		Movement->SetMovementMode(MOVE_Walking);
		Shooter->SetActorLocation(FVector(0.0, 0.0, Shooter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.15));
		Movement->bForceNextFloorCheck = true;
		Movement->PerformMovement(1.0f / 60.0f);
		if (!Test.TestTrue(TEXT("Fixture has a real walkable floor"), Movement->CurrentFloor.IsWalkableFloor())) return false;

		Shooter->SlideDuration = 1.0f;
		Shooter->MinSlideSpeed = 200.0f;
		Shooter->SlideSpeedMultiplier = 1.2f;
		UCurveFloat* Curve = NewObject<UCurveFloat>(Shooter);
		Curve->FloatCurve.AddKey(0.0f, 1.0f);
		Curve->FloatCurve.AddKey(1.0f, 0.44f);
		Shooter->SlideSpeedCurve = Curve;
		Friction = Movement->GroundFriction;
		Braking = Movement->BrakingDecelerationWalking;
		BrakingFactor = Movement->BrakingFrictionFactor;
		Acceleration = Movement->MaxAcceleration;
		CrouchSpeed = Movement->MaxWalkSpeedCrouched;
		Movement->Velocity = FVector(400.0, 0.0, 0.0);
		Input->HandleSprintPressed();
		Input->HandleCrouchPressed();
		Start = Shooter->GetActorLocation();
		return Test.TestTrue(TEXT("Slide starts through the gameplay input path"), Movement->IsSlideSimulating() && Input->IsSliding());
	}

	void CheckRestored(FAutomationTestBase& Test) const
	{
		Test.TestFalse(TEXT("Slide simulation ended"), Movement->IsSlideSimulating());
		Test.TestFalse(TEXT("Slide presentation state ended"), Input->IsSliding());
		Test.TestFalse(TEXT("Slide action lock released"), Shooter->IsActionLocked());
		Test.TestEqual(TEXT("Ground friction restored"), Movement->GroundFriction, Friction);
		Test.TestEqual(TEXT("Braking restored"), Movement->BrakingDecelerationWalking, Braking);
		Test.TestEqual(TEXT("Braking factor restored"), Movement->BrakingFrictionFactor, BrakingFactor);
		Test.TestEqual(TEXT("Acceleration restored"), Movement->MaxAcceleration, Acceleration);
		Test.TestEqual(TEXT("Crouch speed restored"), Movement->MaxWalkSpeedCrouched, CrouchSpeed);
	}

	float Distance() const { return FVector::Dist2D(Start, Shooter->GetActorLocation()); }

	~FFixture()
	{
		if (!World) return;
		if (Input && Input->IsSliding()) Input->StopSlide(ESlideEndReason::ForcedCancel);
		// 절벽 테스트에서 시작 상태로 둔 월드는 정식 종료 경로를 거친 뒤 파괴한다.
		if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}
};

bool Step(FAutomationTestBase& Test, FFixture& Fixture, float Delta)
{
	const float Before = Fixture.Movement->GetSlideElapsedTime();
	const float Speed = Fixture.Movement->GetCurrentSlideSpeed();
	const FVector BeforePosition = Fixture.Shooter->GetActorLocation();
	Fixture.Movement->PerformMovement(Delta);
	Test.TestFalse(TEXT("Actual movement position remains finite"), Fixture.Shooter->GetActorLocation().ContainsNaN());
	Test.TestFalse(TEXT("Actual movement velocity remains finite"), Fixture.Movement->Velocity.ContainsNaN());
	Test.TestTrue(TEXT("Unobstructed slide never moves backwards"), Fixture.Shooter->GetActorLocation().X >= BeforePosition.X - 0.01);
	Test.TestTrue(TEXT("Flat-ground slide keeps its entry direction"), FMath::Abs(Fixture.Shooter->GetActorLocation().Y - Fixture.Start.Y) < 0.01);
	Test.TestTrue(TEXT("Simulation clock never reverses"), Fixture.Movement->GetSlideElapsedTime() >= Before);
	Test.TestTrue(TEXT("Simulation never consumes more than the supplied delta"),
		Fixture.Movement->GetSlideElapsedTime() - Before <= Delta + KINDA_SMALL_NUMBER);
	Test.TestTrue(TEXT("Slide curve never accelerates"), Fixture.Movement->GetCurrentSlideSpeed() <= Speed + KINDA_SMALL_NUMBER);
	return Fixture.Movement->IsSlideSimulating();
}

// 실제 파일 읽기 시간을 조작하지 않는다. 로드는 비동기로 진행하고 레벨 표시만 정해진 시간 동안 보류한다.
class FDelayedStreamingCommand final : public IAutomationLatentCommand
{
public:
	FDelayedStreamingCommand(FAutomationTestBase& InTest, float InDelay)
		: Test(InTest), Delay(InDelay) {}
	virtual ~FDelayedStreamingCommand() override { Finish(); }

	virtual bool Update() override
	{
		if (!Fixture)
		{
			Fixture = MakeUnique<FFixture>();
			if (!Fixture->Initialize(Test)) return Finish();
			bool bSuccess = false;
			Streaming = ULevelStreamingDynamic::LoadLevelInstance(Fixture->World,
				TEXT("/Game/Maps/Title"), FVector(100000.0, 0.0, 0.0), FRotator::ZeroRotator, bSuccess);
			if (!Test.TestTrue(TEXT("Lightweight level async load request accepted"), bSuccess && Streaming)) return Finish();
			Streaming->SetShouldBeVisible(false);
			Started = FPlatformTime::Seconds();
			LastUpdate = Started;
			Test.AddInfo(FString::Printf(TEXT("[SlideSimTest] Case=LoadingDelay Phase=Requested VisibilityHoldMs=%.0f Map=/Game/Maps/Title IOThrottled=0"), Delay * 1000.0f));
			return false;
		}

		const double Now = FPlatformTime::Seconds();
		if (Now - Started > 15.0)
		{
			Test.AddError(TEXT("[SlideSimTest] LoadingDelay timed out waiting for level load/visibility or slide completion (15s)."));
			return Finish();
		}
		Fixture->World->UpdateLevelStreaming();
		if (!bLoggedLoaded && Streaming->IsLevelLoaded())
		{
			bLoggedLoaded = true;
			Test.AddInfo(FString::Printf(TEXT("[SlideSimTest] Case=LoadingDelay Phase=PackageLoaded WallElapsedMs=%.3f Visible=%d"),
				(Now - Started) * 1000.0, Streaming->IsLevelVisible()));
		}
		if (!bReleased && Now - Started >= Delay)
		{
			bReleased = true;
			Streaming->SetShouldBeVisible(true);
			Test.AddInfo(FString::Printf(TEXT("[SlideSimTest] Case=LoadingDelay Phase=VisibilityReleased RequestedHoldMs=%.0f ActualHoldMs=%.3f Loaded=%d"),
				Delay * 1000.0f, (Now - Started) * 1000.0, Streaming->IsLevelLoaded()));
		}
		else if (!bReleased)
		{
			Test.TestFalse(TEXT("Held streaming level remains hidden"), Streaming->IsLevelVisible());
		}

		// 자동화 프레임의 실제 간격을 넣는다. 월드 Tick 상한으로 잘린 시간을 가정하지 않는다.
		const float Delta = static_cast<float>(Now - LastUpdate);
		LastUpdate = Now;
		if (Fixture->Movement->IsSlideSimulating() && Delta > 0.0f)
		{
			const float Before = Fixture->Movement->GetSlideElapsedTime();
			Step(Test, *Fixture, Delta);
			if (Now - LastLog >= 0.1 || Delta >= 0.1f || !Fixture->Movement->IsSlideSimulating())
			{
				LastLog = Now;
				Test.AddInfo(FString::Printf(TEXT("[SlideSimTest] Case=LoadingDelay RequestedDt=%.6f ConsumedDt=%.6f SimElapsed=%.6f DistanceCm=%.3f Loaded=%d Visible=%d"),
					Delta, Fixture->Movement->GetSlideElapsedTime() - Before, Fixture->Movement->GetSlideElapsedTime(),
					Fixture->Distance(), Streaming->IsLevelLoaded(), Streaming->IsLevelVisible()));
			}
		}
		if (!Fixture->Movement->IsSlideSimulating() && Streaming->IsLevelVisible())
		{
			Fixture->CheckRestored(Test);
			Test.TestTrue(TEXT("Streaming overlap slide moved forward"), Fixture->Distance() > 300.0f);
			Test.AddInfo(FString::Printf(TEXT("[SlideSimTest] Case=LoadingDelay Phase=Completed HoldMs=%.0f SimElapsed=%.6f DistanceCm=%.3f"),
				Delay * 1000.0f, Fixture->Movement->GetSlideElapsedTime(), Fixture->Distance()));
			return Finish();
		}
		return false;
	}

private:
	bool Finish()
	{
		// fixture 폐기 전에 스트리밍 요청을 정리해 다음 자동화 케이스에 남기지 않는다.
		if (Streaming && Fixture && Fixture->World)
		{
			Streaming->SetShouldBeVisible(false);
			Streaming->SetShouldBeLoaded(false);
			Streaming->SetIsRequestingUnloadAndRemoval(true);
			Fixture->World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
		}
		Streaming = nullptr;
		Fixture.Reset();
		return true;
	}

	FAutomationTestBase& Test;
	float Delay;
	double Started = 0.0;
	double LastUpdate = 0.0;
	double LastLog = 0.0;
	bool bReleased = false;
	bool bLoggedLoaded = false;
	ULevelStreamingDynamic* Streaming = nullptr;
	TUniquePtr<FFixture> Fixture;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterSlideSimulationTest,
	"Outlier.Shooter.Slide.Simulation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterSlideSimulationTest::RunTest(const FString& Parameters)
{
	using namespace ShooterSlideSimulationTests;
	// 기존 60Hz 타이머의 좌측 샘플 적분값을 비교 기준으로 남긴다 (entry=400*1.2, linear 1->0.44).
	float LegacyDistance = 0.0f;
	for (int32 Index = 0; Index < 60; ++Index)
		LegacyDistance += 480.0f * FMath::Lerp(1.0f, 0.44f, Index / 60.0f) / 60.0f;
	float ReferenceDistance = 0.0f;
	for (int32 FPS : {60, 30, 120})
	{
		for (int32 Trial = 0; Trial < 5; ++Trial)
		{
			FFixture Fixture;
			if (!Fixture.Initialize(*this)) return false;
			const float Delta = 1.0f / FPS;
			int32 Frames = 0;
			while (Fixture.Movement->IsSlideSimulating() && Frames < FPS * 2)
			{
				Step(*this, Fixture, Delta);
				++Frames;
			}
			Fixture.CheckRestored(*this);
			TestTrue(TEXT("Slide completion is within one simulation frame"), FMath::Abs(Frames * Delta - 1.0f) <= Delta + KINDA_SMALL_NUMBER);
			TestTrue(TEXT("Simulation completes at configured duration"), FMath::IsNearlyEqual(Fixture.Movement->GetSlideElapsedTime(), 1.0f, KINDA_SMALL_NUMBER));
			const float Distance = Fixture.Distance();
			if (FPS == 60 && Trial == 0) ReferenceDistance = Distance;
			TestTrue(TEXT("Distance stays within five percent of 60Hz simulation"), ReferenceDistance > 0.0f && FMath::Abs(Distance - ReferenceDistance) <= ReferenceDistance * 0.05f);
			TestTrue(TEXT("Distance stays within five percent of legacy flat-ground baseline"), FMath::Abs(Distance - LegacyDistance) <= LegacyDistance * 0.05f);
			AddInfo(FString::Printf(TEXT("[SlideSimTest] Case=FPS FPS=%d Trial=%d InputDt=%.6f Frames=%d SimElapsed=%.6f DistanceCm=%.3f Ref60Cm=%.3f LegacyCm=%.3f"),
				FPS, Trial + 1, Delta, Frames, Fixture.Movement->GetSlideElapsedTime(), Distance, ReferenceDistance, LegacyDistance));
		}
	}
	for (float Hitch : {0.1f, 0.25f})
	{
		FFixture Fixture;
		if (!Fixture.Initialize(*this)) return false;
		for (int32 Index = 0; Index < 12; ++Index) Step(*this, Fixture, 1.0f / 60.0f);
		const float Before = Fixture.Movement->GetSlideElapsedTime();
		Step(*this, Fixture, Hitch);
		AddInfo(FString::Printf(TEXT("[SlideSimTest] Case=LongDelta InjectedDt=%.6f ConsumedDt=%.6f SimElapsed=%.6f DistanceCm=%.3f MaxTimeStep=%.6f MaxIterations=%d IOThrottled=0"),
			Hitch, Fixture.Movement->GetSlideElapsedTime() - Before, Fixture.Movement->GetSlideElapsedTime(), Fixture.Distance(),
			Fixture.Movement->MaxSimulationTimeStep, Fixture.Movement->MaxSimulationIterations));
		for (int32 Index = 0; Index < 120 && Fixture.Movement->IsSlideSimulating(); ++Index) Step(*this, Fixture, 1.0f / 60.0f);
		Fixture.CheckRestored(*this);
		TestTrue(TEXT("Long-delta slide distance remains within five percent"), FMath::Abs(Fixture.Distance() - ReferenceDistance) <= ReferenceDistance * 0.05f);
	}
	{
		FFixture Fixture;
		if (!Fixture.Initialize(*this)) return false;
		Fixture.Movement->MaxSimulationTimeStep = 0.05f;
		Fixture.Movement->MaxSimulationIterations = 2;
		Step(*this, Fixture, 0.25f);
		const float Consumed = Fixture.Movement->GetSlideElapsedTime();
		TestTrue(TEXT("Two iterations cannot consume more than two bounded steps"), FMath::IsNearlyEqual(Consumed, 0.1f, KINDA_SMALL_NUMBER));
		TestTrue(TEXT("Budget-limited movement covers a finite partial distance"), Fixture.Distance() > 0.0f && Fixture.Distance() < 48.0f);
		Step(*this, Fixture, 1.0f / 60.0f);
		TestTrue(TEXT("Dropped simulation time is not recovered on the next move"),
			FMath::IsNearlyEqual(Fixture.Movement->GetSlideElapsedTime() - Consumed, 1.0f / 60.0f, KINDA_SMALL_NUMBER));
		AddInfo(FString::Printf(TEXT("[SlideSimTest] Case=TimeBudget InjectedDt=0.250000 ConsumedDt=%.6f DroppedDt=%.6f NextSimElapsed=%.6f DistanceCm=%.3f MaxTimeStep=0.050000 MaxIterations=2"),
			Consumed, 0.25f - Consumed, Fixture.Movement->GetSlideElapsedTime(), Fixture.Distance()));
		for (int32 Index = 0; Index < 120 && Fixture.Movement->IsSlideSimulating(); ++Index) Step(*this, Fixture, 1.0f / 60.0f);
		Fixture.CheckRestored(*this);
	}
	for (ESlideEndReason Reason : {ESlideEndReason::ForcedCancel, ESlideEndReason::JumpCancel, ESlideEndReason::FallCancel})
	{
		FFixture Fixture;
		if (!Fixture.Initialize(*this)) return false;
		Step(*this, Fixture, 0.05f);
		const int32 JumpCount = Fixture.Shooter->JumpCurrentCount;
		if (Reason == ESlideEndReason::JumpCancel) Fixture.Input->DoJumpStart();
		else if (Reason == ESlideEndReason::FallCancel) Fixture.Movement->SetMovementMode(MOVE_Falling);
		else Fixture.Input->StopSlide(Reason);
		Fixture.CheckRestored(*this);
		TestEqual(TEXT("Slide cancel does not consume a jump"), Fixture.Shooter->JumpCurrentCount, JumpCount);

		// 종료 후 실제 웅크림을 해제하고 새 입력으로 재진입한다. 이전 시계/커브 상태가 남으면 실패한다.
		Fixture.Input->HandleCrouchReleased();
		Fixture.Input->HandleSprintReleased();
		Fixture.Movement->SetMovementMode(MOVE_Walking);
		Fixture.Shooter->SetActorLocation(FVector(0.0, 0.0,
			Fixture.Shooter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.15));
		Fixture.Movement->bForceNextFloorCheck = true;
		Fixture.Movement->PerformMovement(1.0f / 60.0f);
		Fixture.Movement->Velocity = FVector(400.0, 0.0, 0.0);
		Fixture.Input->HandleSprintPressed();
		Fixture.Input->HandleCrouchPressed();
		TestTrue(TEXT("Slide re-enters after cancellation"), Fixture.Movement->IsSlideSimulating());
		TestEqual(TEXT("Re-entry clears previous simulation time"), Fixture.Movement->GetSlideElapsedTime(), 0.0f);
		TestTrue(TEXT("Re-entry captures a fresh speed"), FMath::IsNearlyEqual(Fixture.Movement->GetCurrentSlideSpeed(), 480.0f, 0.01f));
		Fixture.Input->StopSlide(ESlideEndReason::ForcedCancel);
		Fixture.CheckRestored(*this);
		AddInfo(FString::Printf(TEXT("[SlideSimTest] Case=CancelReentry Reason=%s"), *UEnum::GetValueAsString(Reason)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterSlideTerrainTest,
	"Outlier.Shooter.Slide.Terrain", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterSlideTerrainTest::RunTest(const FString& Parameters)
{
	using namespace ShooterSlideSimulationTests;
	for (ETerrain Terrain : {ETerrain::Slope, ETerrain::SmallStep, ETerrain::Cliff})
	{
		const TCHAR* Name = Terrain == ETerrain::Slope ? TEXT("Slope")
			: Terrain == ETerrain::SmallStep ? TEXT("SmallStep") : TEXT("Cliff");
		for (int32 FPS : {30, 60, 120})
		{
			for (float Hitch : {0.0f, 0.1f, 0.25f})
			{
				FFixture Fixture;
				if (!Fixture.Initialize(*this, Terrain)) return false;
				auto* Movement = Fixture.Movement;
				auto* Shooter = Fixture.Shooter;
				const int32 JumpCount = Shooter->JumpCurrentCount;
				const float StartFeetZ = Shooter->GetActorLocation().Z
					- Shooter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
				if (Terrain == ETerrain::SmallStep
					&& !TestTrue(TEXT("Fixture step is below the existing step height"), Movement->MaxStepHeight > 10.0f)) return false;
				if (Terrain == ETerrain::Cliff
					&& !TestTrue(TEXT("Shooter retains its existing crouched ledge policy"), Movement->bCanWalkOffLedgesWhenCrouching)) return false;

				float Travel2D = 0.0f;
				float Travel3D = 0.0f;
				// 같은 진입 상태에서 평상 Delta와 한 번의 긴 Delta를 비교한다. 실제 IO/통신 지연은 주입하지 않는다.
				// 3D 거리는 캡슐 중심 기준이라 앉기/일어서기의 높이 변경도 포함한다. 지면 추종은 발 높이로 검증한다.
				for (int32 Frame = 0; Frame < FPS * 2 && Movement->IsSlideSimulating(); ++Frame)
				{
					const FVector Before = Shooter->GetActorLocation();
					const float Delta = Hitch > 0.0f && Frame == FPS / 10 ? Hitch : 1.0f / FPS;
					Step(*this, Fixture, Delta);
					Travel2D += FVector::Dist2D(Before, Shooter->GetActorLocation());
					Travel3D += FVector::Distance(Before, Shooter->GetActorLocation());
				}
				Fixture.CheckRestored(*this);
				TestEqual(TEXT("Terrain movement does not consume a jump"), Shooter->JumpCurrentCount, JumpCount);
				if (Terrain == ETerrain::Cliff)
				{
					TestTrue(TEXT("Actual floor edge enters Falling"), Movement->IsFalling());
					TestEqual(TEXT("Actual falling cancels Slide"), Movement->SlideEndReason, uint8(ESlideEndReason::FallCancel));
					TestTrue(TEXT("Cliff cancellation occurs before normal Slide expiry"), Movement->GetSlideElapsedTime() < Movement->SlideSimulationDuration);
					TestTrue(TEXT("Capsule advances beyond the actual floor edge"), Shooter->GetActorLocation().X > 120.0f);
				}
				else
				{
					const float EndFeetZ = Shooter->GetActorLocation().Z
						- Shooter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
					TestTrue(TEXT("Walkable terrain retains grounded movement"), Movement->IsMovingOnGround() && Movement->CurrentFloor.IsWalkableFloor());
					TestEqual(TEXT("Walkable terrain preserves normal Slide expiry"), Movement->SlideEndReason, uint8(ESlideEndReason::Finished));
					TestTrue(TEXT("Terrain does not block forward Slide progress"), Fixture.Distance() > 200.0f);
					TestTrue(TEXT("Terrain Slide completes at its configured duration"),
						FMath::IsNearlyEqual(Movement->GetSlideElapsedTime(), Movement->SlideSimulationDuration, KINDA_SMALL_NUMBER));
					if (Terrain == ETerrain::Slope)
					{
						TestTrue(TEXT("Capsule actually follows the uphill floor"), EndFeetZ - StartFeetZ > 20.0f);
					}
					else
					{
						TestTrue(TEXT("Capsule actually climbs the ten-centimeter step"), FMath::IsNearlyEqual(EndFeetZ - StartFeetZ, 10.0f, 2.0f));
					}
				}
				AddInfo(FString::Printf(TEXT("[SlideTerrainTest] Case=%s FPS=%d InjectedDt=%.6f IOThrottled=0 SimElapsed=%.6f NetDistance2DCm=%.3f Travel2DCm=%.3f CapsuleCenterTravel3DCm=%.3f EndReason=%s Falling=%d"),
					Name, FPS, Hitch, Movement->GetSlideElapsedTime(), Fixture.Distance(), Travel2D, Travel3D,
					*UEnum::GetValueAsString(static_cast<ESlideEndReason>(Movement->SlideEndReason)), Movement->IsFalling()));
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterSlideLoadingDelayTest,
	"Outlier.Shooter.Slide.LoadingDelay", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterSlideLoadingDelayTest::RunTest(const FString& Parameters)
{
	using namespace ShooterSlideSimulationTests;
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedStreamingCommand(*this, 0.1f));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedStreamingCommand(*this, 0.25f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterSlidePredictionTest,
	"Outlier.Shooter.Slide.Prediction", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterSlidePredictionTest::RunTest(const FString& Parameters)
{
	using namespace ShooterSlideSimulationTests;
	(void)Parameters;
	{
		FFixture Fixture;
		if (!Fixture.Initialize(*this)) return false;
		auto* Movement = Fixture.Movement;
		FShooterAudioMoveData Sent;
		Sent.MovementInput.Epoch = 7;
		Sent.TimeStamp = 1.0f;
		Sent.MovementMode = MOVE_Walking;
		Sent.AudioMoveId = 21;
		Sent.AudioAirborneId = 19;
		Sent.MovementInput.CrouchRequest = 17;
		Sent.MovementInput.JumpCancelRequest = 18;
		Sent.MovementInput.bSprintAtCrouch = true;
		Sent.MovementInput.bCrouch = true;
		Sent.PredictedSlideRequest = 17;
		Sent.bPredictedSliding = true;
		Sent.bPredictedSprintHeld = false;
		Sent.bPredictedCrouchHeld = true;
		Sent.bPredictedCrouched = true;
		Sent.PredictedEndReason = uint8(ESlideEndReason::JumpCancel);
		FNetBitWriter Writer(nullptr, 8192);
		using EMoveType = FCharacterNetworkMoveData::ENetworkMoveType;
		if (!TestTrue(TEXT("Packed movement input serializes"), Sent.Serialize(*Movement, Writer, nullptr, EMoveType::NewMove))) return false;
		FNetBitReader Reader(nullptr, Writer.GetData(), Writer.GetNumBits());
		FShooterAudioMoveData Received;
		TestTrue(TEXT("Packed movement input deserializes"), Received.Serialize(*Movement, Reader, nullptr, EMoveType::NewMove));
		TestFalse(TEXT("Movement packet roundtrip has no archive error"), Writer.IsError() || Reader.IsError());
		TestTrue(TEXT("Movement packet preserves request order and held intent"), Received.MovementInput == Sent.MovementInput);
		TestEqual(TEXT("Movement packet preserves predicted Slide request"), Received.PredictedSlideRequest, Sent.PredictedSlideRequest);
		TestEqual(TEXT("Movement packet preserves predicted active state"), Received.bPredictedSliding, Sent.bPredictedSliding);
		TestEqual(TEXT("Movement packet preserves effective Sprint intent"), Received.bPredictedSprintHeld, Sent.bPredictedSprintHeld);
		TestEqual(TEXT("Movement packet preserves effective crouch intent"), Received.bPredictedCrouchHeld, Sent.bPredictedCrouchHeld);
		TestEqual(TEXT("Movement packet preserves actual capsule posture"), Received.bPredictedCrouched, Sent.bPredictedCrouched);
		TestEqual(TEXT("Movement packet preserves predicted end reason"), Received.PredictedEndReason, Sent.PredictedEndReason);
		TestEqual(TEXT("Movement packet preserves existing movement audio ID"), Received.AudioMoveId, Sent.AudioMoveId);
		TestEqual(TEXT("Movement packet preserves existing airborne audio ID"), Received.AudioAirborneId, Sent.AudioAirborneId);

		FShooterMoveResponse Response;
		Response.ClientAdjustment.TimeStamp = 1.0f;
		Response.ClientAdjustment.MovementMode = MOVE_Walking;
		Response.SlideSnapshot = Movement->CaptureSlideSnapshot();
		Response.SlideSnapshot.ActiveRequest = 17;
		Response.SlideSnapshot.ProcessedCrouchRequest = 17;
		Response.SlideSnapshot.ProcessedJumpCancelRequest = 16;
		Response.SlideSnapshot.AppliedInput = Sent.MovementInput;
		Response.SlideSnapshot.Elapsed = 0.375f;
		Response.SlideSnapshot.CurrentSpeed = 379.2f;
		Response.SlideSnapshot.bCrouched = true;
		Response.JumpSnapshot.JumpCount = 1;
		Response.JumpSnapshot.Coyote.RemainingTime = 0.025f;
		FNetBitWriter ResponseWriter(nullptr, 8192);
		if (!TestTrue(TEXT("Slide correction serializes"), Response.Serialize(*Movement, ResponseWriter, nullptr))) return false;
		FNetBitReader ResponseReader(nullptr, ResponseWriter.GetData(), ResponseWriter.GetNumBits());
		FShooterMoveResponse ReceivedResponse;
		TestTrue(TEXT("Slide correction deserializes"), ReceivedResponse.Serialize(*Movement, ResponseReader, nullptr));
		TestFalse(TEXT("Slide correction roundtrip has no archive error"), ResponseWriter.IsError() || ResponseReader.IsError());
		const auto& Snapshot = ReceivedResponse.SlideSnapshot;
		TestTrue(TEXT("Correction preserves active Slide"), Snapshot.bActive);
		TestEqual(TEXT("Correction preserves actual capsule posture"), Snapshot.bCrouched, Response.SlideSnapshot.bCrouched);
		TestTrue(TEXT("Correction preserves applied input baseline"), Snapshot.AppliedInput == Response.SlideSnapshot.AppliedInput);
		TestEqual(TEXT("Correction preserves request watermark"), Snapshot.ProcessedCrouchRequest, uint32(17));
		TestEqual(TEXT("Correction preserves cancel watermark"), Snapshot.ProcessedJumpCancelRequest, uint32(16));
		TestEqual(TEXT("Correction preserves active request"), Snapshot.ActiveRequest, uint32(17));
		TestEqual(TEXT("Correction preserves entry direction"), Snapshot.Direction, Response.SlideSnapshot.Direction);
		TestEqual(TEXT("Correction preserves entry speed"), Snapshot.StartSpeed, Response.SlideSnapshot.StartSpeed);
		TestEqual(TEXT("Correction preserves monotone speed state"), Snapshot.CurrentSpeed, 379.2f);
		TestEqual(TEXT("Correction preserves simulation time"), Snapshot.Elapsed, 0.375f);
		TestEqual(TEXT("Correction preserves duration"), Snapshot.Duration, Response.SlideSnapshot.Duration);
		TestEqual(TEXT("Slide extension preserves existing jump snapshot"), ReceivedResponse.JumpSnapshot.JumpCount, 1);
		TestEqual(TEXT("Slide extension preserves existing coyote snapshot"), ReceivedResponse.JumpSnapshot.Coyote.RemainingTime, 0.025f);
		AddInfo(TEXT("[SlidePredictionTest] Case=Serialization MovementAndCorrection=Completed"));

		// 위치가 일치해도 Slide 상태가 다르면 서버가 보정해야 한다. 전송 지연 후에도 판정 시점의 상태를 보낸다.
		FShooterAudioMoveData MismatchedMove;
		MismatchedMove.TimeStamp = 2.0f;
		MismatchedMove.bPredictedSliding = false;
		Movement->SetCurrentNetworkMoveData(&MismatchedMove);
		Movement->ServerMoveHandleClientError(2.0f, 0.05f, FVector::ZeroVector,
			Fixture.Shooter->GetActorLocation(), nullptr, NAME_None, MOVE_Walking);
		Movement->SetCurrentNetworkMoveData(nullptr);
		auto* ServerData = Movement->GetPredictionData_Server_Character();
		TestEqual(TEXT("State-only mismatch queues correction at matching timestamp"), ServerData->PendingAdjustment.TimeStamp, 2.0f);
		TestFalse(TEXT("State-only mismatch is not acknowledged as a good move"), ServerData->PendingAdjustment.bAckGoodMove);
		const float QueuedElapsed = Movement->GetSlideElapsedTime();
		Movement->PerformMovement(0.05f);
		FShooterMoveResponse DelayedResponse;
		DelayedResponse.ServerFillResponseData(*Movement, ServerData->PendingAdjustment);
		TestEqual(TEXT("Delayed response keeps queued simulation time"), DelayedResponse.SlideSnapshot.Elapsed, QueuedElapsed);
		TestTrue(TEXT("Simulation progressed after correction was queued"), Movement->GetSlideElapsedTime() > QueuedElapsed);
		AddInfo(FString::Printf(TEXT("[SlidePredictionTest] Case=StateOnlyCorrection QueuedElapsed=%.6f ResponseElapsed=%.6f AckGood=%d"),
			QueuedElapsed, DelayedResponse.SlideSnapshot.Elapsed, ServerData->PendingAdjustment.bAckGoodMove));

		FShooterSlideSnapshot LockSnapshot = Movement->CaptureSlideSnapshot();
		Fixture.Shooter->BeginActionLock(EShooterActionLock::Reload);
		Movement->RestoreSlideSnapshot(LockSnapshot);
		TestEqual(TEXT("Restoring active Slide cannot replace an unrelated action lock"), Fixture.Shooter->GetActionLock(), EShooterActionLock::Reload);
		LockSnapshot.bActive = false;
		Movement->RestoreSlideSnapshot(LockSnapshot);
		TestEqual(TEXT("Restoring inactive Slide cannot release an unrelated action lock"), Fixture.Shooter->GetActionLock(), EShooterActionLock::Reload);
		Fixture.Shooter->EndActionLock(EShooterActionLock::Reload);
		AddInfo(TEXT("[SlidePredictionTest] Case=UnrelatedLockRestore ChecksCompleted"));
	}

	// 별도 소켓은 열지 않는다. 클라이언트 역할의 독립 World에서 엔진의 실제 보정/저장 이동 재생을 실행한다.
	UClass* DriverClass = LoadClass<UNetDriver>(nullptr, TEXT("/Script/OnlineSubsystemUtils.IpNetDriver"));
	UClass* ConnectionClass = LoadClass<UNetConnection>(nullptr, TEXT("/Script/OnlineSubsystemUtils.IpConnection"));
	if (!TestNotNull(TEXT("Client fixture net driver class"), DriverClass)
		|| !TestNotNull(TEXT("Client fixture connection class"), ConnectionClass)) return false;
	for (int32 Case = 0; Case < 6; ++Case)
	{
		const TCHAR* Name = Case == 0 ? TEXT("RejectedRequest") : Case == 1 ? TEXT("ActiveCorrection")
			: Case == 2 ? TEXT("NewRequest") : Case == 3 ? TEXT("JumpCancelBoundary")
			: Case == 4 ? TEXT("FinishBoundary") : TEXT("LifecycleBoundary");
		FFixture Fixture;
		if (!Fixture.Initialize(*this)) return false;
		auto* Movement = Fixture.Movement;
		auto* Shooter = Fixture.Shooter;
		// 이 World는 BeginPlay 없이 물리만 직접 실행한다. 엔진 보정은 비활성 CMC를 무시하므로 명시적으로 활성화한다.
		Movement->Activate(true);
		if (!TestTrue(TEXT("Native correction fixture movement is active"), Movement->IsActive())) return false;
		UNetDriver* Driver = NewObject<UNetDriver>(Fixture.World, DriverClass);
		Driver->World = Fixture.World;
		UNetConnection* Connection = NewObject<UNetConnection>(Driver, ConnectionClass);
		Driver->ServerConnection = Connection;
		// 역할 판정용 연결에도 정리 시 사용할 Driver가 필요하다. 소켓/채널을 열지 않았으므로 전송은 닫힌 상태로 둔다.
		Connection->Driver = Driver;
		Connection->SetConnectionState(USOCK_Closed);
		// 실제 로그인 대기 연결이 아니므로 종료 시 전역 PendingConnectionLost 알림을 보내지 않는다.
		Connection->SetClientLoginState(EClientLoginState::ReceivedJoin);
		Fixture.World->SetNetDriver(Driver);
		Shooter->SetRole(ROLE_AutonomousProxy);
		ON_SCOPE_EXIT
		{
			// fixture의 일반 게임 정리 경로에 테스트용 연결/클라이언트 역할을 남기지 않는다.
			Shooter->SetRole(ROLE_Authority);
			// Driver를 먼저 끊으면 CleanUp의 로그인 상태 정리에서 null을 참조한다. 연결부터 정리한 뒤 World를 분리한다.
			Connection->CleanUp();
			TestNull(TEXT("Replay fixture cleanup detaches the server connection"), Driver->ServerConnection.Get());
			TestNull(TEXT("Replay fixture cleanup releases its driver"), Connection->Driver.Get());
			TestEqual(TEXT("Replay fixture cleanup completes its login state"), Connection->ClientLoginState, EClientLoginState::CleanedUp);
			// 에디터 종료 GC의 FinishDestroy도 CleanUp을 다시 호출한다. 같은 경로의 재호출이 안전한지 즉시 검증한다.
			Connection->CleanUp();
			TestNull(TEXT("Repeated replay fixture cleanup keeps the driver detached"), Connection->Driver.Get());
			TestEqual(TEXT("Repeated replay fixture cleanup preserves the completed state"), Connection->ClientLoginState, EClientLoginState::CleanedUp);
			AddInfo(FString::Printf(TEXT("[SlidePredictionTest] Case=ConnectionCleanup Scenario=%s DriverDetached=%d ConnectionDetached=%d Repeated=1"),
				Name, Connection->Driver == nullptr, Driver->ServerConnection == nullptr));
			Fixture.World->SetNetDriver(nullptr);
			Driver->World = nullptr;
		};
		if (!TestEqual(TEXT("Native replay fixture uses client net mode"), Movement->GetNetMode(), NM_Client)) return false;
		auto* ClientData = static_cast<FNetworkPredictionData_Client_Character*>(Movement->GetPredictionData_Client());
		FShooterMovementInput Input;
		Input.CrouchRequest = 10;
		Input.bSprint = Input.bCrouch = Input.bSprintAtCrouch = true;
		Movement->MoveInput = Movement->AppliedMovementInput = Movement->LiveMovementInput = Input;
		Movement->ProcessedCrouchRequest = Movement->ActiveSlideRequest = 10;
		Movement->PerformMovement(0.25f);
		FShooterSlideSnapshot ServerSlide = Movement->CaptureSlideSnapshot();
		const FShooterJumpSnapshot ServerJump = Movement->CaptureJumpSnapshot();
		FVector ServerLocation = Shooter->GetActorLocation();
		FVector ServerVelocity = Movement->Velocity;
		UCapsuleComponent* Capsule = Shooter->GetCapsuleComponent();
		const float StandingHalfHeight = Shooter->GetClass()->GetDefaultObject<AShooterCharacter>()->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
		if (Case == 0 || Case == 5)
		{
			// 서버가 이미 거절한 요청의 입력을 재전송/재생해도 진입 조건을 다시 평가해서는 안 된다.
			ServerSlide.bActive = false;
			ServerSlide.bSprintHeld = ServerSlide.bCrouchHeld = false;
			ServerSlide.bCrouched = false;
			// 거절한 서버는 서 있는 캡슐 중심을 보낸다. 예측의 낮아진 중심을 서버 위치로 위장하지 않는다.
			ServerLocation.Z += (StandingHalfHeight - Capsule->GetUnscaledCapsuleHalfHeight())
				* Capsule->GetScaledCapsuleHalfHeight() / Capsule->GetUnscaledCapsuleHalfHeight();
			ServerSlide.EndReason = uint8(ESlideEndReason::ForcedCancel);
			if (Case == 5)
			{
				// 새 위치/조종에서 승인한 비활성 상태 뒤로 이전 수명주기의 SavedMove가 남아 있는 경우다.
				ServerSlide.AppliedInput = {};
				ServerSlide.AppliedInput.Epoch = 1;
			}
		}
		else if (Case == 2)
		{
			Fixture.Input->StopSlide(ESlideEndReason::ForcedCancel);
			Fixture.Input->HandleCrouchReleased();
			Movement->PerformMovement(1.0f / 60.0f);
			Movement->Velocity = FVector(400.0, 0.0, 0.0);
			ServerLocation = Shooter->GetActorLocation();
			ServerVelocity = Movement->Velocity;
			ServerSlide = Movement->CaptureSlideSnapshot();
			ServerSlide.AppliedInput = {};
			ServerSlide.ProcessedCrouchRequest = 9;
			Movement->RestoreSlideSnapshot(ServerSlide);
		}
		else if (Case == 4)
		{
			ServerSlide.Elapsed = 0.95f;
			ServerSlide.CurrentSpeed = 224.64f;
			ServerVelocity = FVector(ServerSlide.CurrentSpeed, 0.0, 0.0);
		}

		const auto SaveMove = [&](float TimeStamp)
		{
			FSavedMovePtr Move = ClientData->AllocateNewMove();
			Move->Clear();
			Move->SetMoveFor(Shooter, 0.05f, FVector::ZeroVector, *ClientData);
			Move->TimeStamp = TimeStamp;
			ClientData->SavedMoves.Add(Move);
			return Move;
		};
		FSavedMovePtr Acked = SaveMove(1.0f);
		if (Case == 2)
		{
			// 같은 이동 전에 앉기/Shift를 놓아도 누른 순간의 Slide 요청과 진입 의도는 남아 있어야 한다.
			Input.bCrouch = false;
			Input.bSprint = false;
		}
		for (int32 Index = 0; Index < 2; ++Index)
		{
			if (Case == 3) Input.JumpCancelRequest = 11;
			Movement->MoveInput = Movement->LiveMovementInput = Input;
			Movement->bUsingMovementInput = true;
			Shooter->CheckJumpInput(0.05f);
			FSavedMovePtr Move = SaveMove(1.05f + Index * 0.05f);
			Movement->PerformMovement(0.05f);
			Move->PostUpdate(Shooter, FSavedMove_Character::PostUpdate_Record);
			Movement->bUsingMovementInput = false;
		}
		TestFalse(TEXT("Slide or custom input boundary does not combine"),
			Acked->CanCombineWith(ClientData->SavedMoves.Last(), Shooter, 0.125f));
		if (Case == 2 || Case == 3)
			TestTrue(TEXT("Changed Slide input remains an important move for resend"), ClientData->SavedMoves.Last()->IsImportantMove(Acked));

		// 실제 packed 응답도 CMC에 등록된 컨테이너를 채운 뒤 엔진 보정 경로로 전달한다.
		FShooterMoveResponse& Response = Movement->MoveResponse;
		Response.ClientAdjustment.TimeStamp = 1.0f;
		Response.ClientAdjustment.NewLoc = ServerLocation;
		Response.ClientAdjustment.NewVel = ServerVelocity;
		Response.ClientAdjustment.MovementMode = MOVE_Walking;
		Response.SlideSnapshot = ServerSlide;
		Response.JumpSnapshot = ServerJump;
		const int32 JumpCount = Shooter->JumpCurrentCount;
		if (!TestTrue(TEXT("Correction timestamp identifies an unacknowledged saved move"),
			ClientData->GetSavedMoveIndex(Response.ClientAdjustment.TimeStamp) != INDEX_NONE)) continue;
		Movement->ClientHandleMoveResponse(Response);
		// 보정 자체가 거부되면 후속 상태 비교를 멈춰 같은 원인으로 수십 개의 실패 로그가 생기지 않게 한다.
		if (!TestTrue(TEXT("Native correction schedules replay"), ClientData->bUpdatePosition)) continue;
		TestTrue(TEXT("Native correction acknowledges the matching move"),
			ClientData->LastAckedMove.IsValid() && ClientData->LastAckedMove->TimeStamp == 1.0f);
		TestEqual(TEXT("Native correction leaves exactly two unacknowledged moves"), ClientData->SavedMoves.Num(), 2);
		TestTrue(TEXT("Native correction applies server location before replay"), Shooter->GetActorLocation().Equals(ServerLocation, 0.01));
		TestEqual(TEXT("Slide restore preserves native corrected velocity"), Movement->Velocity, ServerVelocity);
		TestEqual(TEXT("Correction restores authoritative crouched posture before replay"), bool(Shooter->bIsCrouched), ServerSlide.bCrouched);
		const float ExpectedHalfHeight = ServerSlide.bCrouched
			? FMath::Max(Movement->GetCrouchedHalfHeight(), Capsule->GetUnscaledCapsuleRadius()) : StandingHalfHeight;
		TestTrue(TEXT("Correction restores matching capsule height before replay"),
			FMath::IsNearlyEqual(Capsule->GetUnscaledCapsuleHalfHeight(), ExpectedHalfHeight, 0.01f));
		const uint32 BeforeReplaySerial = Movement->SlideSimulationSerial;
		const FVector BeforeReplay = Shooter->GetActorLocation();
		if (!TestTrue(TEXT("Native saved-move replay executes"), Movement->ClientUpdatePositionAfterServerUpdate())) continue;
		TestFalse(TEXT("Native replay consumes the update request"), ClientData->bUpdatePosition);
		TestFalse(TEXT("Replayed position remains finite"), Shooter->GetActorLocation().ContainsNaN());
		TestEqual(TEXT("Slide replay does not consume jump count"), Shooter->JumpCurrentCount, JumpCount);
		if (Case == 1 || Case == 2)
		{
			TestTrue(TEXT("Approved/new Slide remains active after replay"), Movement->IsSlideSimulating());
			TestTrue(TEXT("Replay advances from server state without restarting each move"),
				FMath::IsNearlyEqual(Movement->GetSlideElapsedTime(), Case == 1 ? 0.35f : 0.1f, 0.0001f));
			TestEqual(TEXT("Replayed sticky request starts at most once"), Movement->SlideSimulationSerial - BeforeReplaySerial, uint32(Case == 2 ? 1 : 0));
			TestTrue(TEXT("Replayed Slide advances actual position"), Shooter->GetActorLocation().X > BeforeReplay.X + 20.0);
			TestTrue(TEXT("Entry speed multiplier is not applied again during replay"), FMath::IsNearlyEqual(Movement->SlideStartSpeed, 480.0f, 0.01f));
			if (Case == 2)
			{
				TestFalse(TEXT("Replay preserves released crouch hold after a short tap"), Fixture.Input->WantsToCrouch());
				TestFalse(TEXT("Replay preserves released Sprint hold after entry"), Fixture.Input->WantsToSprint());
			}
		}
		else
		{
			Fixture.CheckRestored(*this);
			TestEqual(TEXT("Rejected/canceled/finished Slide is not restarted by later saved moves"), Movement->SlideSimulationSerial, BeforeReplaySerial);
			if (Case == 3) TestEqual(TEXT("Replay retains JumpCancel reason"), Movement->SlideEndReason, uint8(ESlideEndReason::JumpCancel));
			if (Case == 5)
			{
				TestEqual(TEXT("Native correction adopts the server lifecycle"), Movement->MovementEpoch, uint32(1));
				TestFalse(TEXT("Old-lifecycle replay cannot restore Sprint hold"), Fixture.Input->WantsToSprint());
				TestFalse(TEXT("Old-lifecycle replay cannot restore crouch hold"), Fixture.Input->WantsToCrouch());
			}
			if (Case == 4)
			{
				// PhysSlide는 MIN_TICK_TIME 미만의 잔여 시간을 종료로 처리하므로 같은 기준으로 검증한다.
				const float RemainingSlideTime = ServerSlide.Duration - Movement->GetSlideElapsedTime();
				TestTrue(TEXT("Natural finish reaches configured simulation duration"),
					RemainingSlideTime >= 0.0f && RemainingSlideTime < UCharacterMovementComponent::MIN_TICK_TIME);
				TestEqual(TEXT("Natural finish retains Finished reason"), Movement->SlideEndReason, uint8(ESlideEndReason::Finished));
			}
		}
		AddInfo(FString::Printf(TEXT("[SlidePredictionTest] Case=%s CorrectionTime=1.000 ReplayMoves=2 SimElapsed=%.9f DistanceCm=%.3f Active=%d NewStarts=%u"),
			Name, Movement->GetSlideElapsedTime(), FVector::Dist2D(BeforeReplay, Shooter->GetActorLocation()),
			Movement->IsSlideSimulating(), Movement->SlideSimulationSerial - BeforeReplaySerial));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterSlideLifecycleTest,
	"Outlier.Shooter.Slide.Lifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterSlideLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace ShooterSlideSimulationTests;
	(void)Parameters;
	for (int32 Case = 0; Case < 6; ++Case)
	{
		const TCHAR* Name = Case == 0 ? TEXT("Death") : Case == 1 ? TEXT("SuitTransition")
			: Case == 2 ? TEXT("Teleport") : Case == 3 ? TEXT("UnPossess")
			: Case == 4 ? TEXT("ControllerReplication") : TEXT("EndPlay");
		FFixture Fixture;
		if (!Fixture.Initialize(*this)) return false;
		auto* Movement = Fixture.Movement;
		auto* Shooter = Fixture.Shooter;
		Movement->PerformMovement(0.1f);
		const FVector Velocity = Movement->Velocity;
		const int32 JumpCount = Shooter->JumpCurrentCount;
		Movement->NextMovementRequest = 10;
		Movement->AppliedMovementInput.CrouchRequest = Movement->ActiveSlideRequest = 10;
		Movement->LiveMovementInput = Movement->MoveInput = Movement->AppliedMovementInput;
		const FShooterSlideSnapshot OldSlide = Movement->CaptureSlideSnapshot();
		auto* ClientData = static_cast<FNetworkPredictionData_Client_Character*>(Movement->GetPredictionData_Client());
		ClientData->CurrentTimeStamp = 2.0f;
		FSavedMovePtr SavedMove = ClientData->AllocateNewMove();
		SavedMove->SetMoveFor(Shooter, 0.05f, FVector::ZeroVector, *ClientData);
		ClientData->SavedMoves.Add(SavedMove);
		ClientData->PendingMove = ClientData->LastAckedMove = SavedMove;
		ClientData->bUpdatePosition = true;
		// BeginPlay 없는 fixture에서도 수명주기 정리 경로에 이전 카메라 결과가 남는지 확인한다.
		Shooter->CurrentSlideCameraEffectAlpha = Shooter->TargetSlideCameraEffectAlpha = 1.0f;
		Shooter->ActiveSlideCameraRollDegrees = 10.0f;
		switch (Case)
		{
		case 0: Shooter->HandleDeath(); break;
		case 1: Shooter->PrepareForSuitTransition(); break;
		case 2:
			TestTrue(TEXT("Actual teleport succeeds"), Shooter->TeleportTo(
				Shooter->GetActorLocation() + FVector(500.0, 0.0, 0.0), FRotator::ZeroRotator, false, true));
			break;
		case 3: Shooter->UnPossessed(); break;
		case 4: Shooter->OnRep_Controller(); break;
		case 5: Shooter->EndPlay(EEndPlayReason::RemovedFromWorld); break;
		}
		Fixture.CheckRestored(*this);
		TestEqual(TEXT("Lifecycle preserves request sequence"), Movement->NextMovementRequest, uint32(10));
		TestFalse(TEXT("Lifecycle clears effective Sprint hold"), Fixture.Input->WantsToSprint());
		TestFalse(TEXT("Lifecycle clears effective crouch hold"), Fixture.Input->WantsToCrouch());
		TestFalse(TEXT("Lifecycle clears live Sprint/crouch input"), Movement->LiveMovementInput.bSprint || Movement->LiveMovementInput.bCrouch);
		TestEqual(TEXT("Lifecycle clears active request"), Movement->ActiveSlideRequest, uint32(0));
		TestEqual(TEXT("Lifecycle clears presented request"), Movement->PresentedSlideRequest, uint32(0));
		TestFalse(TEXT("Lifecycle clears Slide presentation"), Movement->bSlidePresented);
		TestEqual(TEXT("Lifecycle clears Slide time"), Movement->GetSlideElapsedTime(), 0.0f);
		TestEqual(TEXT("Lifecycle clears Slide camera roll"), Shooter->GetCurrentSlideCameraRollDegrees(), 0.0f);
		TestEqual(TEXT("Lifecycle clears Slide camera interpolation"), Shooter->CurrentSlideCameraEffectAlpha, 0.0f);
		TestEqual(TEXT("Lifecycle does not consume a jump"), Shooter->JumpCurrentCount, JumpCount);
		TestEqual(TEXT("Lifecycle clears saved moves"), ClientData->SavedMoves.Num(), 0);
		TestFalse(TEXT("Lifecycle clears pending/acked moves"), ClientData->PendingMove.IsValid() || ClientData->LastAckedMove.IsValid());
		TestFalse(TEXT("Lifecycle clears pending replay"), ClientData->bUpdatePosition);
		TestEqual(TEXT("Lifecycle preserves movement timestamp"), ClientData->CurrentTimeStamp, 2.0f);
		if (Case != 0) TestEqual(TEXT("Slide cleanup does not reset remaining velocity"), Movement->Velocity, Velocity);
		const uint32 Epoch = Movement->MovementEpoch;
		TestTrue(TEXT("Server advances the lifecycle"), Epoch > OldSlide.AppliedInput.Epoch);
		Movement->RestoreSlideSnapshot(OldSlide);
		TestFalse(TEXT("Old lifecycle snapshot cannot revive Slide"), Movement->IsSlideSimulating());
		// 실제 수신 진입점도 지난 보정의 위치/상태를 적용하기 전에 버려야 한다.
		FShooterMoveResponse StaleResponse;
		StaleResponse.SlideSnapshot = OldSlide;
		StaleResponse.ClientAdjustment.TimeStamp = 1.0f;
		StaleResponse.ClientAdjustment.NewLoc = FVector(9000.0, 0.0, 0.0);
		const FVector Position = Shooter->GetActorLocation();
		Movement->ClientHandleMoveResponse(StaleResponse);
		TestEqual(TEXT("Old lifecycle correction cannot rewind position"), Shooter->GetActorLocation(), Position);
		Movement->ResetSlideForLifecycle();
		Fixture.CheckRestored(*this);
		if (Case != 0 && Case != 5)
		{
			// 전환 경계 뒤의 일반 이동을 복구하고, 이전 패킷과 새 입력을 같은 진입점에서 비교한다.
			Movement->SetMovementMode(MOVE_Walking);
			// UnCrouch는 요청만 바꾼다. 실제 캡슐을 일으킨 다음 새 Sprint/Slide 입력을 판정한다.
			Movement->PerformMovement(1.0f / 60.0f);
			if (!TestFalse(TEXT("Re-entry fixture has completed physical uncrouch"), bool(Shooter->bIsCrouched))
				|| !TestTrue(TEXT("Re-entry fixture has a real walkable floor"), Movement->CurrentFloor.IsWalkableFloor())) return false;
			Movement->Velocity = FVector(400.0, 0.0, 0.0);
			Movement->bUsingMovementInput = true;
			Movement->MoveInput = OldSlide.AppliedInput;
			Movement->MoveInput.bSprint = Movement->MoveInput.bCrouch = Movement->MoveInput.bSprintAtCrouch = true;
			Movement->MoveInput.CrouchRequest = 11;
			Movement->ApplyMovementInput();
			TestFalse(TEXT("Delayed old-lifecycle request cannot start Slide"), Movement->IsSlideSimulating());
			Movement->MoveInput.Epoch = Movement->MovementEpoch;
			Movement->ApplyMovementInput();
			TestTrue(TEXT("New-lifecycle input can start a fresh Slide"), Movement->IsSlideSimulating());
			Movement->bUsingMovementInput = false;
			Fixture.Input->StopSlide(ESlideEndReason::ForcedCancel);
			Fixture.CheckRestored(*this);
		}
		AddInfo(FString::Printf(TEXT("[SlideLifecycleTest] Case=%s Epoch=%u SavedMoves=%d Active=%d CameraRoll=%.3f"),
			Name, Movement->MovementEpoch, ClientData->SavedMoves.Num(), Movement->IsSlideSimulating(), Shooter->GetCurrentSlideCameraRollDegrees()));
		if (Case == 0)
		{
			// GameMode의 Pair 리스폰 전체가 아니라, 교체 Pawn이 이전 이동/잠금을 상속하지 않는 경계를 검증한다.
			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AShooterCharacter* Replacement = Fixture.World->SpawnActor<AShooterCharacter>(Shooter->GetClass(),
				FVector(1000.0, 0.0, 200.0), FRotator::ZeroRotator, Spawn);
			if (!TestNotNull(TEXT("Replacement Shooter Pawn"), Replacement)) return false;
			auto* ReplacementMovement = CastChecked<UShooterCharacterMovementComponent>(Replacement->GetCharacterMovement());
			TestFalse(TEXT("Replacement Pawn has no previous Slide"), ReplacementMovement->IsSlideSimulating());
			TestFalse(TEXT("Replacement Pawn has no previous action lock"), Replacement->IsActionLocked());
			TestEqual(TEXT("Replacement Pawn has no previous Slide request"), ReplacementMovement->ActiveSlideRequest, uint32(0));
			TestEqual(TEXT("Replacement Pawn has a fresh lifecycle"), ReplacementMovement->MovementEpoch, uint32(0));
			TestEqual(TEXT("Replacement Pawn has original friction"), ReplacementMovement->GroundFriction, Fixture.Friction);
			TestEqual(TEXT("Replacement Pawn has original acceleration"), ReplacementMovement->MaxAcceleration, Fixture.Acceleration);
			TestEqual(TEXT("Replacement Pawn has no previous camera roll"), Replacement->GetCurrentSlideCameraRollDegrees(), 0.0f);
			AddInfo(TEXT("[SlideLifecycleTest] Case=ReplacementPawn PreviousSlideAndLock=Cleared"));
		}
	}

	// 반복 보정은 승인된 요청의 시작 연출을 다시 내보내지 않고, 중복 취소도 한 번만 종료한다.
	{
		FFixture Fixture;
		if (!Fixture.Initialize(*this)) return false;
		auto* Movement = Fixture.Movement;
		Movement->ActiveSlideRequest = Movement->PresentedSlideRequest = 20;
		const auto Snapshot = Movement->CaptureSlideSnapshot();
		const uint32 Starts = Movement->SlidePresentationStarts;
		const uint32 Stops = Movement->SlidePresentationStops;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			Movement->bRestoringMovementState = true;
			Movement->OnTeleported();
			Movement->RestoreSlideSnapshot(Snapshot);
			Movement->ReconcileSlidePresentation();
			Movement->bRestoringMovementState = false;
			Movement->ReconcileSlidePresentation();
		}
		TestEqual(TEXT("Correction retains the lifecycle"), Movement->MovementEpoch, Snapshot.AppliedInput.Epoch);
		TestEqual(TEXT("Same approved Slide has no duplicate presentation starts"), Movement->SlidePresentationStarts, Starts);
		Fixture.Input->StopSlide(ESlideEndReason::ForcedCancel);
		Fixture.Input->StopSlide(ESlideEndReason::ForcedCancel);
		TestEqual(TEXT("Repeated cancellation dispatches one presentation stop"), Movement->SlidePresentationStops - Stops, uint32(1));
		Fixture.CheckRestored(*this);
		FShooterSlideSnapshot NextLifecycle = Snapshot;
		NextLifecycle.AppliedInput.Epoch = Snapshot.AppliedInput.Epoch + 1;
		Movement->RestoreSlideSnapshot(NextLifecycle);
		Movement->ReconcileSlidePresentation();
		Movement->ReconcileSlidePresentation();
		TestEqual(TEXT("Same request number in a new lifecycle starts presentation once"), Movement->SlidePresentationStarts - Starts, uint32(1));
		Fixture.Input->StopSlide(ESlideEndReason::ForcedCancel);
		Fixture.CheckRestored(*this);
		AddInfo(TEXT("[SlideLifecycleTest] Case=Presentation CorrectionRepeats=3 DuplicateStarts=0 StopRequests=1"));
	}
	{
		FFixture Fixture;
		if (!Fixture.Initialize(*this)) return false;
		auto* Movement = Fixture.Movement;
		Fixture.Shooter->SetRole(ROLE_AutonomousProxy);
		ON_SCOPE_EXIT { Fixture.Shooter->SetRole(ROLE_Authority); };
		Movement->ResetSlideForLifecycle();
		TestEqual(TEXT("Local cleanup cannot invent a server lifecycle"), Movement->MovementEpoch, uint32(0));
		FShooterSlideSnapshot ServerSlide = Movement->CaptureSlideSnapshot();
		ServerSlide.AppliedInput.Epoch = 3;
		ServerSlide.ProcessedCrouchRequest = 80;
		ServerSlide.ProcessedJumpCancelRequest = 81;
		Movement->RestoreSlideSnapshot(ServerSlide);
		TestEqual(TEXT("New owner adopts the server lifecycle"), Movement->MovementEpoch, uint32(3));
		TestEqual(TEXT("New owner issues requests beyond the server watermark"), Movement->NextMovementRequest, uint32(81));
		Fixture.CheckRestored(*this);
		Fixture.Shooter->BeginActionLock(EShooterActionLock::Reload);
		Movement->ResetSlideForLifecycle();
		TestEqual(TEXT("Slide cleanup preserves an unrelated Reload lock"), Fixture.Shooter->GetActionLock(), EShooterActionLock::Reload);
		Fixture.Shooter->EndActionLock(EShooterActionLock::Reload);
		Fixture.CheckRestored(*this);
		AddInfo(TEXT("[SlideLifecycleTest] Case=Ownership Epoch=3 NextRequestBaseline=81"));
	}

	// 실제 충돌은 Walking이 처리한다. 연결되지 않은 WallCancel 정책은 자동으로 활성화하지 않는다.
	const auto AddBlocker = [](FFixture& Fixture, const FVector& Center, const FVector& Extent)
	{
		AActor* Actor = Fixture.World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
		Actor->SetRootComponent(Box);
		Box->SetBoxExtent(Extent);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetMobility(EComponentMobility::Static);
		Box->SetWorldLocation(Center);
		Box->RegisterComponent();
		return Actor;
	};
	{
		FFixture Fixture;
		if (!Fixture.Initialize(*this)) return false;
		const float WallFront = Fixture.Shooter->GetActorLocation().X + 100.0f;
		AddBlocker(Fixture, FVector(WallFront + 20.0f, 0.0, 200.0), FVector(20.0, 1000.0, 300.0));
		for (int32 Frame = 0; Frame < 65; ++Frame) Fixture.Movement->PerformMovement(1.0f / 60.0f);
		TestTrue(TEXT("Actual wall blocks the capsule"), Fixture.Shooter->GetActorLocation().X
			+ Fixture.Shooter->GetCapsuleComponent()->GetScaledCapsuleRadius() <= WallFront + 0.1f);
		TestEqual(TEXT("Unbound WallCancel is not enabled by collision"), Fixture.Movement->SlideEndReason, uint8(ESlideEndReason::Finished));
		Fixture.CheckRestored(*this);
		AddInfo(FString::Printf(TEXT("[SlideLifecycleTest] Case=ActualWall DistanceCm=%.3f EndReason=Finished"), Fixture.Distance()));
	}
	{
		FFixture Fixture;
		if (!Fixture.Initialize(*this)) return false;
		// 호환용 명시 호출만 검증한다. 실제 충돌이 이 함수를 호출하도록 바인딩하지 않는다.
		Fixture.Input->HandleCrouchReleased();
		Fixture.Input->HandleCrouchPressed();
		Fixture.Input->HandleSprintReleased();
		FHitResult Hit;
		Fixture.Shooter->SlideWallStopDotThreshold = 0.5f;
		Hit.bBlockingHit = true;
		Hit.ImpactNormal = FVector(-1.0, 0.0, 0.0);
		Fixture.Shooter->HandleSlideWallHit(Hit);
		TestEqual(TEXT("Explicit compatibility wall callback retains WallCancel"), Fixture.Movement->SlideEndReason, uint8(ESlideEndReason::WallCancel));
		TestTrue(TEXT("WallCancel respects re-pressed crouch hold"), Fixture.Input->WantsToCrouch());
		TestFalse(TEXT("WallCancel cannot resume released Sprint"), Fixture.Input->WantsToSprint());
		Fixture.CheckRestored(*this);
		AddInfo(TEXT("[SlideLifecycleTest] Case=ExplicitWallCallback EndReason=WallCancel CrouchHeld=1 SprintHeld=0"));
	}
	{
		FFixture Fixture;
		if (!Fixture.Initialize(*this)) return false;
		Fixture.Movement->PerformMovement(1.0f / 60.0f);
		UCapsuleComponent* Capsule = Fixture.Shooter->GetCapsuleComponent();
		if (!TestTrue(TEXT("Ceiling fixture is actually crouched"), bool(Fixture.Shooter->bIsCrouched))) return false;
		const FVector Center = Fixture.Shooter->GetActorLocation();
		AActor* Ceiling = AddBlocker(Fixture, FVector(Center.X, Center.Y, Center.Z + Capsule->GetScaledCapsuleHalfHeight() + 18.0f),
			FVector(1000.0, 1000.0, 10.0));
		Fixture.Input->HandleCrouchReleased();
		Fixture.Input->StopSlide(ESlideEndReason::Finished);
		Fixture.Movement->PerformMovement(1.0f / 60.0f);
		TestTrue(TEXT("Low ceiling preserves actual crouched posture"), bool(Fixture.Shooter->bIsCrouched));
		TestFalse(TEXT("Low ceiling blocks Sprint after released Slide"), Fixture.Input->IsSprinting());
		Fixture.CheckRestored(*this);
		Ceiling->Destroy();
		Fixture.Movement->PerformMovement(1.0f / 60.0f);
		TestFalse(TEXT("Standing space allows uncrouch"), bool(Fixture.Shooter->bIsCrouched));
		TestTrue(TEXT("Standing space resumes held Sprint"), Fixture.Input->IsSprinting());
		AddInfo(TEXT("[SlideLifecycleTest] Case=LowCeiling BlockedStand=1 ResumedSprint=1"));
	}
	for (int32 Case = 0; Case < 4; ++Case)
	{
		FFixture Fixture;
		if (!Fixture.Initialize(*this)) return false;
		const int32 JumpCount = Fixture.Shooter->JumpCurrentCount;
		// BeginPlay 없는 fixture에서도 Launch/AddImpulse의 IsActive 검증을 통과해야 실제 외력이 예약된다.
		Fixture.Movement->Activate(true);
		if (!TestTrue(TEXT("External movement fixture CMC is active"), Fixture.Movement->IsActive())) return false;
		if (Case == 0) Fixture.Shooter->LaunchCharacter(FVector(100.0, 0.0, 500.0), true, true);
		else if (Case == 1) Fixture.Movement->AddImpulse(FVector(0.0, 0.0, 500.0), true);
		else Fixture.Movement->AddImpulse(Case == 2 ? FVector::ZeroVector : FVector(0.0, 100.0, 0.0), true);
		const FVector QueuedExternalVelocity = Case == 0
			? Fixture.Movement->PendingLaunchVelocity : Fixture.Movement->PendingImpulseToApply;
		if (Case != 2 && !TestFalse(TEXT("External movement is actually queued before simulation"), QueuedExternalVelocity.IsNearlyZero())) return false;
		Fixture.Movement->PerformMovement(1.0f / 60.0f);
		TestEqual(TEXT("External movement does not consume a jump"), Fixture.Shooter->JumpCurrentCount, JumpCount);
		if (Case < 2)
		{
			Fixture.CheckRestored(*this);
			TestTrue(TEXT("Vertical external movement enters Falling"), Fixture.Movement->IsFalling());
			TestTrue(TEXT("Vertical external velocity survives Slide cancellation"), Fixture.Movement->Velocity.Z > 0.0);
			TestEqual(TEXT("External falling retains existing FallCancel policy"), Fixture.Movement->SlideEndReason, uint8(ESlideEndReason::FallCancel));
		}
		else
		{
			TestTrue(TEXT("Zero/horizontal impulse retains existing grounded Slide policy"), Fixture.Movement->IsSlideSimulating());
			TestTrue(TEXT("Grounded Slide keeps its entry direction after impulse"), FMath::Abs(Fixture.Movement->Velocity.Y) < 0.01);
			TestTrue(TEXT("Grounded impulse does not add Slide propulsion"), Fixture.Movement->Velocity.Size2D() <= 480.0 + 0.01);
		}
		AddInfo(FString::Printf(TEXT("[SlideLifecycleTest] Case=%s CMCActive=%d QueuedExternal=%s Active=%d VelocityZ=%.3f"),
			Case == 0 ? TEXT("Launch") : Case == 1 ? TEXT("Impulse") : Case == 2 ? TEXT("ZeroImpulse") : TEXT("HorizontalImpulse"),
			Fixture.Movement->IsActive(), *QueuedExternalVelocity.ToString(), Fixture.Movement->IsSlideSimulating(), Fixture.Movement->Velocity.Z));
	}
	{
		FFixture Fixture;
		if (!Fixture.Initialize(*this)) return false;
		UClass* HammerClass = LoadClass<AWeaponBase>(nullptr, TEXT("/Game/Blueprints/Weapon/BP_Hammer.BP_Hammer_C"));
		if (!TestNotNull(TEXT("Slide melee fixture class"), HammerClass)) return false;
		AWeaponBase* Hammer = Fixture.World->SpawnActor<AWeaponBase>(HammerClass);
		if (!TestNotNull(TEXT("Slide melee fixture weapon"), Hammer)) return false;
		Hammer->SetActorEnableCollision(false);
		Fixture.Shooter->CurrentWeapon = Hammer;
		Fixture.Shooter->CurrentWeaponType = EWeaponType::Melee;
		TestEqual(TEXT("Slide melee fixture uses a melee weapon"), Fixture.Shooter->GetWeaponType(), EWeaponType::Melee);
		Fixture.Shooter->bAllowMeleeAttackWhileSliding = false;
		TestFalse(TEXT("Default policy rejects a new melee attack during Slide"), Fixture.Shooter->CanFireInCurrentState());
		Fixture.Shooter->bAllowMeleeAttackWhileSliding = true;
		TestTrue(TEXT("Explicit melee Slide option permits the existing attack policy"), Fixture.Shooter->CanFireInCurrentState());
		Fixture.Shooter->BeginActionLock(EShooterActionLock::Reload);
		TestFalse(TEXT("Unrelated Reload lock still blocks melee"), Fixture.Shooter->CanFireInCurrentState());
		Fixture.Shooter->EndActionLock(EShooterActionLock::Reload);
		Fixture.Input->StopSlide(ESlideEndReason::ForcedCancel);
		Fixture.CheckRestored(*this);
		AddInfo(TEXT("[SlideLifecycleTest] Case=Melee ExistingSlideOptionAndReloadLock=Completed"));
	}
	return true;
}

#endif
