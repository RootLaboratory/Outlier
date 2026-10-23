#if WITH_DEV_AUTOMATION_TESTS

#include "Level/Elevator/ElevatorShaftMotionProfile.h"
#include "Curves/CurveFloat.h"
#include "Misc/AutomationTest.h"
#include <limits>

namespace ElevatorShaftProfileTests
{
	UCurveFloat* MakeSmoothCurve(bool bAcceleration)
	{
		UCurveFloat* Curve = NewObject<UCurveFloat>(GetTransientPackage());
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElevatorShaftProfileIntegralTest,
	"Outlier.Elevator.Motion.Profile.Integral",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElevatorShaftProfileIntegralTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FElevatorShaftSpeedProfile Acceleration;
	FElevatorShaftSpeedProfile Deceleration;
	FString Error;
	if (!TestTrue(TEXT("Smooth acceleration is valid"), Acceleration.Build(ElevatorShaftProfileTests::MakeSmoothCurve(true), true, Error))
		|| !TestTrue(TEXT("Smooth deceleration is valid"), Deceleration.Build(ElevatorShaftProfileTests::MakeSmoothCurve(false), false, Error))) return false;

	TestTrue(TEXT("Symmetric acceleration covers half the constant-speed distance"),
		FMath::IsNearlyEqual(Acceleration.TotalIntegral(), 0.5, 1.e-6));
	TestTrue(TEXT("250 cm/s over 3.5 s brakes over 437.5 cm"),
		FMath::IsNearlyEqual(250.0 * 3.5 * Deceleration.TotalIntegral(), 437.5, 1.e-3));
	for (double Alpha : {0.0, 0.0003, 0.173, 0.5, 0.9997, 1.0})
	{
		const double ExpectedSpeed = Alpha * Alpha * (3.0 - 2.0 * Alpha);
		const double ExpectedDistance = Alpha * Alpha * Alpha * (1.0 - 0.5 * Alpha);
		TestTrue(TEXT("Cached nonlinear speed agrees with authored Hermite curve"),
			FMath::IsNearlyEqual(Acceleration.Speed(Alpha), ExpectedSpeed, 2.e-6));
		TestTrue(TEXT("Partial sample integration agrees with analytic distance"),
			FMath::IsNearlyEqual(Acceleration.Integral(Alpha), ExpectedDistance, 1.e-6));
		TestTrue(TEXT("Complementary acceleration and braking cover constant-speed distance"),
			FMath::IsNearlyEqual(Acceleration.Integral(Alpha) + Deceleration.Integral(Alpha), Alpha, 1.e-6));
	}

	constexpr double Alpha = 0.371234;
	constexpr double Epsilon = 1.e-6;
	TestTrue(TEXT("Distance derivative matches speed inside a cached sample interval"),
		FMath::IsNearlyEqual((Acceleration.Integral(Alpha + Epsilon) - Acceleration.Integral(Alpha - Epsilon)) / (2.0 * Epsilon),
			Acceleration.Speed(Alpha), 1.e-5));
	for (int32 Frames : {30, 60, 120})
	{
		double Distance = 0.0;
		for (int32 Frame = 1; Frame <= Frames; ++Frame)
		{
			Distance += Acceleration.Integral(static_cast<double>(Frame) / Frames)
				- Acceleration.Integral(static_cast<double>(Frame - 1) / Frames);
		}
		TestTrue(TEXT("Frame subdivision and one large time jump produce the same travel"), FMath::IsNearlyEqual(Distance, 0.5, 1.e-6));
	}
	TestEqual(TEXT("Acceleration joins cruise at target speed"), Acceleration.Speed(1.0), 1.0);
	TestEqual(TEXT("Braking joins cruise at target speed"), Deceleration.Speed(0.0), 1.0);
	TestEqual(TEXT("Braking ends at rest"), Deceleration.Speed(1.0), 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElevatorShaftProfileValidationTest,
	"Outlier.Elevator.Motion.Profile.Validation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElevatorShaftProfileValidationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FElevatorShaftSpeedProfile Profile;
	FString Error;
	TestFalse(TEXT("Missing curve fails without a fallback"), Profile.Build(nullptr, true, Error));
	TestFalse(TEXT("Acceleration cannot use a braking curve"), Profile.Build(ElevatorShaftProfileTests::MakeSmoothCurve(false), true, Error));
	UCurveFloat* Linear = NewObject<UCurveFloat>(GetTransientPackage());
	Linear->FloatCurve.AddKey(0.f, 0.f);
	Linear->FloatCurve.AddKey(1.f, 1.f);
	TestFalse(TEXT("Linear endpoints would abruptly change acceleration"), Profile.Build(Linear, true, Error));
	UCurveFloat* Invalid = ElevatorShaftProfileTests::MakeSmoothCurve(true);
	Invalid->FloatCurve.AddKey(0.5f, 1.5f);
	TestFalse(TEXT("Overspeed curve fails"), Profile.Build(Invalid, true, Error));
	TestTrue(TEXT("Valid build clears previous diagnostic"), Profile.Build(ElevatorShaftProfileTests::MakeSmoothCurve(true), true, Error) && Error.IsEmpty());
	TestFalse(TEXT("Rebuild with invalid data discards old cache"), Profile.Build(nullptr, true, Error));
	TestEqual(TEXT("Invalid cache cannot move visuals"), Profile.TotalIntegral(), 0.0);
	TestEqual(TEXT("Invalid cache has zero speed"), Profile.Speed(0.5), 0.0);
	TestEqual(TEXT("Nonfinite alpha cannot propagate into transforms"), Profile.Integral(std::numeric_limits<double>::quiet_NaN()), 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElevatorShaftFinishBoundaryTest,
	"Outlier.Elevator.Motion.Profile.FinishBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElevatorShaftFinishBoundaryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestEqual(TEXT("Early clear finishes only the current section"), FElevatorShaftSpeedProfile::FinishBoundary(25.0, 1450.0), 1450.0);
	TestEqual(TEXT("Exact boundary does not add another floor"), FElevatorShaftSpeedProfile::FinishBoundary(2900.0, 1450.0), 2900.0);
	TestEqual(TEXT("Boundary tolerance accepts either side"), FElevatorShaftSpeedProfile::FinishBoundary(2900.05, 1450.0), 2900.0);
	TestEqual(TEXT("Just below boundary uses that boundary"), FElevatorShaftSpeedProfile::FinishBoundary(2899.95, 1450.0), 2900.0);
	TestEqual(TEXT("Zero motion needs no extra section"), FElevatorShaftSpeedProfile::FinishBoundary(0.0, 1450.0), 0.0);
	TestEqual(TEXT("Missing height is rejected"), FElevatorShaftSpeedProfile::FinishBoundary(30.0, 0.0), 0.0);
	const double Finish = FElevatorShaftSpeedProfile::FinishBoundary(1450.0 * 1.e9 + 200.0, 1450.0);
	TestEqual(TEXT("Large accumulated distance retains the current section boundary"), Finish, 1450.0 * (1.e9 + 1.0));
	TestEqual(TEXT("Repeated completion of the reserved boundary cannot extend travel"), FElevatorShaftSpeedProfile::FinishBoundary(Finish, 1450.0), Finish);
	return true;
}

#endif
