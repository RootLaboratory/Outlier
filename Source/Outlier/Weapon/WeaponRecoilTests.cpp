#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Weapon/WeaponRecoilCalculation.h"
#include "Weapon/WeaponRecoilRow.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponRecoilDeterminismTest,
	"Outlier.Weapon.Recoil.DeterminismAndLimits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponRecoilDeterminismTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FWeaponRecoilRow Profile;
	Profile.ControlPitchPerShot = 1.0f;
	Profile.ControlPitchRandomRange = 0.1f;
	Profile.ControlYawPerShot = 0.3f;
	Profile.ControlYawRandomRange = 0.1f;
	Profile.YawDirectionPersistence = 0.7f;
	Profile.RecoilGrowthPerShot = 0.2f;
	Profile.MaxRecoilMultiplier = 1.5f;
	Profile.MaxAccumulatedPitchDegrees = 3.0f;
	Profile.MaxAccumulatedYaw = 0.8f;

	FWeaponRecoilRuntimeState FirstState;
	FWeaponRecoilRuntimeState SecondState;
	for (uint32 ShotSequence = 1; ShotSequence <= 8; ++ShotSequence)
	{
		const FVector2D FirstResult = OutlierWeaponRecoil::CalculateControlRecoil(
			Profile,
			1.0f,
			ShotSequence,
			FirstState);
		const FVector2D SecondResult = OutlierWeaponRecoil::CalculateControlRecoil(
			Profile,
			1.0f,
			ShotSequence,
			SecondState);

		TestTrue(TEXT("The same shot sequence produces the same recoil"), FirstResult.Equals(SecondResult));
	}

	TestTrue(
		TEXT("Accumulated pitch stays inside the profile limit"),
		FirstState.AccumulatedPitch <= Profile.MaxAccumulatedPitchDegrees);
	TestTrue(
		TEXT("Accumulated yaw stays inside the profile limit"),
		FMath::Abs(FirstState.AccumulatedYaw) <= Profile.MaxAccumulatedYaw);

	FirstState.Reset();
	TestEqual(TEXT("Reset clears the consecutive shot count"), FirstState.ConsecutiveShotCount, 0);
	TestEqual(TEXT("Reset clears accumulated pitch"), FirstState.AccumulatedPitch, 0.0f);
	TestEqual(TEXT("Reset clears accumulated yaw"), FirstState.AccumulatedYaw, 0.0f);
	TestEqual(TEXT("Reset clears yaw persistence"), FirstState.PersistentYawDirection, static_cast<int8>(0));

	return true;
}

#endif
