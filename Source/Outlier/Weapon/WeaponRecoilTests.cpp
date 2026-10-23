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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponRecoilUpgradeScalingTest,
	"Outlier.Weapon.Recoil.UpgradeScaling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponRecoilUpgradeScalingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	// 무기 자체 배율이 달라도 업그레이드는 최종 반동을 같은 비율로 줄인다.
	for (const float WeaponScale : { 1.0f, 0.6f })
	{
		FWeaponRecoilRow Profile;
		Profile.ControlPitchPerShot = 1.0f;
		Profile.ControlYawPerShot = 0.3f;
		Profile.RecoilGrowthPerShot = 0.2f;
		Profile.MaxRecoilMultiplier = 1.5f;
		Profile.MaxAccumulatedPitchDegrees = 2.5f;
		Profile.MaxAccumulatedYaw = 0.5f;
		Profile.YawDirectionPersistence = 1.0f;

		FWeaponRecoilRuntimeState BaseState;
		FWeaponRecoilRuntimeState FirstUpgradeState;
		FWeaponRecoilRuntimeState BothUpgradesState;
		for (uint32 Shot = 1; Shot <= 8; ++Shot)
		{
			const FVector2D Base = OutlierWeaponRecoil::CalculateControlRecoil(Profile, WeaponScale, Shot, BaseState);
			const FVector2D First = OutlierWeaponRecoil::CalculateControlRecoil(
				Profile, WeaponScale, Shot, FirstUpgradeState, 0.9f);
			const FVector2D Both = OutlierWeaponRecoil::CalculateControlRecoil(
				Profile, WeaponScale, Shot, BothUpgradesState, 0.8f);
			TestTrue(TEXT("First upgrade reduces every shot to 90%, including at the cap"), First.Equals(Base * 0.9f));
			TestTrue(TEXT("Both upgrades reduce every shot to 80%, including at the cap"), Both.Equals(Base * 0.8f));
		}
		TestEqual(TEXT("Burst reaches the original pitch cap"), BaseState.AccumulatedPitch, 2.5f);
		TestEqual(TEXT("Upgrade does not change the underlying burst state"),
			BothUpgradesState.AccumulatedPitch, BaseState.AccumulatedPitch);
	}

	FWeaponRecoilRow Profile;
	Profile.ControlPitchPerShot = 1.0f;
	Profile.MaxAccumulatedPitchDegrees = 3.0f;
	FWeaponRecoilRuntimeState State;
	TestTrue(TEXT("A negative upgrade multiplier cannot reverse recoil"),
		OutlierWeaponRecoil::CalculateControlRecoil(Profile, 1.0f, 1, State, -0.1f).IsNearlyZero());
	State.Reset();
	TestTrue(TEXT("A zero upgrade multiplier removes control recoil"),
		OutlierWeaponRecoil::CalculateControlRecoil(Profile, 1.0f, 1, State, 0.0f).IsNearlyZero());
	return true;
}

#endif
