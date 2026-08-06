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
	constexpr float OuterRadius = 1000.0f;

	TestEqual(
		TEXT("The explosion center uses maximum damage"),
		UExplosionSubsystem::CalculateDistanceDamage(0.0f, MaxDamage, OuterRadius, 1.0f),
		MaxDamage);
	TestEqual(
		TEXT("A linear curve keeps half strength at half range"),
		UExplosionSubsystem::CalculateFalloffRatio(500.0f, OuterRadius, 1.0f),
		0.5f);
	TestEqual(
		TEXT("A quadratic impulse curve keeps one quarter strength at half range"),
		UExplosionSubsystem::CalculateFalloffRatio(500.0f, OuterRadius, 2.0f),
		0.25f);
	TestEqual(
		TEXT("The outer radius applies no damage"),
		UExplosionSubsystem::CalculateDistanceDamage(OuterRadius, MaxDamage, OuterRadius, 1.5f),
		0.0f);
	TestEqual(
		TEXT("Outside the outer radius applies no damage"),
		UExplosionSubsystem::CalculateDistanceDamage(1000.1f, MaxDamage, OuterRadius, 1.5f),
		0.0f);

	return true;
}

#endif
