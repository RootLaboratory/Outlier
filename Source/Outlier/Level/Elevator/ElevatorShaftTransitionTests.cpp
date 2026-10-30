#if WITH_DEV_AUTOMATION_TESTS

#include "Level/Elevator/ElevatorShaftController.h"
#include "Curves/CurveFloat.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

namespace ElevatorTransitionTests
{
	struct FWorldFixture
	{
		UWorld* World = nullptr;

		FWorldFixture()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false,
				MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("ElevatorTransitionTest")));
			if (!World) return;
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}

		~FWorldFixture()
		{
			if (!World) return;
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
			World->RemoveFromRoot();
		}
	};

	UCurveFloat* MakeCurve(AActor* Owner, bool bAcceleration)
	{
		UCurveFloat* Curve = NewObject<UCurveFloat>(Owner);
		for (int32 Index = 0; Index <= 1; ++Index)
		{
			const FKeyHandle Handle = Curve->FloatCurve.AddKey(Index, bAcceleration ? Index : 1 - Index);
			Curve->FloatCurve.SetKeyInterpMode(Handle, RCIM_Cubic);
			FRichCurveKey& Key = Curve->FloatCurve.GetKey(Handle);
			Key.TangentMode = RCTM_User;
			Key.ArriveTangent = 0.f;
			Key.LeaveTangent = 0.f;
		}
		return Curve;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElevatorArrivalMotionTest,
	"Outlier.Elevator.Transition.ArrivalMotion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElevatorArrivalMotionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	ElevatorTransitionTests::FWorldFixture Fixture;
	if (!TestNotNull(TEXT("Transition world exists"), Fixture.World)) return false;
	AElevatorShaftController* Controller = Fixture.World->SpawnActor<AElevatorShaftController>();
	if (!TestNotNull(TEXT("Controller exists"), Controller)) return false;
	Controller->AccelerationCurve = ElevatorTransitionTests::MakeCurve(Controller, true);
	Controller->DecelerationCurve = ElevatorTransitionTests::MakeCurve(Controller, false);
	FString Error;
	if (!TestTrue(TEXT("Authored motion profiles are valid"), Controller->BuildMotionProfiles(Error))) return false;

	constexpr double Epsilon = 1.e-4;
	for (float Speed : {250.f, 900.f})
	{
		Controller->Motion.Speed = Speed;
		Controller->Motion.AccelerationSeconds = 3.5;
		Controller->Motion.DecelerationSeconds = 3.5;
		const double AccelerationEnd = Controller->DepartureDistance(3.5);
		TestTrue(TEXT("Departure reaches the integrated half-speed distance"),
			FMath::IsNearlyEqual(AccelerationEnd, Speed * 3.5 * 0.5, 1.e-3));
		TestTrue(TEXT("Acceleration joins cruise with continuous distance"),
			FMath::Abs(Controller->DepartureDistance(3.5 + Epsilon) - Controller->DepartureDistance(3.5 - Epsilon)) < Speed * Epsilon * 2.01);
		TestTrue(TEXT("Departure joins cruise at target speed"),
			FMath::IsNearlyEqual((Controller->DepartureDistance(3.5 + Epsilon) - Controller->DepartureDistance(3.5 - Epsilon)) / (2 * Epsilon), Speed, 0.01));

		for (double Height : {450.0, 1450.0})
		{
			for (double DepartureElapsed : {0.4, 10.0})
			{
				Controller->Motion.DepartureElapsed = DepartureElapsed;
				const double RemainingAcceleration = FMath::Max(0.0, 3.5 - DepartureElapsed);
				const double AccelerationDistance = Controller->DepartureDistance(DepartureElapsed + RemainingAcceleration)
					- Controller->DepartureDistance(DepartureElapsed);
				const double BrakeDistance = Speed * 3.5 * 0.5;
				// 짧은 층/고속 설정도 감속도를 바꾸지 않고 정수 층 거리로 확장한다.
				Controller->Motion.ArrivalDistance = FMath::CeilToDouble((AccelerationDistance + BrakeDistance + Speed) / Height) * Height;
				Controller->Motion.BrakeStartSeconds = RemainingAcceleration
					+ (Controller->Motion.ArrivalDistance - AccelerationDistance - BrakeDistance) / Speed;
				const double BrakeStart = Controller->Motion.BrakeStartSeconds;
				const double End = BrakeStart + 3.5;

				TestEqual(TEXT("Arrival starts at its prepared coordinate origin"), Controller->ArrivalTravel(0), 0.0);
				const double SourceSpeed = (Controller->DepartureDistance(DepartureElapsed + Epsilon)
					- Controller->DepartureDistance(DepartureElapsed)) / Epsilon;
				TestTrue(TEXT("Arrival inherits current speed including unfinished acceleration"),
					FMath::IsNearlyEqual(Controller->ArrivalTravel(Epsilon) / Epsilon, SourceSpeed, 1.e-5));
				TestTrue(TEXT("Distance remains continuous when braking starts"),
					FMath::Abs(Controller->ArrivalTravel(BrakeStart + Epsilon) - Controller->ArrivalTravel(BrakeStart - Epsilon)) < Speed * Epsilon * 2.01);
				TestTrue(TEXT("Braking begins at the incoming cruise speed"),
					FMath::IsNearlyEqual((Controller->ArrivalTravel(BrakeStart + Epsilon) - Controller->ArrivalTravel(BrakeStart - Epsilon)) / (2 * Epsilon), Speed, 0.01));
				TestTrue(TEXT("Integral lands exactly at the authored final section position"),
					FMath::IsNearlyEqual(Controller->ArrivalTravel(End), Controller->Motion.ArrivalDistance, 0.001));
				TestTrue(TEXT("Landing ends at zero speed"),
					FMath::Abs((Controller->ArrivalTravel(End) - Controller->ArrivalTravel(End - Epsilon)) / Epsilon) < 0.01);
				TestTrue(TEXT("Large late time jumps remain on the landing point"),
					FMath::IsNearlyEqual(Controller->ArrivalTravel(End + 100000.0), Controller->Motion.ArrivalDistance, 0.001));
				for (int32 Frames : {30, 60, 120})
				{
					double Travel = 0;
					for (int32 Frame = 1; Frame <= Frames; ++Frame)
					{
						Travel += Controller->ArrivalTravel(End * Frame / Frames) - Controller->ArrivalTravel(End * (Frame - 1) / Frames);
					}
					TestTrue(TEXT("Frame subdivisions match one server-time evaluation"),
						FMath::IsNearlyEqual(Travel, Controller->Motion.ArrivalDistance, 0.001));
				}
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElevatorCompletionBoundaryTest,
	"Outlier.Elevator.Transition.CompletionBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElevatorCompletionBoundaryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	ElevatorTransitionTests::FWorldFixture Fixture;
	if (!TestNotNull(TEXT("Completion world exists"), Fixture.World)) return false;
	AElevatorShaftController* Controller = Fixture.World->SpawnActor<AElevatorShaftController>();
	if (!TestNotNull(TEXT("Controller exists"), Controller)) return false;
	Controller->AccelerationCurve = ElevatorTransitionTests::MakeCurve(Controller, true);
	Controller->DecelerationCurve = ElevatorTransitionTests::MakeCurve(Controller, false);
	FString Error;
	if (!TestTrue(TEXT("Profiles are ready"), Controller->BuildMotionProfiles(Error))) return false;
	Controller->Motion.bStarted = true;
	Controller->Motion.StartServerTime = Controller->ServerTime() - 10.0;
	const double ExpectedDistance = 250.0 * (3.5 * 0.5 + 6.5);
	for (EElevatorShaftState State : {EElevatorShaftState::RisingCombat, EElevatorShaftState::FinishingCurrentSection, EElevatorShaftState::TransitionOut})
	{
		Controller->Motion.State = State;
		TestTrue(TEXT("Combat completion and blackout do not freeze departure motion"),
			FMath::IsNearlyEqual(Controller->CurrentDistance(), ExpectedDistance, 0.001));
	}

	Controller->Motion.State = EElevatorShaftState::FinishingCurrentSection;
	Controller->Motion.FinishDistance = FElevatorShaftSpeedProfile::FinishBoundary(ExpectedDistance, 1450.0);
	const double ReservedBoundary = Controller->Motion.FinishDistance;
	Controller->Motion.ExecutionId = FGuid::NewGuid();
	Controller->CombatContext.RoomRegistrationId = FGuid::NewGuid();
	Controller->CombatContext.GameplayGeneration = static_cast<int32>(Controller->Motion.Generation);
	for (int32 Duplicate = 0; Duplicate < 3; ++Duplicate)
	{
		FRoomCombatLifecycleEvent Context{Controller->CombatRoomTag, ERoomCombatEvent::RoomCleared,
			Controller->CombatContext.CombatPhaseIndex, static_cast<int32>(Controller->Motion.Generation),
			Controller->CombatContext.RoomRegistrationId, Controller->Motion.ExecutionId};
		Controller->OnCombatEvent(Context);
	}
	TestEqual(TEXT("Duplicate RoomCleared cannot reserve extra floors"), Controller->Motion.FinishDistance, ReservedBoundary);
	TestTrue(TEXT("Only the current section remains to travel"), ReservedBoundary - ExpectedDistance >= 0.0 && ReservedBoundary - ExpectedDistance < 1450.0);
	Controller->Motion.bStarted = false;
	Controller->Motion.StoppedDistance = 0;
	TestEqual(TEXT("No actual spawn means no fabricated departure motion"), Controller->CurrentDistance(), 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElevatorControllerTransportTest,
	"Outlier.Elevator.Transition.ControllerTransport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElevatorControllerTransportTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	ElevatorTransitionTests::FWorldFixture Fixture;
	if (!TestNotNull(TEXT("Transport world exists"), Fixture.World)) return false;
	AElevatorShaftController* Controller = Fixture.World->SpawnActor<AElevatorShaftController>();
	if (!TestNotNull(TEXT("Controller exists"), Controller)) return false;
	const FTransform Departure(FRotator(0, -90, 0), FVector(10000, -7500, 1000));
	const FTransform EntranceOffset(FRotator(0, 30, 0), FVector(150, -50, 25));
	const FTransform Entrance = EntranceOffset * Departure;
	const FTransform Arrival(FRotator(0, 90, 0), FVector(-500, 2000, 100));
	Controller->SetActorTransform(Departure);
	Controller->DepartureControllerTransform = Departure;
	Controller->ControllerSectionOffset = Departure.GetRelativeTransform(Entrance);
	Controller->Motion.ArrivalTransform = Arrival;

	// 바닥/안전 외벽은 Controller의 자식이므로 원점과 회전이 달라도 같은 배치를 유지해야 한다.
	UBoxComponent* Collision = NewObject<UBoxComponent>(Controller);
	Controller->AddInstanceComponent(Collision);
	Collision->SetupAttachment(Controller->GetRootComponent());
	Collision->SetMobility(EComponentMobility::Movable);
	Collision->SetRelativeLocation(FVector(300, -200, 70));
	Collision->SetCollisionProfileName(TEXT("BlockAll"));
	Collision->RegisterComponent();
	const FTransform OriginalCollision = Collision->GetComponentTransform();
	const FTransform CollisionOffset = Collision->GetRelativeTransform();
	Controller->SetControllerAtArrival(true);
	TestTrue(TEXT("Controller aligns its entrance reference with arrival"),
		(EntranceOffset * Controller->GetActorTransform()).Equals(Arrival, 0.001));
	TestTrue(TEXT("Collision travels with the complete controller"),
		Collision->GetComponentTransform().Equals(CollisionOffset * Controller->GetActorTransform(), 0.001));
	TestFalse(TEXT("Collision no longer stays at departure"),
		Collision->GetComponentTransform().Equals(OriginalCollision, 0.001));
	TestEqual(TEXT("Transport preserves wall collision"), Collision->GetCollisionEnabled(), ECollisionEnabled::QueryAndPhysics);
	TestEqual(TEXT("Transport preserves player blocking"), Collision->GetCollisionResponseToChannel(ECC_Pawn), ECR_Block);
	const FTransform Arrived = Controller->GetActorTransform();
	Controller->SetControllerAtArrival(true);
	TestTrue(TEXT("Repeated arrival state cannot accumulate offsets"), Controller->GetActorTransform().Equals(Arrived, 0.001));
	Controller->SetControllerAtArrival(false);
	TestTrue(TEXT("Rollback restores controller placement"), Controller->GetActorTransform().Equals(Departure, 0.001));
	TestTrue(TEXT("Rollback restores collision placement"), Collision->GetComponentTransform().Equals(OriginalCollision, 0.001));
	Controller->SetControllerAtArrival(true);
	TestTrue(TEXT("Retry uses the original coordinate frame"), Controller->GetActorTransform().Equals(Arrived, 0.001));
	Controller->bInitialized = true;
	Controller->Motion.bArrival = true;
	Controller->Motion.State = EElevatorShaftState::Completed;
	Controller->OnRep_Motion();
	TestTrue(TEXT("Completed state keeps the whole controller at arrival"), Controller->GetActorTransform().Equals(Arrived, 0.001));
	Controller->Motion.bArrival = false;
	Controller->Motion.State = EElevatorShaftState::WaitingForPair;
	Controller->OnRep_Motion();
	TestTrue(TEXT("Replicated reset restores the departure frame"), Controller->GetActorTransform().Equals(Departure, 0.001));
	Controller->Motion.bArrival = true;
	Controller->Motion.State = EElevatorShaftState::PreparingArrival;
	Controller->OnRep_Motion();
	TestTrue(TEXT("Replicated arrival also moves the entire controller"), Controller->GetActorTransform().Equals(Arrived, 0.001));
	return true;
}

#endif
