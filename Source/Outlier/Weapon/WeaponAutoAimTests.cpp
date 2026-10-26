#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Weapon/WeaponAutoAimCalculation.h"
#include "GAS/Data/OutlierShooterSuitAbilityDataRow.h"

#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponAutoAimScreenProjectionTest,
	"Outlier.Weapon.AutoAim.ScreenProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponAutoAimScreenProjectionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	float DistanceSquared = 0.0f;
	constexpr float Aspect = 16.0f / 9.0f;
	auto IsInside = [&](const FVector& Point, float FOV = 90.0f, float ViewAspect = 16.0f / 9.0f, float WidthRatio = 1.0f / 3.0f)
	{
		return OutlierAutoAim::TryGetScreenDistanceSquared(Point, FOV, ViewAspect, WidthRatio, DistanceSquared);
	};

	TestTrue(TEXT("Crosshair center is inside"), IsInside(FVector(300.0, 0.0, 0.0)));
	TestEqual(TEXT("Center distance is zero"), DistanceSquared, 0.0f);
	TestTrue(TEXT("One-third width circle includes its horizontal boundary"), IsInside(FVector(300.0, 100.0, 0.0)));
	TestTrue(TEXT("Boundary distance uses screen width"), FMath::IsNearlyEqual(DistanceSquared, 1.0f / 36.0f));
	TestTrue(TEXT("Vertical radius is identical to horizontal radius"), IsInside(FVector(300.0, 0.0, 100.0)));
	TestFalse(TEXT("Just beyond the circle is excluded"), IsInside(FVector(300.0, 100.1, 0.0)));
	TestFalse(TEXT("Circle excludes points inside its bounding square but outside its radius"), IsInside(FVector(300.0, 90.0, 90.0)));
	TestFalse(TEXT("Point behind the camera is excluded"), IsInside(FVector(-300.0, 0.0, 0.0)));
	TestFalse(TEXT("Camera plane is excluded"), IsInside(FVector::ZeroVector));
	TestTrue(TEXT("Wider FOV brings the same point into the circle"), IsInside(FVector(300.0, 150.0, 0.0), 120.0f));
	TestFalse(TEXT("Narrower FOV moves the same point outside the circle"), IsInside(FVector(300.0, 100.0, 0.0), 60.0f));
	TestTrue(TEXT("Larger configured diameter admits a farther point"), IsInside(FVector(300.0, 150.0, 0.0), 90.0f, Aspect, 0.6f));
	TestFalse(TEXT("Ultrawide screen clips a point within the circle but above the viewport"),
		IsInside(FVector(300.0, 0.0, 95.0), 90.0f, 32.0f / 9.0f));
	TestTrue(TEXT("Same point is visible in a taller viewport"), IsInside(FVector(300.0, 0.0, 95.0)));
	TestFalse(TEXT("Horizontal viewport boundary is enforced"), IsInside(FVector(300.0, 301.0, 0.0), 90.0f, Aspect, 1.0f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponAutoAimInvalidProjectionTest,
	"Outlier.Weapon.AutoAim.InvalidProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponAutoAimInvalidProjectionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	float DistanceSquared = 1.0f;
	const float NaN = std::numeric_limits<float>::quiet_NaN();
	const float Infinity = std::numeric_limits<float>::infinity();
	const FVector Center(300.0, 0.0, 0.0);
	auto IsInside = [&](float FOV, float Aspect, float Diameter)
	{
		return OutlierAutoAim::TryGetScreenDistanceSquared(Center, FOV, Aspect, Diameter, DistanceSquared);
	};

	for (const float InvalidFOV : { 0.0f, -1.0f, 180.0f, 181.0f, NaN, Infinity })
	{
		TestFalse(TEXT("Invalid FOV is rejected"), IsInside(InvalidFOV, 16.0f / 9.0f, 1.0f / 3.0f));
	}
	for (const float InvalidAspect : { 0.0f, -1.0f, NaN, Infinity })
	{
		TestFalse(TEXT("Invalid aspect ratio is rejected"), IsInside(90.0f, InvalidAspect, 1.0f / 3.0f));
	}
	for (const float InvalidDiameter : { 0.0f, -1.0f, 1.01f, NaN, Infinity })
	{
		TestFalse(TEXT("Invalid circle diameter is rejected"), IsInside(90.0f, 16.0f / 9.0f, InvalidDiameter));
	}
	for (const FVector InvalidPoint : { FVector(NaN, 0.0, 0.0), FVector(300.0, Infinity, 0.0), FVector(300.0, 0.0, NaN) })
	{
		TestFalse(TEXT("Non-finite point is rejected"), OutlierAutoAim::TryGetScreenDistanceSquared(
			InvalidPoint, 90.0f, 16.0f / 9.0f, 1.0f / 3.0f, DistanceSquared));
	}
	TestEqual(TEXT("Rejected input clears the output distance"), DistanceSquared, 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponAutoAimConfigTest,
	"Outlier.Weapon.AutoAim.Configuration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponAutoAimConfigTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FOutlierShooterSuitConfig Config;
	FString Error;
	TestTrue(TEXT("Default configuration accepts the one-third width circle"), Config.IsValid(Error));
	TestTrue(TEXT("Default circle diameter is one-third of the screen width"),
		FMath::IsNearlyEqual(Config.WeaponOvercharge.AutoAimDiameterWidthRatio, 1.0f / 3.0f));
	const FOutlierShooterSuitConfig Original = Config;
	Config.WeaponOvercharge.AutoAimDiameterWidthRatio = 0.5f;
	TestFalse(TEXT("Changing the circle size changes configuration equality"), Config.Equals(Original));
	for (const float InvalidRatio : {0.0f, -0.1f, 1.01f, std::numeric_limits<float>::quiet_NaN(),
		std::numeric_limits<float>::infinity()})
	{
		Config.WeaponOvercharge.AutoAimDiameterWidthRatio = InvalidRatio;
		TestFalse(TEXT("Invalid circle configuration is rejected"), Config.IsValid(Error));
		TestTrue(TEXT("Configuration error identifies the circle field"), Error.Contains(TEXT("AutoAimDiameterWidthRatio")));
	}
	return true;
}

#endif
