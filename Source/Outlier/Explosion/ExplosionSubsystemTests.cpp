#if WITH_DEV_AUTOMATION_TESTS

#include "Explosion/ExplosionSubsystem.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FExplosionDistanceDamageTest,
	"Outlier.Explosion.DistanceDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExplosionDistanceDamageTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	constexpr float MaxDamage = 10.0f;
	constexpr float MinDamage = 2.0f;
	constexpr float InnerRadius = 500.0f;
	constexpr float OuterRadius = 1000.0f;

	TestEqual(
		TEXT("Inside the inner radius uses maximum damage"),
		UExplosionSubsystem::CalculateDistanceDamage(250.0f, MaxDamage, MinDamage, InnerRadius, OuterRadius),
		MaxDamage);
	TestEqual(
		TEXT("The middle of the falloff range is linear"),
		UExplosionSubsystem::CalculateDistanceDamage(750.0f, MaxDamage, MinDamage, InnerRadius, OuterRadius),
		6.0f);
	TestEqual(
		TEXT("The outer radius uses minimum damage"),
		UExplosionSubsystem::CalculateDistanceDamage(OuterRadius, MaxDamage, MinDamage, InnerRadius, OuterRadius),
		MinDamage);
	TestEqual(
		TEXT("Outside the outer radius applies no damage"),
		UExplosionSubsystem::CalculateDistanceDamage(1000.1f, MaxDamage, MinDamage, InnerRadius, OuterRadius),
		0.0f);

	return true;
}

#endif
