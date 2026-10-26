#if WITH_DEV_AUTOMATION_TESTS

#include "Level/Elevator/ElevatorShaftController.h"
#include "Misc/AutomationTest.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElevatorShaftInitialSlotsTest,
	"Outlier.Elevator.Motion.InitialSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElevatorShaftInitialSlotsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	constexpr double Height = 1000.0;
	for (int32 Slot = 0; Slot < 3; ++Slot)
	{
		TestEqual(FString::Printf(TEXT("Slot %d begins below the entrance"), Slot),
			AElevatorShaftController::CalculateLoopOffset(0.0, Slot, 3, Height), -(Slot + 1) * Height);
		TestEqual(TEXT("A negative elapsed distance preserves the initial placement"),
			AElevatorShaftController::CalculateLoopOffset(-250.0, Slot, 3, Height), -(Slot + 1) * Height);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElevatorShaftWrapBoundaryTest,
	"Outlier.Elevator.Motion.WrapBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElevatorShaftWrapBoundaryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	constexpr double Height = 1000.0;
	constexpr double Epsilon = 0.01;
	// 첫 입구 층은 재사용하지 않는다. 반복 층 0이 상단을 벗어날 때에만 풀 하단으로 보낸다.
	TestEqual(TEXT("After one section the first loop occupies the former entrance slot"),
		AElevatorShaftController::CalculateLoopOffset(Height, 0, 3, Height), 0.0);
	TestTrue(TEXT("Immediately before wrapping the first loop reaches the upper boundary"),
		FMath::IsNearlyEqual(AElevatorShaftController::CalculateLoopOffset(2 * Height - Epsilon, 0, 3, Height),
			Height - Epsilon, 1.e-6));
	TestEqual(TEXT("The exact upper boundary recycles to the lower slot"),
		AElevatorShaftController::CalculateLoopOffset(2 * Height, 0, 3, Height), -2 * Height);
	TestTrue(TEXT("Immediately after wrapping motion continues upward from the lower slot"),
		FMath::IsNearlyEqual(AElevatorShaftController::CalculateLoopOffset(2 * Height + Epsilon, 0, 3, Height),
			-2 * Height + Epsilon, 1.e-6));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElevatorShaftMultiWrapSpacingTest,
	"Outlier.Elevator.Motion.MultiWrapSpacing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElevatorShaftMultiWrapSpacingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	constexpr double Height = 1000.0;
	constexpr int32 Count = 5;
	constexpr double Period = Count * Height;
	for (double Distance : {0.0, Height, 2 * Height, 2 * Height + 0.25, Period * 1.e9 + 2250.0})
	{
		TArray<double> WrappedOffsets;
		for (int32 Slot = 0; Slot < Count; ++Slot)
		{
			const double Offset = AElevatorShaftController::CalculateLoopOffset(Distance, Slot, Count, Height);
			TestTrue(TEXT("Large jumps always produce finite offsets"), FMath::IsFinite(Offset));
			WrappedOffsets.Add(FMath::Fmod(FMath::Fmod(Offset, Period) + Period, Period));
		}
		WrappedOffsets.Sort();
		for (int32 Slot = 0; Slot < Count; ++Slot)
		{
			const double Next = Slot + 1 < Count ? WrappedOffsets[Slot + 1] : WrappedOffsets[0] + Period;
			TestTrue(FString::Printf(TEXT("Distance %.2f keeps slot %d distinct and exactly one height apart"), Distance, Slot),
				FMath::IsNearlyEqual(Next - WrappedOffsets[Slot], Height, 1.e-6));
		}
	}

	// 틱 누적 없이 서버 시각에서 바로 계산하므로 여러 번 순환한 뒤에도 같은 배치로 수렴해야 한다.
	for (int32 Slot = 0; Slot < Count; ++Slot)
	{
		TestEqual(TEXT("A billion complete periods preserve the same visible position"),
			AElevatorShaftController::CalculateLoopOffset(Period * 1.e9 + 2250.0, Slot, Count, Height),
			AElevatorShaftController::CalculateLoopOffset(Period * 2 + 2250.0, Slot, Count, Height));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElevatorShaftInvalidMotionInputsTest,
	"Outlier.Elevator.Motion.InvalidInputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElevatorShaftInvalidMotionInputsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const double Infinity = std::numeric_limits<double>::infinity();
	const double NaN = std::numeric_limits<double>::quiet_NaN();
	for (int32 Count : {0, -1})
	{
		TestEqual(TEXT("An invalid pool count returns a safe offset"),
			AElevatorShaftController::CalculateLoopOffset(100.0, 0, Count, 1000.0), 0.0);
	}
	for (int32 Slot : {-1, 3})
	{
		TestEqual(TEXT("An out-of-pool slot returns a safe offset"),
			AElevatorShaftController::CalculateLoopOffset(100.0, Slot, 3, 1000.0), 0.0);
	}
	for (double Height : {0.0, -1.0, Infinity, NaN, std::numeric_limits<double>::max()})
	{
		TestEqual(TEXT("Invalid height or overflowed period returns a safe offset"),
			AElevatorShaftController::CalculateLoopOffset(100.0, 0, 3, Height), 0.0);
	}
	for (double Distance : {Infinity, -Infinity, NaN})
	{
		TestEqual(TEXT("Nonfinite travel cannot propagate into actor transforms"),
			AElevatorShaftController::CalculateLoopOffset(Distance, 0, 3, 1000.0), 0.0);
	}
	return true;
}

#endif
