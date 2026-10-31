#if WITH_DEV_AUTOMATION_TESTS

#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterCharacterMovementComponent.h"
#include "Shooter/ShooterMovementComponent.h"
#include "OutlierPlayerState.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UObject/CoreNet.h"
#include "UObject/UnrealType.h"

namespace ShooterSlideJumpTests
{
struct FFixture
{
	UWorld* World = nullptr;
	AShooterCharacter* Shooter = nullptr;
	UShooterCharacterMovementComponent* Movement = nullptr;
	UShooterMovementComponent* Input = nullptr;
	AOutlierPlayerState* PlayerState = nullptr;
	float GroundFriction = 0.0f;
	float Braking = 0.0f;
	float MaxAcceleration = 0.0f;

	UBoxComponent* AddBox(const FVector& Center, const FVector& Extent)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		if (!Actor) return nullptr;
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
		Actor->SetRootComponent(Box);
		Box->SetBoxExtent(Extent);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetMobility(EComponentMobility::Static);
		Box->SetWorldLocation(Center);
		Box->RegisterComponent();
		return Box;
	}

	bool Initialize(FAutomationTestBase& Test, bool bAcquiredSuit = false)
	{
		UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
			TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
		if (!Test.TestNotNull(TEXT("Concrete Slide jump Shooter class"), ShooterClass)) return false;
		FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
		const FName Name = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("SlideJump"),
			EUniqueObjectNameOptions::GloballyUnique);
		World = UWorld::CreateWorld(EWorldType::Game, false, Name, GetTransientPackage());
		if (!Test.TestNotNull(TEXT("Slide jump world"), World)) return false;
		World->AddToRoot();
		Context.SetCurrentWorld(World);
		if (!Test.TestNotNull(TEXT("Slide jump floor"), AddBox(FVector(0.0, 0.0, -50.0), FVector(10000.0, 10000.0, 50.0)))) return false;
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Shooter = World->SpawnActor<AShooterCharacter>(ShooterClass, FVector(0.0, 0.0, 200.0), FRotator::ZeroRotator, Spawn);
		PlayerState = World->SpawnActor<AOutlierPlayerState>();
		if (!Test.TestNotNull(TEXT("Slide jump Shooter"), Shooter)
			|| !Test.TestNotNull(TEXT("Slide jump PlayerState"), PlayerState)) return false;
		Shooter->SetPlayerState(PlayerState);
		PlayerState->SetAcquiredSuit(bAcquiredSuit);
		Movement = Cast<UShooterCharacterMovementComponent>(Shooter->GetCharacterMovement());
		Input = Shooter->FindComponentByClass<UShooterMovementComponent>();
		if (!Test.TestNotNull(TEXT("Slide jump movement"), Movement)
			|| !Test.TestNotNull(TEXT("Slide jump input"), Input)) return false;
		Movement->bRunPhysicsWithNoController = true;
		Movement->SetMovementMode(MOVE_Walking);
		Shooter->SetActorLocation(FVector(0.0, 0.0, Shooter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.15));
		Movement->bForceNextFloorCheck = true;
		Movement->PerformMovement(1.0f / 60.0f);
		if (!Test.TestTrue(TEXT("Slide jump fixture has a real floor"), Movement->CurrentFloor.IsWalkableFloor())) return false;
		const auto SetTuning = [this, &Test](const TCHAR* PropertyName, float Value)
		{
			FFloatProperty* Property = FindFProperty<FFloatProperty>(Shooter->GetClass(), PropertyName);
			if (!Test.TestNotNull(PropertyName, Property)) return false;
			Property->SetPropertyValue_InContainer(Shooter, Value);
			return true;
		};
		if (!SetTuning(TEXT("SlideDuration"), 1.0f) || !SetTuning(TEXT("MinSlideSpeed"), 200.0f)
			|| !SetTuning(TEXT("SlideSpeedMultiplier"), 1.2f) || !SetTuning(TEXT("SprintSpeed"), 600.0f)) return false;
		UCurveFloat* Curve = NewObject<UCurveFloat>(Shooter);
		Curve->FloatCurve.AddKey(0.0f, 1.0f);
		Curve->FloatCurve.AddKey(1.0f, 0.5f);
		FObjectProperty* CurveProperty = FindFProperty<FObjectProperty>(Shooter->GetClass(), TEXT("SlideSpeedCurve"));
		if (!Test.TestNotNull(TEXT("Slide curve tuning property"), CurveProperty)) return false;
		CurveProperty->SetObjectPropertyValue_InContainer(Shooter, Curve);
		// 런타임 기본 커브는 없다. 테스트도 사용할 Curve Float를 명시적으로 준비한다.
		UCurveFloat* JumpCurve = NewObject<UCurveFloat>(Movement);
		for (const FVector2D Key : {FVector2D(0.0, 1.0), FVector2D(0.2, 0.95), FVector2D(0.5, 0.65),
			FVector2D(0.8, 0.2), FVector2D(1.0, 0.0)})
		{
			JumpCurve->FloatCurve.SetKeyInterpMode(JumpCurve->FloatCurve.AddKey(float(Key.X), float(Key.Y)), RCIM_Linear);
		}
		Movement->SlideJumpDecayCurve = JumpCurve;
		GroundFriction = Movement->GroundFriction;
		Braking = Movement->BrakingDecelerationWalking;
		MaxAcceleration = Movement->MaxAcceleration;
		Movement->Velocity = FVector(800.0, 0.0, 0.0);
		Input->SetSprintIntent(true);
		Input->HandleCrouchPressed();
		// 요청 bool만 확인하지 않고 실제 작은 캡슐로 Slide를 한 이동 진행한다.
		Movement->PerformMovement(1.0f / 60.0f);
		return Test.TestTrue(TEXT("Slide jump fixture is actually sliding and crouched"),
			Movement->IsSlideSimulating() && Shooter->bIsCrouched);
	}

	~FFixture()
	{
		if (!World) return;
		if (Input && Input->IsSliding()) Input->StopSlide(ESlideEndReason::ForcedCancel);
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterSlideJumpTest,
	"Outlier.Shooter.Slide.Jump",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterSlideJumpTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace ShooterSlideJumpTests;
	TestEqual(TEXT("Native Slide jump multiplier defaults to ten percent extra speed"),
		GetDefault<UShooterCharacterMovementComponent>()->SlideJumpSpeedMultiplier, 1.10f);
	const auto PrepareAirSimulation = [](FFixture& Fixture, float HorizontalSpeed)
	{
		auto* Movement = Fixture.Movement;
		if (Fixture.Input->IsSliding()) Fixture.Input->StopSlide(ESlideEndReason::ForcedCancel);
		Movement->bWantsToCrouch = false;
		Movement->UnCrouch(false);
		Fixture.Shooter->StopJumping();
		Fixture.Shooter->JumpForceTimeRemaining = 0.0f;
		Fixture.Shooter->SetActorLocation(FVector(0.0, 0.0, 3000.0));
		Movement->SetMovementMode(MOVE_Falling);
		Movement->GravityScale = 0.0f;
		Movement->AirControl = 1.0f;
		Movement->FallingLateralFriction = 0.0f;
		Movement->BrakingDecelerationFalling = 0.0f;
		Movement->Acceleration = FVector::ZeroVector;
		Movement->Velocity = FVector(HorizontalSpeed, 0.0, 0.0);
		Movement->SlideJumpDecayDuration = 0.5f;
		Movement->StartSlideJumpMomentum(Movement->Velocity);
	};
	for (bool bAcquiredSuit : {false, true})
	for (float SpeedMultiplier : {1.0f, 1.10f})
	{
		FFixture Fixture;
		if (!Fixture.Initialize(*this, bAcquiredSuit)) return false;
		auto* Shooter = Fixture.Shooter;
		auto* Movement = Fixture.Movement;
		Movement->SlideJumpSpeedMultiplier = SpeedMultiplier;
		Shooter->JumpMaxHoldTime = 0.0f;
		Shooter->JumpCurrentCount = 0;
		Shooter->bWasJumping = false;
		// Slide 방향/시작 속도와 다른 실제 속도로 충돌 후 계승 경계를 검증한다.
		const FVector Before(710.0, -125.0, 0.0);
		Movement->Velocity = Before;
		Shooter->SetActorRotation(FRotator(0.0, 90.0, 0.0));
		Fixture.Input->DoJumpStart();
		Shooter->CheckJumpInput(0.0f);
		TestTrue(TEXT("One Slide jump Press enters Falling"), Movement->IsFalling());
		TestEqual(TEXT("Slide jump consumes exactly the first jump"), Shooter->JumpCurrentCount, 1);
		const FVector ExpectedHorizontal = Before * SpeedMultiplier;
		TestTrue(TEXT("Slide jump applies the configured multiplier once to actual XY without view rotation"),
			FVector(Movement->Velocity.X, Movement->Velocity.Y, 0.0f).Equals(ExpectedHorizontal, 0.01f));
		TestEqual(TEXT("Slide jump uses normal vertical jump velocity"), Movement->Velocity.Z, double(Movement->JumpZVelocity));
		TestFalse(TEXT("Slide jump ends slide simulation"), Movement->IsSlideSimulating());
		TestFalse(TEXT("Slide jump releases the Slide lock"), Shooter->IsActionLocked());
		TestFalse(TEXT("Slide jump clears crouch intent"), Fixture.Input->WantsToCrouch());
		TestFalse(TEXT("Slide jump expands the real capsule"), Shooter->bIsCrouched != 0);
		TestTrue(TEXT("Slide jump preserves Sprint Toggle"), Fixture.Input->WantsToSprint());
		TestFalse(TEXT("Airborne Sprint remains inactive"), Fixture.Input->IsSprinting());
		TestEqual(TEXT("Slide jump restores ground friction"), Movement->GroundFriction, Fixture.GroundFriction);
		TestEqual(TEXT("Slide jump restores walking braking"), Movement->BrakingDecelerationWalking, Fixture.Braking);
		TestEqual(TEXT("Slide jump restores acceleration"), Movement->MaxAcceleration, Fixture.MaxAcceleration);
		TestTrue(TEXT("Excess actual speed starts inertia"), Movement->SlideJumpMomentum.bActive);
		const FShooterSlideJumpMomentum TakeoffMomentum = Movement->SlideJumpMomentum;
		const uint32 Epoch = Movement->GetMovementInputEpoch();
		// 공중 Toggle은 착지 의도만 바꾼다. 이륙 때 확정한 속도/감속 기준은 다시 계산하지 않는다.
		for (const bool bSprint : {false, true})
		{
			TestTrue(TEXT("Airborne Sprint change is accepted"),
				Movement->ApplyActionInput(bSprint ? 2 : 1, Epoch, bSprint, false));
			TestEqual(TEXT("Airborne Sprint change updates only the intent"), Fixture.Input->WantsToSprint(), bSprint);
			TestFalse(TEXT("Slide jump momentum is not actual airborne Sprint"), Fixture.Input->IsSprinting());
			TestEqual(TEXT("Airborne Toggle preserves the takeoff Sprint target"), Movement->SlideJumpMomentum.TargetSpeed, TakeoffMomentum.TargetSpeed);
			TestEqual(TEXT("Airborne Toggle preserves the inherited excess"), Movement->SlideJumpMomentum.InitialExcessSpeed, TakeoffMomentum.InitialExcessSpeed);
			TestEqual(TEXT("Airborne Toggle cannot restart curve progress"), Movement->SlideJumpMomentum.Elapsed, TakeoffMomentum.Elapsed);
			TestTrue(TEXT("Airborne Toggle cannot multiply or replace velocity"),
				FVector(Movement->Velocity.X, Movement->Velocity.Y, 0.0f).Equals(ExpectedHorizontal, 0.01f));
		}
		Shooter->CheckJumpInput(0.0f);
		TestEqual(TEXT("Holding the same Press does not consume a second jump"), Shooter->JumpCurrentCount, 1);
		TestTrue(TEXT("Holding the same Press cannot apply the multiplier again"),
			FVector(Movement->Velocity.X, Movement->Velocity.Y, 0.0f).Equals(ExpectedHorizontal, 0.01f));
		Fixture.Input->DoJumpEnd();
		Shooter->ClearJumpInput(0.0f);
		Fixture.Input->DoJumpStart();
		Shooter->CheckJumpInput(0.0f);
		TestEqual(TEXT("Only acquired Suit permits the subsequent air jump"), Shooter->JumpCurrentCount, bAcquiredSuit ? 2 : 1);
		TestTrue(TEXT("The later air jump cannot apply the Slide jump multiplier again"),
			FVector(Movement->Velocity.X, Movement->Velocity.Y, 0.0f).Equals(ExpectedHorizontal, 0.01f));
		Movement->SetMovementMode(MOVE_Walking);
		TestFalse(TEXT("Landing clears inertia"), Movement->SlideJumpMomentum.bActive);
	}
	{
		FFixture Fixture;
		if (!Fixture.Initialize(*this)) return false;
		auto* Movement = Fixture.Movement;
		Movement->bUsingMovementInput = true;
		Movement->MoveInput.Epoch = Movement->GetMovementInputEpoch();
		Movement->MoveInput.JumpCancelRequest = 1;
		// Press/Release가 한 이동에 묶이면 compressed Jump bit는 false지만 요청 순번은 남는다.
		Fixture.Shooter->StopJumping();
		Fixture.Shooter->CheckJumpInput(0.0f);
		TestEqual(TEXT("Quick Slide jump tap still consumes one first jump"), Fixture.Shooter->JumpCurrentCount, 1);
		TestFalse(TEXT("Quick Slide jump tap does not become a held jump"), Fixture.Shooter->bPressedJump != 0);
		Fixture.Shooter->CheckJumpInput(0.0f);
		TestEqual(TEXT("Repeated packed request does not consume another jump"), Fixture.Shooter->JumpCurrentCount, 1);
	}
	{
		FFixture Fixture;
		if (!Fixture.Initialize(*this)) return false;
		auto* Shooter = Fixture.Shooter;
		auto* Movement = Fixture.Movement;
		Shooter->JumpMaxHoldTime = 0.0f;
		// 작은 캡슐은 들어가지만 서 있는 캡슐은 닿는 실제 천장을 만든다.
		const float CrouchedTop = Shooter->GetActorLocation().Z + Shooter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		UBoxComponent* Ceiling = Fixture.AddBox(FVector(0.0, 0.0, CrouchedTop + 15.0f), FVector(1000.0, 1000.0, 10.0));
		if (!TestNotNull(TEXT("Low ceiling fixture"), Ceiling)) return false;
		const FVector BeforeRejectedJump = Movement->Velocity;
		Fixture.Input->DoJumpStart();
		Shooter->CheckJumpInput(0.0f);
		TestFalse(TEXT("Ceiling rejection still cancels Slide"), Movement->IsSlideSimulating());
		TestTrue(TEXT("Ceiling rejection retains the actual crouched capsule"), Shooter->bIsCrouched != 0);
		TestEqual(TEXT("Ceiling rejection consumes no jump"), Shooter->JumpCurrentCount, 0);
		TestFalse(TEXT("Ceiling rejection starts no inertia"), Movement->SlideJumpMomentum.bActive);
		TestTrue(TEXT("Ceiling rejection applies no horizontal multiplier"), Movement->Velocity.Equals(BeforeRejectedJump, 0.01f));
		TestFalse(TEXT("Ceiling rejection consumes the failed Press"), Shooter->bPressedJump != 0);
		Ceiling->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Movement->UnCrouch(false);
		Shooter->CheckJumpInput(0.0f);
		TestEqual(TEXT("Removing the ceiling cannot execute a delayed rejected jump"), Shooter->JumpCurrentCount, 0);
		TestFalse(TEXT("Removing the ceiling does not cause takeoff"), Movement->IsFalling());
	}
	{
		FFixture Fixture;
		if (!Fixture.Initialize(*this)) return false;
		auto* Shooter = Fixture.Shooter;
		auto* Movement = Fixture.Movement;
		const float CrouchedTop = Shooter->GetActorLocation().Z + Shooter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		UBoxComponent* Ceiling = Fixture.AddBox(FVector(0.0, 0.0, CrouchedTop + 15.0f), FVector(1000.0, 1000.0, 10.0));
		if (!TestNotNull(TEXT("Repeated held request ceiling"), Ceiling)) return false;
		Movement->bUsingMovementInput = true;
		Movement->MoveInput.Epoch = Movement->GetMovementInputEpoch();
		Movement->MoveInput.JumpCancelRequest = 1;
		Shooter->Jump();
		Shooter->CheckJumpInput(0.0f);
		TestTrue(TEXT("Rejected Slide jump remembers rejection until release or new request"), Movement->bSlideJumpRejected);
		Ceiling->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Movement->UnCrouch(false);
		// 다음 패킷에 같은 홀드 bit가 남아 있어도 실패한 Press를 새 시도로 해석하지 않는다.
		Shooter->bPressedJump = true;
		Shooter->CheckJumpInput(0.0f);
		TestEqual(TEXT("Repeated rejected held input cannot take off after ceiling removal"), Shooter->JumpCurrentCount, 0);
		TestFalse(TEXT("Repeated rejected held input creates no inertia"), Movement->SlideJumpMomentum.bActive);
		Movement->MoveInput.JumpCancelRequest = 2;
		Shooter->Jump();
		Shooter->CheckJumpInput(0.0f);
		TestEqual(TEXT("A genuinely new jump request can succeed after rejection"), Shooter->JumpCurrentCount, 1);
	}
	{
		FFixture Fixture;
		if (!Fixture.Initialize(*this)) return false;
		auto* Movement = Fixture.Movement;
		Fixture.Input->DoJumpStart();
		Fixture.Shooter->CheckJumpInput(0.0f);
		PrepareAirSimulation(Fixture, 1000.0f);
		Movement->PhysFalling(0.1f, 0);
		const FVector BeforeCorrection = Movement->Velocity;
		FShooterSlideSnapshot Sent = Movement->CaptureSlideSnapshot();
		TestTrue(TEXT("Correction fixture captures progressed curve state"), Sent.JumpMomentum.Elapsed > 0.09f);
		FNetBitWriter Writer(nullptr, 4096);
		Sent.Serialize(Writer);
		TestFalse(TEXT("Inertia correction serializes"), Writer.IsError());
		FNetBitReader Reader(nullptr, Writer.GetData(), Writer.GetNumBits());
		FShooterSlideSnapshot Received;
		Received.Serialize(Reader);
		TestFalse(TEXT("Inertia correction deserializes"), Reader.IsError());
		TestEqual(TEXT("Correction preserves inertia acceptance"), Received.JumpMomentum.bActive, Sent.JumpMomentum.bActive);
		TestEqual(TEXT("Correction preserves takeoff target"), Received.JumpMomentum.TargetSpeed, Sent.JumpMomentum.TargetSpeed);
		TestEqual(TEXT("Correction preserves initial excess speed"), Received.JumpMomentum.InitialExcessSpeed, Sent.JumpMomentum.InitialExcessSpeed);
		TestEqual(TEXT("Correction preserves curve elapsed time"), Received.JumpMomentum.Elapsed, Sent.JumpMomentum.Elapsed);
		TestEqual(TEXT("Correction preserves curve duration"), Received.JumpMomentum.Duration, Sent.JumpMomentum.Duration);
		TestEqual(TEXT("Correction preserves monotone remaining curve ratio"), Received.JumpMomentum.RemainingRatio, Sent.JumpMomentum.RemainingRatio);
		Movement->ResetSlideJumpMomentum();
		Movement->RestoreSlideSnapshot(Received);
		TestTrue(TEXT("Correction restores approved inertia without reentering Slide"),
			Movement->SlideJumpMomentum.bActive && !Movement->IsSlideSimulating());
		TestTrue(TEXT("Inertia restore never adds inherited velocity again"), Movement->Velocity.Equals(BeforeCorrection));
		Movement->PhysFalling(0.15f, 0);
		TestTrue(TEXT("Restored curve progress continues at the quarter-second checkpoint"),
			FMath::IsNearlyEqual(Movement->Velocity.Size2D(),
				Sent.JumpMomentum.TargetSpeed + Sent.JumpMomentum.InitialExcessSpeed * 0.65f, 0.1f));

		auto* ClientData = Movement->GetPredictionData_Client_Character();
		FSavedMovePtr SavedMove = ClientData->AllocateNewMove();
		SavedMove->Clear();
		SavedMove->SetMoveFor(Fixture.Shooter, 1.0f / 60.0f, FVector::ZeroVector, *ClientData);
		SavedMove->PostUpdate(Fixture.Shooter, FSavedMove_Character::PostUpdate_Record);
		Movement->ResetSlideJumpMomentum();
		SavedMove->PrepMoveFor(Fixture.Shooter);
		TestFalse(TEXT("SavedMove cannot revive inertia rejected by correction"), Movement->SlideJumpMomentum.bActive);
		Movement->StartSlideJumpMomentum(FVector(1000.0, 0.0, 0.0));
		Movement->ResetSlideForLifecycle();
		TestFalse(TEXT("Lifecycle cleanup clears inertia"), Movement->SlideJumpMomentum.bActive);
		// 순간이동은 Falling 상태를 유지할 수도 있다. 모드 변경 없이도 이전 관성과 입력 이력을 끊어야 한다.
		PrepareAirSimulation(Fixture, 1000.0f);
		Movement->PhysFalling(0.1f, 0);
		const uint32 BeforeTeleportEpoch = Movement->GetMovementInputEpoch();
		const FVector BeforeTeleportVelocity = Movement->Velocity;
		Movement->OnTeleported();
		TestFalse(TEXT("Teleport clears active inertia even while still airborne"), Movement->SlideJumpMomentum.bActive);
		TestFalse(TEXT("Teleport clears the previous Sprint toggle"), Fixture.Input->WantsToSprint());
		TestTrue(TEXT("Teleport advances the movement input lifecycle"), Movement->GetMovementInputEpoch() != BeforeTeleportEpoch);
		TestTrue(TEXT("Teleport cleanup cannot reapply the inherited velocity"), Movement->Velocity.Equals(BeforeTeleportVelocity));
		TestFalse(TEXT("Pre-teleport action packets cannot restore Sprint"),
			Movement->ApplyActionInput(1, BeforeTeleportEpoch, true, false));
		SavedMove->PrepMoveFor(Fixture.Shooter);
		TestFalse(TEXT("Pre-teleport SavedMove cannot revive old inertia"), Movement->SlideJumpMomentum.bActive);
	}
	{
		FFixture Fixture;
		if (!Fixture.Initialize(*this)) return false;
		auto* Movement = Fixture.Movement;
		PrepareAirSimulation(Fixture, 1000.0f);
		Movement->PhysFalling(0.0f, 0);
		TestEqual(TEXT("Zero simulation time does not advance the curve"), Movement->SlideJumpMomentum.Elapsed, 0.0f);
		Movement->PhysFalling(0.1f, Movement->MaxSimulationIterations);
		TestEqual(TEXT("An exhausted simulation budget does not advance the curve"), Movement->SlideJumpMomentum.Elapsed, 0.0f);
		TestEqual(TEXT("An exhausted simulation budget preserves actual speed"), Movement->Velocity.Size2D(), 1000.0);
		const FShooterSlideJumpMomentum BeforeScratch = Movement->SlideJumpMomentum;
		{
			// 엔진의 충돌 판정용 가상 적분처럼 속도만 임시 변경하고 영속 시계는 소비하지 않는다.
			TGuardValue<FVector> VelocityGuard(Movement->Velocity, Movement->Velocity);
			TGuardValue<float> DecayGuard(Movement->SlideJumpIntervalDecayRate, 100.0f);
			Movement->CalcVelocity(0.05f, 0.0f, false, 0.0f);
		}
		TestEqual(TEXT("Scratch velocity integration preserves elapsed curve time"), Movement->SlideJumpMomentum.Elapsed, BeforeScratch.Elapsed);
		TestEqual(TEXT("Scratch velocity integration preserves remaining curve ratio"), Movement->SlideJumpMomentum.RemainingRatio, BeforeScratch.RemainingRatio);
		TestEqual(TEXT("Scratch velocity integration preserves initial excess speed"), Movement->SlideJumpMomentum.InitialExcessSpeed, BeforeScratch.InitialExcessSpeed);
	}
	for (int32 FramesPerSecond : {30, 60, 120})
	{
		FFixture Fixture;
		if (!Fixture.Initialize(*this)) return false;
		auto* Movement = Fixture.Movement;
		PrepareAirSimulation(Fixture, 1000.0f);
		const float Target = Movement->SlideJumpMomentum.TargetSpeed;
		const float OriginalWalkLimit = Movement->MaxWalkSpeed;
		const float Delta = 1.0f / FramesPerSecond;
		float Elapsed = 0.0f;
		const float CheckpointTimes[] = {0.1f, 0.25f, 0.4f, 0.5f};
		const float ExpectedRatios[] = {0.95f, 0.65f, 0.2f, 0.0f};
		// 중력/착지 변수를 제거하되 실제 Falling 적분과 시계를 사용해 프레임별 곡선 진행을 비교한다.
		for (int32 Checkpoint = 0; Checkpoint < UE_ARRAY_COUNT(CheckpointTimes); ++Checkpoint)
		{
			while (Elapsed + KINDA_SMALL_NUMBER < CheckpointTimes[Checkpoint])
			{
				const float Step = FMath::Min(Delta, CheckpointTimes[Checkpoint] - Elapsed);
				const float Before = Movement->Velocity.Size2D();
				Movement->PhysFalling(Step, 0);
				TestTrue(TEXT("Unforced curve decay never accelerates"), Movement->Velocity.Size2D() <= Before + 0.01f);
				if (Elapsed == 0.0f) TestTrue(TEXT("Takeoff speed is not immediately cut to Sprint target"), Movement->Velocity.Size2D() > Target);
				Elapsed += Step;
			}
			TestTrue(FString::Printf(TEXT("%d FPS follows the assigned curve at %.2f seconds"), FramesPerSecond, CheckpointTimes[Checkpoint]),
				FMath::IsNearlyEqual(Movement->Velocity.Size2D(), Target + 400.0f * ExpectedRatios[Checkpoint], 0.1f));
		}
		TestFalse(TEXT("Curve completion clears inertia"), Movement->SlideJumpMomentum.bActive);
		TestEqual(TEXT("Inertia restores the ordinary air speed setting after each calculation"), Movement->MaxWalkSpeed, OriginalWalkLimit);
		PrepareAirSimulation(Fixture, 1000.0f);
		Movement->Velocity = FVector(Target + 20.0f, 0.0, 0.0);
		Movement->PhysFalling(Delta, 0);
		TestTrue(TEXT("Collision-reduced speed is never restored to the old curve"), Movement->Velocity.Size2D() <= Target + 20.01f);
		PrepareAirSimulation(Fixture, 1000.0f);
		Movement->PhysFalling(Delta, 0);
		const float UnforcedSpeed = Movement->Velocity.Size2D();
		PrepareAirSimulation(Fixture, 1000.0f);
		Movement->Acceleration = FVector(-2000.0, 0.0, 0.0);
		Movement->PhysFalling(Delta, 0);
		const float AfterReverseInput = Movement->Velocity.Size2D();
		TestTrue(TEXT("Reverse input can slow inertia below its unforced decay result"),
			AfterReverseInput < UnforcedSpeed);
		Movement->Acceleration = FVector::ZeroVector;
		Movement->PhysFalling(Delta, 0);
		TestTrue(TEXT("Releasing reverse input does not recover the lost speed"), Movement->Velocity.Size2D() <= AfterReverseInput + 0.01f);
		Movement->Velocity = FVector(150.0, 0.0, 0.0);
		Movement->PhysFalling(Delta, 0);
		TestTrue(TEXT("Below-target speed is never raised to a minimum"), Movement->Velocity.Size2D() <= 150.01f);
		AddInfo(FString::Printf(TEXT("[SlideJumpTest] FPS=%d Target=%.3f FinalReducedSpeed=%.3f"),
			FramesPerSecond, Target, Movement->Velocity.Size2D()));
	}
	{
		FFixture Fixture;
		if (!Fixture.Initialize(*this)) return false;
		auto* Movement = Fixture.Movement;
		UCurveFloat* ExternalCurve = NewObject<UCurveFloat>(Movement);
		ExternalCurve->FloatCurve.AddKey(0.0f, 1.0f);
		ExternalCurve->FloatCurve.AddKey(1.0f, 0.0f);
		Movement->SlideJumpDecayCurve = ExternalCurve;
		PrepareAirSimulation(Fixture, 1000.0f);
		Movement->PhysFalling(0.1f, 0);
		TestTrue(TEXT("The assigned linear asset determines the decay"),
			FMath::IsNearlyEqual(Movement->Velocity.Size2D(), 920.0f, 0.1f));

		// 오버슈트/상승 구간은 튜닝 에셋에 있어도 감속을 되돌리거나 새 속도를 만들지 못한다.
		ExternalCurve->FloatCurve.Reset();
		ExternalCurve->FloatCurve.AddKey(0.0f, 1.0f);
		ExternalCurve->FloatCurve.AddKey(0.3f, 1.4f);
		ExternalCurve->FloatCurve.AddKey(0.6f, 0.5f);
		ExternalCurve->FloatCurve.AddKey(0.8f, 0.9f);
		ExternalCurve->FloatCurve.AddKey(1.0f, -0.2f);
		PrepareAirSimulation(Fixture, 1000.0f);
		for (int32 Frame = 0; Frame < 30; ++Frame)
		{
			const float Before = Movement->Velocity.Size2D();
			Movement->PhysFalling(1.0f / 60.0f, 0);
			TestTrue(TEXT("Overshoot and upward curve segments cannot inject speed"), Movement->Velocity.Size2D() <= Before + 0.01f);
		}
		TestTrue(TEXT("An out-of-range curve still finishes at the Sprint target"),
			FMath::IsNearlyEqual(Movement->Velocity.Size2D(), 600.0f, 0.1f));
	}
	{
		FFixture Fixture;
		if (!Fixture.Initialize(*this)) return false;
		Fixture.Movement->SlideJumpDecayCurve = nullptr;
		const FVector Before = Fixture.Movement->Velocity;
		AddExpectedError(TEXT("SlideJumpDecayCurve requires a Curve Float asset with keys"),
			EAutomationExpectedErrorFlags::Contains, 1);
		Fixture.Input->DoJumpStart();
		Fixture.Shooter->CheckJumpInput(0.0f);
		TestEqual(TEXT("A missing required curve rejects Slide jump without consuming a jump"), Fixture.Shooter->JumpCurrentCount, 0);
		TestFalse(TEXT("A missing required curve cannot activate substitute inertia"), Fixture.Movement->SlideJumpMomentum.bActive);
		TestTrue(TEXT("A missing required curve cannot apply the multiplier"), Fixture.Movement->Velocity.Equals(Before));
	}
	return true;
}

#endif
