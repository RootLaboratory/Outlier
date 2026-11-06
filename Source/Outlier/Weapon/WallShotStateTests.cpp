#if WITH_DEV_AUTOMATION_TESTS

#include "Shooter/Anim/WeaponWallAvoidance.h"
#include "Shooter/Anim/WeaponValues.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include <limits>

namespace
{
struct FWallStateWorld
{
	UWorld* World = nullptr;
	UBoxComponent* Wall = nullptr;
	const FVector Origin = FVector(0, 0, 1000);

	FWallStateWorld()
	{
		if (!GEngine) return;
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("WallStateTest")), GetTransientPackage());
		if (!World) return;
		World->AddToRoot();
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		AActor* Actor = World->SpawnActor<AActor>();
		Wall = NewObject<UBoxComponent>(Actor);
		Actor->SetRootComponent(Wall);
		Wall->SetCollisionObjectType(ECC_WorldStatic);
		Wall->SetCollisionResponseToAllChannels(ECR_Block);
		Wall->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Wall->RegisterComponent();
	}

	void PlaceWall(const FVector& LocalPosition, const FVector& Extent, const FRotator& Aim)
	{
		Wall->SetBoxExtent(Extent);
		Wall->SetWorldLocationAndRotation(Origin + Aim.RotateVector(LocalPosition), Aim);
		World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
		World->GetPhysicsScene()->Flush();
	}

	~FWallStateWorld()
	{
		if (!World) return;
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}
};

FWeaponValues ControlledProbeSettings()
{
	FWeaponValues Values;
	// 프로브 배치를 단순하게 고정해 어느 방향의 벽을 감지했는지 독립적으로 검증한다.
	Values.WallTraceDistance = 100.0f;
	Values.WallSafeDistance = 100.0f;
	Values.WallTraceRadius = 2.0f;
	Values.WallProbeForwardOffset = 0.0f;
	Values.WallProbeRightOffset = 0.0f;
	Values.WallProbeUpOffset = 0.0f;
	Values.WallVerticalProbeOffset = 20.0f;
	Values.WallSideProbeOffset = 20.0f;
	Values.WallMuzzleBlockForwardOffset = 40.0f;
	Values.WallMuzzleBlockRightOffset = 0.0f;
	Values.WallMuzzleBlockUpOffset = 0.0f;
	Values.WallMuzzleBlockTraceDistance = 30.0f;
	Values.WallMuzzleBlockTraceRadius = 2.0f;
	Values.WallBarrelBlockLength = 40.0f;
	Values.WallBarrelBlockTraceRadius = 2.0f;
	Values.WallPitchBlockTraceRadiusBoost = 0.0f;
	Values.WallPitchBlockLengthBoost = 0.0f;
	Values.bDrawWallOffsetDebug = false;
	return Values;
}

double RotationError(const FQuat& Left, const FQuat& Right)
{
	return FMath::RadiansToDegrees(Left.AngularDistance(Right));
}

bool SameState(const FWeaponWallAvoidanceState& Left, const FWeaponWallAvoidanceState& Right)
{
	return FMath::IsNearlyEqual(Left.AimAlpha, Right.AimAlpha, 0.000001f)
		&& Left.ViewModelWallOffsetLoc.Equals(Right.ViewModelWallOffsetLoc, 0.0001f)
		&& Left.ViewModelWallOffsetRot.Equals(Right.ViewModelWallOffsetRot, 0.0001f)
		&& FMath::IsNearlyEqual(Left.WallOffsetAlpha, Right.WallOffsetAlpha, 0.000001f)
		&& FMath::IsNearlyEqual(Left.WallAvoidUpAlpha, Right.WallAvoidUpAlpha, 0.000001f)
		&& FMath::IsNearlyEqual(Left.WallAvoidDownAlpha, Right.WallAvoidDownAlpha, 0.000001f)
		&& FMath::IsNearlyEqual(Left.WallAvoidSideAlpha, Right.WallAvoidSideAlpha, 0.000001f)
		&& FMath::IsNearlyEqual(Left.WallAvoidSideSign, Right.WallAvoidSideSign, 0.000001f)
		&& FMath::IsNearlyEqual(Left.WallMuzzleBlockAlpha, Right.WallMuzzleBlockAlpha, 0.000001f)
		&& FMath::IsNearlyEqual(Left.WallVeryCloseAlpha, Right.WallVeryCloseAlpha, 0.000001f)
		&& FMath::IsNearlyEqual(Left.WallHardStopAlpha, Right.WallHardStopAlpha, 0.000001f)
		&& FMath::IsNearlyEqual(Left.WallHardStopSafetyAlpha, Right.WallHardStopSafetyAlpha, 0.000001f)
		&& FMath::IsNearlyEqual(Left.WallCeilingAlpha, Right.WallCeilingAlpha, 0.000001f)
		&& FMath::IsNearlyEqual(Left.WallTopEdgeAlpha, Right.WallTopEdgeAlpha, 0.000001f)
		&& FMath::IsNearlyEqual(Left.WallMuzzleBlockReleaseHoldTimer, Right.WallMuzzleBlockReleaseHoldTimer, 0.000001f);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWallShotSharedStateTest, "Outlier.Weapon.WallShot.SharedState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FWallShotSharedStateTest::RunTest(const FString& Parameters)
{
	FWallStateWorld Fixture;
	if (!TestNotNull(TEXT("Collision world"), Fixture.World) || !TestNotNull(TEXT("Wall"), Fixture.Wall)) return false;
	FWeaponValues Values = ControlledProbeSettings();
	Fixture.PlaceWall(FVector(1000, 0, 0), FVector(2, 100, 100), FRotator::ZeroRotator);
	FWeaponWallAvoidanceState Clear;
	for (int32 Frame = 0; Frame < 60; ++Frame)
		Clear.Update(Fixture.World, nullptr, nullptr, Fixture.Origin, FRotator::ZeroRotator, 1.0f / 60.0f, &Values);
	TestEqual(TEXT("Distant wall does not start avoidance"), Clear.GetShotBlendAlpha(), 0.0f);
	TestTrue(TEXT("Distant wall leaves neutral root transform"), Clear.ViewModelWallOffsetLoc.IsZero()
		&& Clear.ViewModelWallOffsetRot.IsZero());
	struct FCase { const TCHAR* Name; FVector Position; FVector Extent; };
	const FCase Cases[] = {
		{TEXT("Front"), FVector(60, 0, 0), FVector(2, 100, 100)},
		{TEXT("Right"), FVector(60, 20, 0), FVector(2, 3, 3)},
		{TEXT("Left"), FVector(60, -20, 0), FVector(2, 3, 3)},
		{TEXT("Upper"), FVector(60, 0, 20), FVector(2, 3, 3)},
		{TEXT("Lower"), FVector(60, 0, -20), FVector(2, 3, 3)},
		{TEXT("Barrel"), FVector(20, 0, 0), FVector(2, 3, 3)},
		{TEXT("InitialOverlap"), FVector(0, 0, 0), FVector(2, 100, 100)}
	};
	int32 Samples = 0;
	FString Trajectories = TEXT("case,pitch,dt,frame,alpha,offsetX,offsetY,offsetZ,pitchOffset,yawOffset,rollOffset\n");
	for (float Dt : {1.0f / 60.0f, 1.0f / 30.0f, 1.0f / 15.0f, 0.25f})
	for (float Pitch : {-35.0f, 0.0f, 35.0f})
	for (const FCase& Case : Cases)
	{
		const FRotator Aim(Pitch, 27.0f, 0.0f);
		Fixture.PlaceWall(Case.Position, Case.Extent, Aim);
		Fixture.Wall->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		FWeaponWallAvoidanceState Authority;
		FWeaponWallAvoidanceState Display;
		const FString Label = FString::Printf(TEXT("%s/pitch=%.0f/dt=%.4f"), Case.Name, Pitch, Dt);
		for (int32 Frame = 0; Frame < FMath::CeilToInt(2.0f / Dt); ++Frame)
		{
			Authority.Update(Fixture.World, nullptr, nullptr, Fixture.Origin, Aim, Dt, &Values);
			Display.Update(Fixture.World, nullptr, nullptr, Fixture.Origin, Aim, Dt, &Values);
			Authority.UpdateAim(true, Dt, Values);
			Display.UpdateAim(true, Dt, Values);
			TestTrue(*(Label + TEXT(" same inputs/state/time")), SameState(Authority, Display));
			TestTrue(*(Label + TEXT(" finite output")), !Authority.ViewModelWallOffsetLoc.ContainsNaN()
				&& !Authority.ViewModelWallOffsetRot.ContainsNaN() && FMath::IsFinite(Authority.GetShotBlendAlpha()));
			++Samples;
			// 서로 다른 dt의 궤적은 원자료로 남긴다. 동일 dt의 결정성 검사와 혼동하지 않는다.
			Trajectories += FString::Printf(TEXT("%s,%.0f,%.8f,%d,%.8f,%.8f,%.8f,%.8f,%.8f,%.8f,%.8f\n"),
				Case.Name, Pitch, Dt, Frame, Authority.GetShotBlendAlpha(),
				Authority.ViewModelWallOffsetLoc.X, Authority.ViewModelWallOffsetLoc.Y, Authority.ViewModelWallOffsetLoc.Z,
				Authority.ViewModelWallOffsetRot.Pitch, Authority.ViewModelWallOffsetRot.Yaw, Authority.ViewModelWallOffsetRot.Roll);
		}
		TestTrue(*(Label + TEXT(" active avoidance")), Authority.GetShotBlendAlpha() > 0.0f);
		if (FCString::Strcmp(Case.Name, TEXT("Right")) == 0)
			TestTrue(*(Label + TEXT(" right wall pushes left")), Authority.WallAvoidSideSign < 0.0f);
		if (FCString::Strcmp(Case.Name, TEXT("Left")) == 0)
			TestTrue(*(Label + TEXT(" left wall pushes right")), Authority.WallAvoidSideSign > 0.0f);
		if (FCString::Strcmp(Case.Name, TEXT("Upper")) == 0)
			TestTrue(*(Label + TEXT(" upper-only wall produces ceiling")), Authority.WallCeilingAlpha > 0.0f);
		if (FCString::Strcmp(Case.Name, TEXT("Lower")) == 0)
			TestTrue(*(Label + TEXT(" lower-only wall produces top edge")), Authority.WallTopEdgeAlpha > 0.0f);
		AddInfo(FString::Printf(TEXT("[WallState] %s alpha=%.6f offset=%s rotation=%s"), *Label,
			Authority.GetShotBlendAlpha(), *Authority.ViewModelWallOffsetLoc.ToCompactString(),
			*Authority.ViewModelWallOffsetRot.ToCompactString()));

		// 해제는 실제 충돌 월드에서 벽을 제거한 뒤 진행한다. dt별 궤적의 동일성은 가정하지 않는다.
		Fixture.Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		for (int32 Frame = 0; Frame < FMath::CeilToInt(6.0f / Dt); ++Frame)
			Authority.Update(Fixture.World, nullptr, nullptr, Fixture.Origin, Aim, Dt, &Values);
		TestEqual(*(Label + TEXT(" release reaches zero")), Authority.GetShotBlendAlpha(), 0.0f);
		TestTrue(*(Label + TEXT(" release offset within 0.1 cm")), Authority.ViewModelWallOffsetLoc.IsNearlyZero(0.1f));
		TestTrue(*(Label + TEXT(" release rotation within 0.1 deg")), Authority.ViewModelWallOffsetRot.IsNearlyZero(0.1f));
	}
	AddInfo(FString::Printf(TEXT("[WallState] Shared-state samples=%d; no meshes or AnimInstances created"), Samples));
	const FString EvidenceDirectory = FPaths::ProjectSavedDir() / TEXT("WallShotValidation");
	IFileManager::Get().MakeDirectory(*EvidenceDirectory, true);
	TestTrue(TEXT("Per-dt trajectories saved"), FFileHelper::SaveStringToFile(Trajectories,
		*(EvidenceDirectory / TEXT("SharedStateTrajectories.csv"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWallShotTransformReadTest, "Outlier.Weapon.WallShot.TransformReads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FWallShotTransformReadTest::RunTest(const FString& Parameters)
{
	const FTransform View(FRotator(15, 175, 0), FVector(10, 20, 1000));
	const FTransform Muzzle(FRotator(-25, -155, 0), FVector(80, 35, 975));
	FWeaponWallAvoidanceState State;
	FTransform Result;
	TestTrue(TEXT("Clear transform resolves"), State.ResolveShotTransform(View, Muzzle, Result));
	TestTrue(TEXT("Clear state returns exact view"), Result.Equals(View, 0.000001f));
	State.WallOffsetAlpha = 1.0f;
	TestTrue(TEXT("Full transform resolves"), State.ResolveShotTransform(View, Muzzle, Result));
	TestTrue(TEXT("Full state returns actual supplied avoidance muzzle"), Result.Equals(Muzzle, 0.000001f));

	FTransform BeforeBoundary;
	FTransform AfterBoundary;
	State.WallOffsetAlpha = 0.0499f;
	State.ResolveShotTransform(View, Muzzle, BeforeBoundary);
	State.WallOffsetAlpha = 0.0501f;
	State.ResolveShotTransform(View, Muzzle, AfterBoundary);
	TestTrue(TEXT("No 0.05 position branch jump"), FVector::Distance(BeforeBoundary.GetLocation(), AfterBoundary.GetLocation()) < 0.1);
	TestTrue(TEXT("No 0.05 direction branch jump"), RotationError(BeforeBoundary.GetRotation(), AfterBoundary.GetRotation()) < 0.1);

	State.WallOffsetAlpha = 0.6f;
	State.AimAlpha = 0.4f;
	State.WallMuzzleBlockReleaseHoldTimer = 0.25f;
	const FWeaponWallAvoidanceState Snapshot = State;
	State.ResolveShotTransform(View, Muzzle, Result);
	for (int32 Read = 0; Read < 32; ++Read)
	{
		FTransform Again;
		TestTrue(TEXT("Repeated read succeeds"), State.ResolveShotTransform(View, Muzzle, Again));
		TestTrue(TEXT("Repeated reads return same transform"), Result.Equals(Again, 0.000001f));
		TestTrue(TEXT("Repeated reads preserve state/timer"), SameState(State, Snapshot));
		TestTrue(TEXT("Interpolated rotation normalized"), Again.GetRotation().IsNormalized());
		FWeaponValues Values;
		float Up = 0.0f;
		float Down = 0.0f;
		State.GetPoseAlphas(Values, 1.0f, Up, Down);
		State.GetAimBreakAlpha(Values);
		TestTrue(TEXT("Pose/aim reads do not advance ADS"), SameState(State, Snapshot));
	}

	// 사격 위치에는 충돌로 허용된 Lean 이동을 한 번 포함한다. 감지 상태 자체는 변하지 않는다.
	for (float Lean : {-1.0f, 1.0f})
	{
		const FVector Offset(0, Lean * 18.0f, 0);
		FTransform LeanView = View;
		FTransform LeanMuzzle = Muzzle;
		LeanView.AddToTranslation(Offset);
		LeanMuzzle.AddToTranslation(Offset);
		FTransform LeanResult;
		State.ResolveShotTransform(LeanView, LeanMuzzle, LeanResult);
		TestTrue(TEXT("Resolved Lean translated once"), LeanResult.GetLocation().Equals(Result.GetLocation() + Offset, 0.0001f));
		TestTrue(TEXT("Lean translation does not rotate barrel"), RotationError(LeanResult.GetRotation(), Result.GetRotation()) < 0.0001);
	}
	FTransform Invalid = Muzzle;
	Invalid.SetRotation(FQuat(0, 0, 0, 0));
	TestFalse(TEXT("Invalid muzzle is rejected, not replaced by origin"), State.ResolveShotTransform(View, Invalid, Result));
	TestTrue(TEXT("Failed read preserves state"), SameState(State, Snapshot));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWallShotStateResetTest, "Outlier.Weapon.WallShot.StateReset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FWallShotStateResetTest::RunTest(const FString& Parameters)
{
	FWallStateWorld Fixture;
	if (!TestNotNull(TEXT("Reset world"), Fixture.World)) return false;
	FWeaponValues Values = ControlledProbeSettings();
	Fixture.PlaceWall(FVector(60, 0, 0), FVector(2, 100, 100), FRotator::ZeroRotator);
	FWeaponWallAvoidanceState Active;
	for (int32 Frame = 0; Frame < 60; ++Frame)
		Active.Update(Fixture.World, nullptr, nullptr, Fixture.Origin, FRotator::ZeroRotator, 1.0f / 60.0f, &Values);
	if (!TestTrue(TEXT("Reset starts from active state"), Active.GetShotBlendAlpha() > 0.0f)) return false;
	// 200ms 늦은 서버 상태를 받은 뒤에도 그 이후의 해제 입력을 재생해야 한다.
	Fixture.Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FWeaponWallAvoidanceSnapshot Snapshot;
	Snapshot.ServerTime = 10.0;
	Snapshot.Sequence = 1;
	Snapshot.State = Active;
	AActor* FirstWeapon = Fixture.Wall->GetOwner();
	AActor* SecondWeapon = Fixture.World->SpawnActor<AActor>();
	Snapshot.Weapon = FirstWeapon;
	Snapshot.EquipmentRevision = 7;
	TestTrue(TEXT("Initial authoritative equipment pair is accepted"),
		Snapshot.MatchesEquipment(FirstWeapon, 7, nullptr, 0));
	TestFalse(TEXT("Same Actor re-equip rejects invalidated generation before new revision arrives"),
		Snapshot.MatchesEquipment(FirstWeapon, 7, FirstWeapon, 7));
	Snapshot.EquipmentRevision = 8;
	TestTrue(TEXT("New generation of same Actor accepts correction"),
		Snapshot.MatchesEquipment(FirstWeapon, 8, FirstWeapon, 7));
	Snapshot.Weapon = SecondWeapon;
	TestFalse(TEXT("New revision alone does not accept a different current weapon"),
		Snapshot.MatchesEquipment(FirstWeapon, 8, FirstWeapon, 7));
	TestTrue(TEXT("Revision-before-reference ordering accepts the final new pair"),
		Snapshot.MatchesEquipment(SecondWeapon, 8, FirstWeapon, 8));
	Snapshot.Weapon = nullptr;
	TArray<FWeaponWallPredictionFrame> ReplayFrames;
	FWeaponWallAvoidanceState ExpectedReplay = Active;
	for (int32 Frame = 1; Frame <= 4; ++Frame)
	{
		ReplayFrames.Add({10.0 + Frame * 0.05, 0.05f, Fixture.Origin, FRotator::ZeroRotator, true});
		ExpectedReplay.Update(Fixture.World, nullptr, nullptr, Fixture.Origin, FRotator::ZeroRotator, 0.05f, &Values);
		ExpectedReplay.UpdateAim(true, 0.05f, Values);
	}
	FWeaponWallAvoidanceState Reconciled;
	TestTrue(TEXT("Delayed server snapshot replays subsequent local inputs"),
		Snapshot.Reconcile(Fixture.World, nullptr, nullptr, Values, 10.2, -1.0, ReplayFrames, Reconciled));
	TestTrue(TEXT("Delayed correction reaches current prediction time"), SameState(Reconciled, ExpectedReplay));
	TestTrue(TEXT("Correction does not mutate received authoritative state"), SameState(Snapshot.State, Active));
	Snapshot.ServerTime = 9.9;
	TestTrue(TEXT("Initial correction can precede local equipment history"),
		Snapshot.Reconcile(Fixture.World, nullptr, nullptr, Values, 10.2, -1.0, ReplayFrames, Reconciled));
	Snapshot.ServerTime = 10.0;
	TestFalse(TEXT("Already applied server timestamp is rejected"),
		Snapshot.Reconcile(Fixture.World, nullptr, nullptr, Values, 10.2, 10.0, ReplayFrames, Reconciled));
	TestFalse(TEXT("Snapshot outside retained history is rejected"),
		Snapshot.Reconcile(Fixture.World, nullptr, nullptr, Values, 10.8, -1.0, ReplayFrames, Reconciled));
	Snapshot.State.WallSmoothedTargetAlpha = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("Nonfinite authoritative interpolation state is rejected"),
		Snapshot.Reconcile(Fixture.World, nullptr, nullptr, Values, 10.2, -1.0, ReplayFrames, Reconciled));
	Fixture.Wall->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	const FWeaponWallAvoidanceState Empty;
	FWeaponWallAvoidanceState State = Active;
	State.Reset();
	TestTrue(TEXT("Explicit lifecycle reset clears offsets and timers"), SameState(State, Empty));
	State = Active;
	State.Update(Fixture.World, nullptr, nullptr, Fixture.Origin, FRotator::ZeroRotator, 0.016f, nullptr);
	TestTrue(TEXT("Missing settings clear previous weapon state"), SameState(State, Empty));
	State = Active;
	State.Update(nullptr, nullptr, nullptr, Fixture.Origin, FRotator::ZeroRotator, 0.016f, &Values);
	TestTrue(TEXT("Missing world clears state"), SameState(State, Empty));
	for (float InvalidDt : {-0.1f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
	{
		State = Active;
		State.Update(Fixture.World, nullptr, nullptr, Fixture.Origin, FRotator::ZeroRotator, InvalidDt, &Values);
		TestTrue(TEXT("Invalid elapsed time resets safely"), SameState(State, Empty));
	}
	State = Active;
	State.Update(Fixture.World, nullptr, nullptr, FVector(std::numeric_limits<double>::infinity(), 0, 0),
		FRotator::ZeroRotator, 0.016f, &Values);
	TestTrue(TEXT("Nonfinite detection origin resets safely"), SameState(State, Empty));

	FWeaponWallAvoidanceState NoRoll;
	FWeaponWallAvoidanceState DisplayRoll;
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		NoRoll.Update(Fixture.World, nullptr, nullptr, Fixture.Origin, FRotator::ZeroRotator, 0.016f, &Values);
		DisplayRoll.Update(Fixture.World, nullptr, nullptr, Fixture.Origin, FRotator(0, 0, 25), 0.016f, &Values);
	}
	TestTrue(TEXT("Display roll does not enter wall detection"), SameState(NoRoll, DisplayRoll));

	// 실제 시간 갱신에서만 ADS를 진행한다. 벽 해제 후에는 같은 입력을 유지한 채 조준으로 복귀한다.
	Values.WallAimBreakStartAlpha = 0.25f;
	Values.WallAimBreakFullAlpha = 0.75f;
	for (float Dt : {1.0f / 60.0f, 1.0f / 30.0f, 1.0f / 15.0f, 0.25f})
	{
		FWeaponWallAvoidanceState Authority;
		FWeaponWallAvoidanceState Display;
		const auto AdvanceAim = [&](bool bAiming, float Seconds)
		{
			for (int32 Frame = 0; Frame < FMath::CeilToInt(Seconds / Dt); ++Frame)
			{
				Authority.UpdateAim(bAiming, Dt, Values);
				Display.UpdateAim(bAiming, Dt, Values);
				TestTrue(TEXT("Same ADS input/time is deterministic"), SameState(Authority, Display));
			}
		};
		AdvanceAim(true, 2.0f);
		TestTrue(TEXT("Clear-wall ADS reaches aim"), FMath::IsNearlyEqual(Authority.AimAlpha, 1.0f, 0.001f));
		Authority.WallMuzzleBlockAlpha = Display.WallMuzzleBlockAlpha = 0.5f;
		TestTrue(TEXT("Middle wall strength breaks half of ADS"), FMath::IsNearlyEqual(Authority.GetAimBreakAlpha(Values), 0.5f, 0.0001f));
		AdvanceAim(true, 2.0f);
		TestTrue(TEXT("Partial obstruction settles at partial aim"), FMath::IsNearlyEqual(Authority.AimAlpha, 0.5f, 0.001f));
		Authority.WallMuzzleBlockAlpha = Display.WallMuzzleBlockAlpha = 0.75f;
		AdvanceAim(true, 2.0f);
		TestTrue(TEXT("Full wall break leaves ADS"), FMath::IsNearlyZero(Authority.AimAlpha, 0.001f));
		Authority.WallMuzzleBlockAlpha = Display.WallMuzzleBlockAlpha = 0.0f;
		AdvanceAim(true, 2.0f);
		TestTrue(TEXT("Released wall restores held ADS input"), FMath::IsNearlyEqual(Authority.AimAlpha, 1.0f, 0.001f));
		AdvanceAim(false, 2.0f);
		TestTrue(TEXT("ADS input release returns to Hip"), FMath::IsNearlyZero(Authority.AimAlpha, 0.001f));
	}
	State.Reset();
	State.UpdateAim(true, 1.0f / 60.0f, Values);
	TestTrue(TEXT("One ADS time step applies exactly once"), FMath::IsNearlyEqual(State.AimAlpha, Values.AimInterpSpeedIn / 60.0f, 0.0001f));
	const float SingleStepAim = State.AimAlpha;
	for (int32 Read = 0; Read < 32; ++Read)
	{
		float Up = 0.0f;
		float Down = 0.0f;
		State.GetPoseAlphas(Values, 1.0f, Up, Down);
		State.GetAimBreakAlpha(Values);
	}
	TestEqual(TEXT("Same-frame readers leave ADS time unchanged"), State.AimAlpha, SingleStepAim);
	State.UpdateAim(true, std::numeric_limits<float>::quiet_NaN(), Values);
	TestEqual(TEXT("Invalid ADS elapsed time resets aim"), State.AimAlpha, 0.0f);

	// 단순 수치 예제로 최종 포즈 가중치를 검증한다. 구현식을 다시 복사해 기대값을 계산하지 않는다.
	Values.WallAvoidUpPoseAlphaScale = 1.2f;
	Values.WallMuzzleBlockUpPoseScale = 0.8f;
	Values.WallMuzzleBlockDownPoseScale = 0.6f;
	Values.WallVeryClosePoseSuppressScale = 0.5f;
	float Up = 0.0f;
	float Down = 0.0f;
	State.Reset();
	State.WallAvoidUpAlpha = 0.4f;
	State.WallCeilingAlpha = 0.25f;
	State.GetPoseAlphas(Values, 1.0f, Up, Down);
	TestTrue(TEXT("Ceiling attenuates up pose"), FMath::IsNearlyEqual(Up, 0.36f, 0.0001f));
	TestTrue(TEXT("Ceiling supplies down pose"), FMath::IsNearlyEqual(Down, 0.25f, 0.0001f));
	State.Reset();
	State.WallMuzzleBlockAlpha = 0.5f;
	State.WallMuzzleBlockDownPreferenceAlpha = 1.0f;
	State.GetPoseAlphas(Values, 1.0f, Up, Down);
	TestEqual(TEXT("Down preference excludes up muzzle pose"), Up, 0.0f);
	TestTrue(TEXT("Down preference uses down pose scale"), FMath::IsNearlyEqual(Down, 0.3f, 0.0001f));
	State.WallMuzzleBlockDownPreferenceAlpha = 0.0f;
	State.GetPoseAlphas(Values, 1.0f, Up, Down);
	TestTrue(TEXT("Up preference uses muzzle and up pose scales"), FMath::IsNearlyEqual(Up, 0.48f, 0.0001f));
	TestEqual(TEXT("Up preference excludes down muzzle pose"), Down, 0.0f);
	State.WallHardStopSafetyAlpha = 1.0f;
	State.GetPoseAlphas(Values, 1.0f, Up, Down);
	TestTrue(TEXT("Hard-stop safety suppresses both avoidance poses"), Up == 0.0f && Down == 0.0f);
	State.Reset();
	State.WallAvoidUpAlpha = 0.4f;
	State.GetPoseAlphas(Values, 0.5f, Up, Down);
	TestTrue(TEXT("Action pose scale is applied once"), FMath::IsNearlyEqual(Up, 0.24f, 0.0001f));
	State.WallVeryCloseAlpha = 1.0f;
	State.WallAvoidDownAlpha = 0.2f;
	State.GetPoseAlphas(Values, 1.0f, Up, Down);
	TestTrue(TEXT("Very-close suppression reduces up pose"), FMath::IsNearlyEqual(Up, 0.24f, 0.0001f));
	TestTrue(TEXT("Very-close suppression reduces down pose"), FMath::IsNearlyEqual(Down, 0.1f, 0.0001f));
	return true;
}

#endif
