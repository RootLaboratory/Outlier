#include "Drone/FlightMovementComponentBase.h"

#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Curves/CurveFloat.h"
#include "HAL/IConsoleManager.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogFlightSpeed, Log, All);
static TAutoConsoleVariable<int32> CVarFlightSpeedDebug(TEXT("outlier.Flight.SpeedDebug"), 0,
	TEXT("Automatic flight speed profile: 0=off, 1=start/stop and one-second samples."));

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightSpeedProfileTest,
	"Outlier.Drone.Flight.SpeedProfile", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightSpeedProfileTest::RunTest(const FString& Parameters)
{
	FFlightSpeedProfile Profile;
	FFlightSpeedProfileState State;
	TestFalse(TEXT("Existing assets are opt-in"), Profile.IsEnabled());
	TestEqual(TEXT("Missing curves preserve requested speed"), State.Update(Profile, 400, 0, 100, 0.25f), 400.0f);
	UCurveFloat* Curve = NewObject<UCurveFloat>();
	Curve->FloatCurve.AddKey(0, 0);
	Curve->FloatCurve.AddKey(1, 1);
	Profile.AccelerationCurve = Curve;
	Profile.AccelerationDuration = 1.0f;
	TestEqual(TEXT("Accelerates from actual speed"), State.Update(Profile, 400, 100, 1000, 0.25f), 175.0f);
	TestEqual(TEXT("Retarget does not restart acceleration"), State.Update(Profile, 400, 175, 800, 0.25f), 250.0f);
	Curve->FloatCurve.UpdateOrAddKey(1, 0.5f);
	TestEqual(TEXT("Duration completes even with a low curve endpoint"), State.Update(Profile, 400, 250, 800, 0.5f), 400.0f);
	Curve->FloatCurve.UpdateOrAddKey(1, 1);
	Profile.DecelerationCurve = Curve;
	TestEqual(TEXT("Half braking distance limits speed by half"), State.Update(Profile, 400, 400, 100, 0.1f), 200.0f);
	TestEqual(TEXT("Endpoint retains enough speed to arrive"), State.Update(Profile, 400, 200, 0, 0.1f), 20.0f);
	TestEqual(TEXT("Charge ignores braking"), State.Update(Profile, 400, 400, 0, 0.1f, false), 400.0f);
	State.Reset();
	TestEqual(TEXT("Hard stop restarts from rest"), State.Update(Profile, 400, 0, 1000, 0.25f), 100.0f);
	const TArray<FVector> Path = { FVector::ZeroVector, FVector(10, 0, 0), FVector(10, 1000, 0) };
	State.SetPath(Path);
	TestEqual(TEXT("Braking uses entire detour, not nearby waypoint"), State.GetRemainingDistance(FVector::ZeroVector, Path, 1), 1010.0f);
	const float BeforeReselect = State.Elapsed;
	State.SetPath({ FVector::ZeroVector, FVector(2000, 0, 0) });
	TestEqual(TEXT("Replacing route retains acceleration clock"), State.Elapsed, BeforeReselect);
	Profile.AccelerationDuration = -1;
	Profile.DecelerationDistance = -1;
	TestFalse(TEXT("Invalid durations and distances disable their curves"), Profile.IsEnabled());
	TestEqual(TEXT("Negative speed cannot move backwards"), State.Update(Profile, -100, 0, 100, 1), 0.0f);
	return true;
}
#endif

namespace
{
float GetFlightCurveAlpha(const UCurveFloat& Curve, float Input)
{
	const float Value = Curve.GetFloatValue(Input);
	return FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.0f, 1.0f) : Input;
}
}

bool FFlightSpeedProfile::IsEnabled() const
{
	return (AccelerationCurve && FMath::IsFinite(AccelerationDuration) && AccelerationDuration > 0.0f)
		|| (DecelerationCurve && FMath::IsFinite(DecelerationDistance) && DecelerationDistance > 0.0f);
}

void FFlightSpeedProfileState::Reset()
{
	if (TargetSpeed >= 0.0f && CVarFlightSpeedDebug.GetValueOnGameThread() > 0)
	{
		UE_LOG(LogFlightSpeed, Log, TEXT("[Speed] Stop Owner=%s Elapsed=%.2f"), *GetNameSafe(LogOwner.Get()), Elapsed);
	}
	*this = FFlightSpeedProfileState();
}

void FFlightSpeedProfileState::SetPath(const TArray<FVector>& Points)
{
	// 선정 시 한 번 합산한다. Tick에서는 현재 구간과 남은 길이를 O(1)로 읽는다.
	PathSuffixDistances.SetNumZeroed(Points.Num());
	for (int32 Index = Points.Num() - 2; Index >= 0; --Index)
	{
		PathSuffixDistances[Index] = PathSuffixDistances[Index + 1]
			+ FVector::Distance(Points[Index], Points[Index + 1]);
	}
}

float FFlightSpeedProfileState::GetRemainingDistance(const FVector& Position,
	const TArray<FVector>& Points, int32 Index) const
{
	if (!Points.IsValidIndex(Index) || !PathSuffixDistances.IsValidIndex(Index)) return 0.0f;
	return FVector::Distance(Position, Points[Index]) + PathSuffixDistances[Index];
}

float FFlightSpeedProfileState::Update(const FFlightSpeedProfile& Profile, float DesiredSpeed,
	float ActualSpeed, float RemainingDistance, float DeltaTime, bool bAllowDeceleration,
	const UObject* Owner, const TCHAR* Mode)
{
	const float SafeSpeed = FMath::IsFinite(DesiredSpeed) ? FMath::Max(DesiredSpeed, 0.0f) : 0.0f;
	if (!Profile.IsEnabled()) return SafeSpeed;
	// 목적지/경유점 변경은 시간을 초기화하지 않는다. 속도 설정이 바뀔 때만 실제 속도에서 새 구간을 시작한다.
	if (!FMath::IsNearlyEqual(TargetSpeed, SafeSpeed))
	{
		StartSpeed = FMath::IsFinite(ActualSpeed) ? FMath::Clamp(ActualSpeed, 0.0f, SafeSpeed) : 0.0f;
		TargetSpeed = SafeSpeed;
		Elapsed = 0.0f;
		LogOwner = Owner;
		if (CVarFlightSpeedDebug.GetValueOnGameThread() > 0)
		{
			UE_LOG(LogFlightSpeed, Log, TEXT("[Speed] Begin Owner=%s Mode=%s Start=%.1f Target=%.1f"),
				*GetNameSafe(Owner), Mode, StartSpeed, TargetSpeed);
		}
	}
	Elapsed += FMath::IsFinite(DeltaTime) ? FMath::Max(DeltaTime, 0.0f) : 0.0f;
	float Speed = SafeSpeed;
	if (Profile.AccelerationCurve && FMath::IsFinite(Profile.AccelerationDuration)
		&& Profile.AccelerationDuration > 0.0f && Elapsed < Profile.AccelerationDuration)
	{
		const float Alpha = GetFlightCurveAlpha(*Profile.AccelerationCurve, Elapsed / Profile.AccelerationDuration);
		Speed = FMath::Lerp(StartSpeed, SafeSpeed, Alpha);
	}
	if (bAllowDeceleration && Profile.DecelerationCurve && FMath::IsFinite(Profile.DecelerationDistance)
		&& Profile.DecelerationDistance > 0.0f && FMath::IsFinite(RemainingDistance))
	{
		const float Ratio = FMath::Clamp(RemainingDistance / Profile.DecelerationDistance, 0.0f, 1.0f);
		if (Ratio < 1.0f)
		{
			const float Minimum = FMath::IsFinite(Profile.MinimumApproachSpeed)
				? FMath::Clamp(Profile.MinimumApproachSpeed, 1.0f, FMath::Max(SafeSpeed, 1.0f)) : 20.0f;
			Speed = FMath::Min(Speed, FMath::Max(Minimum,
				SafeSpeed * GetFlightCurveAlpha(*Profile.DecelerationCurve, Ratio)));
		}
	}
	Speed = FMath::Clamp(Speed, 0.0f, SafeSpeed);
	LogElapsed += FMath::IsFinite(DeltaTime) ? FMath::Max(DeltaTime, 0.0f) : 0.0f;
	if (CVarFlightSpeedDebug.GetValueOnGameThread() > 0 && LogElapsed >= 1.0f)
	{
		LogElapsed = 0.0f;
		UE_LOG(LogFlightSpeed, Log, TEXT("[Speed] Sample Owner=%s Mode=%s Actual=%.1f Limit=%.1f Remaining=%.1f Elapsed=%.2f"),
			*GetNameSafe(Owner), Mode, ActualSpeed, Speed, RemainingDistance, Elapsed);
	}
	return Speed;
}

UFlightMovementComponentBase::UFlightMovementComponentBase()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UFlightMovementComponentBase::OnRegister()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	Super::OnRegister();

	Activate(true);
	SetComponentTickEnabled(true);
}

void UFlightMovementComponentBase::BeginPlay()
{
	Super::BeginPlay();

	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.SetTickFunctionEnable(true);
	Activate(true);
	SetComponentTickEnabled(true);

	if (USceneComponent* VisualTiltRoot = GetFlightVisualTiltRoot())
	{
		BaseMeshRelativeRotation = VisualTiltRoot->GetRelativeRotation();
		bVisualTiltInitialized = true;
	}

	if (USceneComponent* ViewModelRoot = GetFlightViewModelRoot())
	{
		BaseViewModelRelativeRotation = ViewModelRoot->GetRelativeRotation();
		bViewModelTiltInitialized = true;
	}

	RefreshTickEnabled();
}

void UFlightMovementComponentBase::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!GetFlightOwnerCharacter())
	{
		return;
	}

	if (CanRunInputMovement())
	{
		UpdateInputMovement();
	}

	OnAfterInputMovement(DeltaTime);

	if (ShouldUpdateMovementFeel())
	{
		UpdateMovementFeel(DeltaTime);
	}

	RefreshTickEnabled();
}

void UFlightMovementComponentBase::SetMoveInput(const FVector2D& MoveInput)
{
	CurrentMoveInput = MoveInput;
	RefreshTickEnabled();
}

void UFlightMovementComponentBase::SetVerticalInput(float Axis)
{
	VerticalInput = Axis;
	RefreshTickEnabled();
}

void UFlightMovementComponentBase::ResetMovementFeel()
{
	ACharacter* OwnerCharacter = GetFlightOwnerCharacter();

	bMovementFeelInitialized = false;
	SmoothedVelocity = OwnerCharacter
		? OwnerCharacter->GetVelocity()
		: FVector::ZeroVector;
	SmoothedTiltTarget = FVector2D::ZeroVector;
	CurrentInertialPitch = 0.0f;
	CurrentInertialRoll = 0.0f;
	CurrentMeshPitch = 0.0f;
	CurrentMeshRoll = 0.0f;
	CurrentCameraPitch = 0.0f;
	CurrentCameraRoll = 0.0f;
	PreviousTargetPitch = 0.0f;
	PreviousTargetRoll = 0.0f;
	bPitchReboundActive = false;
	bRollReboundActive = false;
	PitchReboundTarget = 0.0f;
	RollReboundTarget = 0.0f;
	bMeshPitchReboundActive = false;
	bMeshRollReboundActive = false;
	MeshPitchReboundTarget = 0.0f;
	MeshRollReboundTarget = 0.0f;
	PreviousMeshTargetPitch = 0.0f;
	PreviousMeshTargetRoll = 0.0f;
	bMeshRotationInitialized = false;
	CurrentImpactMeshPitch = 0.0f;
	CurrentImpactMeshRoll = 0.0f;
	bExternalImpactTiltActive = false;
}

void UFlightMovementComponentBase::ApplyExternalImpactTilt(
	const FVector& WorldDirection,
	float NormalizedStrength,
	float MaxTiltDegrees,
	float RecoveryInterpSpeed)
{
	const ACharacter* OwnerCharacter = GetFlightOwnerCharacter();
	if (!OwnerCharacter)
	{
		return;
	}

	const FVector LocalDirection = OwnerCharacter->GetActorTransform()
		.InverseTransformVectorNoScale(WorldDirection.GetSafeNormal());
	const float TiltDegrees = FMath::Max(MaxTiltDegrees, 0.0f)
		* FMath::Clamp(NormalizedStrength, 0.0f, 1.0f);
	CurrentImpactMeshPitch = FMath::Clamp(
		CurrentImpactMeshPitch - LocalDirection.Z * TiltDegrees,
		-TiltDegrees,
		TiltDegrees);
	CurrentImpactMeshRoll = FMath::Clamp(
		CurrentImpactMeshRoll + LocalDirection.Y * TiltDegrees,
		-TiltDegrees,
		TiltDegrees);
	ImpactMeshTiltInterpSpeed = FMath::Max(RecoveryInterpSpeed, 0.0f);
	bExternalImpactTiltActive = true;
	RefreshTickEnabled();
}

void UFlightMovementComponentBase::RefreshTickEnabled()
{
	const bool bShouldTick = GetFlightOwnerCharacter() != nullptr;
	SetComponentTickEnabled(bShouldTick);
}

void UFlightMovementComponentBase::SetMoveSpeed(float NewMoveSpeed)
{
	MoveSpeed = FMath::Max(NewMoveSpeed, 0.0f);
}

void UFlightMovementComponentBase::SetBoostSpeed(float NewBoostSpeed)
{
	BoostSpeed = FMath::Max(NewBoostSpeed, 0.0f);
}

void UFlightMovementComponentBase::SetVerticalSpeed(float NewVerticalSpeed)
{
	VerticalSpeed = FMath::Max(NewVerticalSpeed, 0.0f);
}

void UFlightMovementComponentBase::SetAcceleration(float NewAcceleration)
{
	Acceleration = FMath::Max(NewAcceleration, 0.0f);
}

void UFlightMovementComponentBase::SetDeceleration(float NewDeceleration)
{
	Deceleration = FMath::Max(NewDeceleration, 0.0f);
}

void UFlightMovementComponentBase::SetRotationLagAmount(float NewRotationLagAmount)
{
	RotationLagAmount = FMath::Max(NewRotationLagAmount, 0.0f);
}

void UFlightMovementComponentBase::SetRotationLagRecoverSpeed(float NewRotationLagRecoverSpeed)
{
	RotationLagRecoverSpeed = FMath::Max(NewRotationLagRecoverSpeed, 0.0f);
}

void UFlightMovementComponentBase::SetCameraPitchOnMove(float NewCameraPitchOnMove)
{
	CameraPitchOnMove = FMath::Max(NewCameraPitchOnMove, 0.0f);
}

void UFlightMovementComponentBase::SetCameraRollOnTurn(float NewCameraRollOnTurn)
{
	CameraRollOnTurn = FMath::Max(NewCameraRollOnTurn, 0.0f);
}

void UFlightMovementComponentBase::SetCameraRollInterpSpeed(float NewCameraRollInterpSpeed)
{
	CameraRollInterpSpeed = FMath::Max(NewCameraRollInterpSpeed, 0.0f);
}

void UFlightMovementComponentBase::SetMeshInertialTiltScale(float NewMeshInertialTiltScale)
{
	MeshInertialTiltScale = NewMeshInertialTiltScale;
}

void UFlightMovementComponentBase::SetCameraInertialTiltScale(float NewCameraInertialTiltScale)
{
	CameraInertialTiltScale = NewCameraInertialTiltScale;
}

void UFlightMovementComponentBase::SetViewModelInertialTiltScale(float NewViewModelInertialTiltScale)
{
	ViewModelInertialTiltScale = NewViewModelInertialTiltScale;
}

void UFlightMovementComponentBase::SetInertialTiltReboundRatio(float NewInertialTiltReboundRatio)
{
	InertialTiltReboundRatio = FMath::Clamp(NewInertialTiltReboundRatio, 0.0f, 1.0f);
}

void UFlightMovementComponentBase::SetInertialTiltReboundMaxScale(float NewInertialTiltReboundMaxScale)
{
	InertialTiltReboundMaxScale = FMath::Max(NewInertialTiltReboundMaxScale, 0.0f);
}

void UFlightMovementComponentBase::SetInertialTiltReboundInterpMultiplier(float NewInertialTiltReboundInterpMultiplier)
{
	InertialTiltReboundInterpMultiplier = FMath::Max(NewInertialTiltReboundInterpMultiplier, 1.0f);
}

void UFlightMovementComponentBase::SetInertialTiltRecoverInterpMultiplier(float NewInertialTiltRecoverInterpMultiplier)
{
	InertialTiltRecoverInterpMultiplier = FMath::Max(NewInertialTiltRecoverInterpMultiplier, 1.0f);
}

void UFlightMovementComponentBase::SetAccelerating(bool bNewAccelerating)
{
	bIsAccelerating = bNewAccelerating;
}

ACharacter* UFlightMovementComponentBase::GetFlightOwnerCharacter() const
{
	return Cast<ACharacter>(GetOwner());
}

USkeletalMeshComponent* UFlightMovementComponentBase::GetFlightVisualMesh() const
{
	const ACharacter* OwnerCharacter = GetFlightOwnerCharacter();
	return OwnerCharacter ? OwnerCharacter->GetMesh() : nullptr;
}

USceneComponent* UFlightMovementComponentBase::GetFlightVisualTiltRoot() const
{
	return GetFlightVisualMesh();
}

USceneComponent* UFlightMovementComponentBase::GetFlightViewModelRoot() const
{
	return nullptr;
}

bool UFlightMovementComponentBase::CanRunInputMovement() const
{
	const ACharacter* OwnerCharacter = GetFlightOwnerCharacter();
	return OwnerCharacter && (OwnerCharacter->HasAuthority() || OwnerCharacter->IsLocallyControlled());
}

bool UFlightMovementComponentBase::ShouldUpdateMovementFeel() const
{
	const ACharacter* OwnerCharacter = GetFlightOwnerCharacter();
	return OwnerCharacter && (OwnerCharacter->IsLocallyControlled() || bExternalImpactTiltActive);
}

EFlightInputMode UFlightMovementComponentBase::GetFlightInputMode() const
{
	return EFlightInputMode::Free;
}

void UFlightMovementComponentBase::OnAfterInputMovement(float DeltaTime)
{
	(void)DeltaTime;
}

bool UFlightMovementComponentBase::HasMoveInput() const
{
	return !CurrentMoveInput.IsNearlyZero(0.01f) ||
		!FMath::IsNearlyZero(VerticalInput, 0.01f);
}

void UFlightMovementComponentBase::ClearFlightInput()
{
	CurrentMoveInput = FVector2D::ZeroVector;
	VerticalInput = 0.0f;
	RefreshTickEnabled();
}

void UFlightMovementComponentBase::UpdateInputMovement()
{
	ACharacter* OwnerCharacter = GetFlightOwnerCharacter();
	if (!OwnerCharacter)
	{
		return;
	}

	const float CurrentSpeed = bIsAccelerating ? BoostSpeed : MoveSpeed;
	const float VerticalScale = VerticalSpeed / FMath::Max(CurrentSpeed, KINDA_SMALL_NUMBER);
	const FVector Move = BuildWorldMoveVector(VerticalScale);

	if (!Move.IsNearlyZero())
	{
		OwnerCharacter->AddMovementInput(Move.GetClampedToMaxSize(1.0f), 1.0f);
	}
}

FVector UFlightMovementComponentBase::BuildWorldMoveVector(float VerticalInputScale) const
{
	const ACharacter* OwnerCharacter = GetFlightOwnerCharacter();
	if (!OwnerCharacter)
	{
		return FVector::ZeroVector;
	}

	if (GetFlightInputMode() == EFlightInputMode::Horizontal)
	{
		FVector Move =
			OwnerCharacter->GetActorForwardVector() * CurrentMoveInput.Y +
			OwnerCharacter->GetActorRightVector() * CurrentMoveInput.X;
		Move.Z = 0.0f;
		return Move + FVector::UpVector * VerticalInput * VerticalInputScale;
	}

	FRotator ViewRot = OwnerCharacter->GetControlRotation();
	ViewRot.Roll = 0.0f;
	return
		ViewRot.Vector() * CurrentMoveInput.Y +
		FRotationMatrix(ViewRot).GetScaledAxis(EAxis::Y) * CurrentMoveInput.X +
		FVector::UpVector * VerticalInput * VerticalInputScale;
}

void UFlightMovementComponentBase::UpdateMovementFeel(float DeltaTime)
{
	if (!CanUpdateMovementFeel(DeltaTime))
	{
		return;
	}

	if (!bVisualTiltInitialized)
	{
		if (USceneComponent* VisualTiltRoot = GetFlightVisualTiltRoot())
		{
			BaseMeshRelativeRotation = VisualTiltRoot->GetRelativeRotation();
			bVisualTiltInitialized = true;
		}
	}

	if (!bViewModelTiltInitialized)
	{
		if (USceneComponent* ViewModelRoot = GetFlightViewModelRoot())
		{
			BaseViewModelRelativeRotation = ViewModelRoot->GetRelativeRotation();
			bViewModelTiltInitialized = true;
		}
	}

	const FFlightMovementKinematics Kinematics = CalculateMovementKinematics(DeltaTime);
	const FFlightTiltTarget DesiredTarget = CalculateTiltTargets(Kinematics);

	UpdateInertialTilt(DesiredTarget, Kinematics, DeltaTime);
	UpdateMeshInertialTilt(Kinematics, DeltaTime);
	CurrentImpactMeshPitch = FMath::FInterpTo(
		CurrentImpactMeshPitch,
		0.0f,
		DeltaTime,
		ImpactMeshTiltInterpSpeed);
	CurrentImpactMeshRoll = FMath::FInterpTo(
		CurrentImpactMeshRoll,
		0.0f,
		DeltaTime,
		ImpactMeshTiltInterpSpeed);
	bExternalImpactTiltActive = !FMath::IsNearlyZero(CurrentImpactMeshPitch, 0.05f)
		|| !FMath::IsNearlyZero(CurrentImpactMeshRoll, 0.05f);
	ApplyCameraTilt();
	ApplyVisualTilt(DeltaTime);

	if (const ACharacter* OwnerCharacter = GetFlightOwnerCharacter())
	{
		LastLocation = OwnerCharacter->GetActorLocation();
	}
}

bool UFlightMovementComponentBase::CanUpdateMovementFeel(float DeltaTime) const
{
	return GetFlightOwnerCharacter() && DeltaTime > KINDA_SMALL_NUMBER;
}

FFlightMovementKinematics UFlightMovementComponentBase::CalculateMovementKinematics(float DeltaTime)
{
	FFlightMovementKinematics Result;

	ACharacter* OwnerCharacter = GetFlightOwnerCharacter();
	if (!OwnerCharacter)
	{
		return Result;
	}

	const FVector CurrentLocation = OwnerCharacter->GetActorLocation();
	if (!bMovementFeelInitialized)
	{
		LastLocation = CurrentLocation;
		bMovementFeelInitialized = true;
	}

	const FVector LocationVelocity = (CurrentLocation - LastLocation) / DeltaTime;
	FVector RawVelocity = OwnerCharacter->GetVelocity();
	if (RawVelocity.IsNearlyZero(1.0f) && !LocationVelocity.IsNearlyZero(1.0f))
	{
		RawVelocity = LocationVelocity;
	}

	Result.Acceleration = (RawVelocity - SmoothedVelocity) / DeltaTime;
	Result.Acceleration = Result.Acceleration.GetClampedToMaxSize(FMath::Max(Acceleration * 2.0f, 1.0f));

	SmoothedVelocity = FMath::VInterpTo(
		SmoothedVelocity,
		RawVelocity,
		DeltaTime,
		FMath::Max(RotationLagRecoverSpeed, 1.0f)
	);

	Result.Velocity = RawVelocity;
	Result.LocalVelocity = OwnerCharacter->GetActorTransform().InverseTransformVectorNoScale(RawVelocity);
	Result.LocalAcceleration = OwnerCharacter->GetActorTransform().InverseTransformVectorNoScale(Result.Acceleration);
	Result.MassFactor = GetMovementMassFactor();

	const float SpeedReference = FMath::Max(BoostSpeed, MoveSpeed);
	Result.SpeedAlpha = FMath::Clamp(
		Result.LocalVelocity.Size() / FMath::Max(SpeedReference, 1.0f),
		0.0f,
		1.0f
	);

	return Result;
}

FFlightTiltTarget UFlightMovementComponentBase::CalculateTiltTargets(const FFlightMovementKinematics& Kinematics) const
{
	FFlightTiltTarget Result;

	const float AccelReference = FMath::Max(Acceleration, 1.0f);
	Result.TiltStrength = FMath::Clamp(RotationLagAmount * 12.5f, 0.1f, 3.0f);
	Result.EffectiveMaxPitch = FMath::Max(CameraPitchOnMove, 0.0f) / Kinematics.MassFactor;
	Result.EffectiveMaxRoll = FMath::Max(CameraRollOnTurn, 0.0f) / Kinematics.MassFactor;

	const float PitchResponseScale = HasMoveInput() ? 1.0f : 0.5f;
	const float AccelPitch = FMath::Clamp(
		-Kinematics.LocalAcceleration.X / AccelReference * Result.EffectiveMaxPitch * 0.45f * PitchResponseScale,
		-Result.EffectiveMaxPitch,
		Result.EffectiveMaxPitch
	);
	const float AccelRoll = FMath::Clamp(
		Kinematics.LocalAcceleration.Y / AccelReference * Result.EffectiveMaxRoll,
		-Result.EffectiveMaxRoll,
		Result.EffectiveMaxRoll
	);

	float TurnRoll = 0.0f;
	if (!Kinematics.LocalVelocity.IsNearlyZero(20.0f))
	{
		const float RollDirectionScale = CurrentMoveInput.Y < -0.2f ? -1.0f : 1.0f;
		const FVector LocalVelocityDir = Kinematics.LocalVelocity.GetSafeNormal();
		const ACharacter* OwnerCharacter = GetFlightOwnerCharacter();
		FVector DesiredLocalDirection = OwnerCharacter
			? OwnerCharacter->GetActorTransform().InverseTransformVectorNoScale(
				BuildWorldMoveVector(0.35f))
			: FVector::ZeroVector;
		if (!DesiredLocalDirection.IsNearlyZero())
		{
			DesiredLocalDirection.Normalize();
			const float DirectionChange = FMath::Clamp(
				DesiredLocalDirection.Y - LocalVelocityDir.Y,
				-1.0f,
				1.0f
			);
			TurnRoll += DirectionChange * Result.EffectiveMaxRoll * 0.5f * RollDirectionScale;
		}

		const float SideSpeedAlpha = FMath::Clamp(
			Kinematics.LocalVelocity.Y / FMath::Max(MoveSpeed, 1.0f),
			-1.0f,
			1.0f
		);
		TurnRoll += SideSpeedAlpha * Result.EffectiveMaxRoll * 0.35f * RollDirectionScale;
	}

	const float VerticalPitch = FMath::Clamp(
		-Kinematics.LocalAcceleration.Z / AccelReference * Result.EffectiveMaxPitch * 0.15f * PitchResponseScale,
		-Result.EffectiveMaxPitch,
		Result.EffectiveMaxPitch
	);

	Result.TurnRoll = TurnRoll;
	Result.Pitch = FMath::Clamp(
		(AccelPitch + VerticalPitch) * Result.TiltStrength,
		-Result.EffectiveMaxPitch,
		Result.EffectiveMaxPitch
	);
	Result.Roll = FMath::Clamp(
		(AccelRoll + TurnRoll) * Result.TiltStrength,
		-Result.EffectiveMaxRoll,
		Result.EffectiveMaxRoll
	);

	return Result;
}

void UFlightMovementComponentBase::UpdateInertialTilt(
	const FFlightTiltTarget& TiltTarget,
	const FFlightMovementKinematics& Kinematics,
	float DeltaTime)
{
	float PitchInterpSpeedMultiplier = 1.0f;
	float RollInterpSpeedMultiplier = 1.0f;
	const float TargetBlendSpeed = FMath::Max(CameraRollInterpSpeed, 1.0f);

	SmoothedTiltTarget.X = FMath::FInterpTo(SmoothedTiltTarget.X, TiltTarget.Pitch, DeltaTime, TargetBlendSpeed);
	SmoothedTiltTarget.Y = FMath::FInterpTo(SmoothedTiltTarget.Y, TiltTarget.Roll, DeltaTime, TargetBlendSpeed);

	FFlightTiltTarget ResolvedTarget = TiltTarget;
	ResolvedTarget.Pitch = ResolveInertialReboundAxis(
		CurrentInertialPitch,
		SmoothedTiltTarget.X,
		TiltTarget.EffectiveMaxPitch,
		Kinematics.SpeedAlpha,
		Kinematics.MassFactor,
		PreviousTargetPitch,
		bPitchReboundActive,
		PitchReboundTarget,
		PitchInterpSpeedMultiplier
	);
	ResolvedTarget.Roll = ResolveInertialReboundAxis(
		CurrentInertialRoll,
		SmoothedTiltTarget.Y,
		TiltTarget.EffectiveMaxRoll,
		Kinematics.SpeedAlpha,
		Kinematics.MassFactor,
		PreviousTargetRoll,
		bRollReboundActive,
		RollReboundTarget,
		RollInterpSpeedMultiplier
	);

	const float BaseFeelInterpSpeed = CameraRollInterpSpeed / Kinematics.MassFactor;
	const float PitchInterpSpeed = BaseFeelInterpSpeed * PitchInterpSpeedMultiplier;
	const float RollInterpSpeed = BaseFeelInterpSpeed * RollInterpSpeedMultiplier;

	CurrentInertialPitch = FMath::FInterpTo(
		CurrentInertialPitch,
		ResolvedTarget.Pitch,
		DeltaTime,
		PitchInterpSpeed
	);
	CurrentInertialRoll = FMath::FInterpTo(
		CurrentInertialRoll,
		ResolvedTarget.Roll,
		DeltaTime,
		RollInterpSpeed
	);
}

void UFlightMovementComponentBase::UpdateMeshInertialTilt(
	const FFlightMovementKinematics& Kinematics,
	float DeltaTime)
{
	const ACharacter* OwnerCharacter = GetFlightOwnerCharacter();
	if (!OwnerCharacter)
	{
		return;
	}

	const float CurrentActorYaw = OwnerCharacter->GetActorRotation().Yaw;
	if (!bMeshRotationInitialized)
	{
		PreviousActorYaw = CurrentActorYaw;
		bMeshRotationInitialized = true;
	}

	const float SpeedReference = FMath::Max(FMath::Max(BoostSpeed, MoveSpeed), 1.0f);
	const float AccelReference = FMath::Max(Acceleration, 1.0f);
	const float EffectiveMaxPitch = FMath::Max(CameraPitchOnMove, 0.0f) / Kinematics.MassFactor;
	const float EffectiveMaxRoll = FMath::Max(CameraRollOnTurn, 0.0f) / Kinematics.MassFactor;

	const float ForwardSpeedAlpha = FMath::Clamp(
		Kinematics.LocalVelocity.X / SpeedReference,
		-1.0f,
		1.0f
	);
	const float VerticalAccelAlpha = FMath::Clamp(
		Kinematics.LocalAcceleration.Z / AccelReference,
		-1.0f,
		1.0f
	);
	const float TargetMeshPitch = FMath::Clamp(
		(-ForwardSpeedAlpha - VerticalAccelAlpha * 0.15f) * EffectiveMaxPitch,
		-EffectiveMaxPitch,
		EffectiveMaxPitch
	);

	const float YawDelta = FMath::FindDeltaAngleDegrees(PreviousActorYaw, CurrentActorYaw);
	const float YawRateAlpha = FMath::Clamp(YawDelta / DeltaTime / 120.0f, -1.0f, 1.0f);
	const float SideAccelAlpha = FMath::Clamp(
		Kinematics.LocalAcceleration.Y / AccelReference,
		-1.0f,
		1.0f
	);
	const float SideSpeedAlpha = FMath::Clamp(
		Kinematics.LocalVelocity.Y / SpeedReference,
		-1.0f,
		1.0f
	);
	const float TargetMeshRoll = FMath::Clamp(
		(YawRateAlpha * 0.8f + SideAccelAlpha * 0.4f + SideSpeedAlpha * 0.25f) * EffectiveMaxRoll,
		-EffectiveMaxRoll,
		EffectiveMaxRoll
	);

	// 메시도 감속하거나 선회 방향이 바뀔 때 반대 방향으로 짧게 튕겨야
	// AI 비행이 목표 각도만 부드럽게 따라가는 느낌에 머물지 않는다.
	float PitchInterpSpeedMultiplier = 1.0f;
	const float ResolvedMeshPitch = ResolveInertialReboundAxis(
		CurrentMeshPitch,
		TargetMeshPitch,
		EffectiveMaxPitch,
		Kinematics.SpeedAlpha,
		Kinematics.MassFactor,
		PreviousMeshTargetPitch,
		bMeshPitchReboundActive,
		MeshPitchReboundTarget,
		PitchInterpSpeedMultiplier
	);

	float RollInterpSpeedMultiplier = 1.0f;
	const float ResolvedMeshRoll = ResolveInertialReboundAxis(
		CurrentMeshRoll,
		TargetMeshRoll,
		EffectiveMaxRoll,
		Kinematics.SpeedAlpha,
		Kinematics.MassFactor,
		PreviousMeshTargetRoll,
		bMeshRollReboundActive,
		MeshRollReboundTarget,
		RollInterpSpeedMultiplier
	);

	const float MeshInterpSpeed = CameraRollInterpSpeed / Kinematics.MassFactor;
	CurrentMeshPitch = FMath::FInterpTo(
		CurrentMeshPitch,
		ResolvedMeshPitch,
		DeltaTime,
		MeshInterpSpeed * PitchInterpSpeedMultiplier
	);
	CurrentMeshRoll = FMath::FInterpTo(
		CurrentMeshRoll,
		ResolvedMeshRoll,
		DeltaTime,
		MeshInterpSpeed * RollInterpSpeedMultiplier
	);

	PreviousActorYaw = CurrentActorYaw;
}

void UFlightMovementComponentBase::ApplyCameraTilt()
{
	CurrentCameraPitch = FMath::Clamp(
		CurrentInertialPitch * CameraInertialTiltScale,
		-FMath::Max(CameraPitchOnMove, 0.0f),
		FMath::Max(CameraPitchOnMove, 0.0f)
	);
	CurrentCameraRoll = CurrentInertialRoll * CameraInertialTiltScale;
}

void UFlightMovementComponentBase::ApplyVisualTilt(float DeltaTime)
{
	ApplyMeshTilt();
	ApplyViewModelTilt(DeltaTime);
}

void UFlightMovementComponentBase::ApplyMeshTilt()
{
	USceneComponent* VisualTiltRoot = GetFlightVisualTiltRoot();
	if (!bVisualTiltInitialized || !VisualTiltRoot)
	{
		return;
	}

	const FRotator TargetMeshRot =
		BaseMeshRelativeRotation +
		FRotator(
			CurrentMeshPitch * MeshInertialTiltScale * MeshPitchInertialTiltScale + CurrentImpactMeshPitch,
			0.0f,
			CurrentMeshRoll * MeshInertialTiltScale * MeshRollInertialTiltScale + CurrentImpactMeshRoll
		);

	VisualTiltRoot->SetRelativeRotation(TargetMeshRot);
}

void UFlightMovementComponentBase::ApplyViewModelTilt(float DeltaTime)
{
	USceneComponent* ViewModelRoot = GetFlightViewModelRoot();
	if (!bViewModelTiltInitialized || !ViewModelRoot)
	{
		return;
	}

	const float FeelInterpSpeed = CameraRollInterpSpeed / GetMovementMassFactor();
	const FRotator TargetViewModelRot =
		BaseViewModelRelativeRotation +
		FRotator(
			CurrentInertialPitch * ViewModelInertialTiltScale,
			0.0f,
			CurrentInertialRoll * ViewModelInertialTiltScale
		);
	const FRotator NewViewModelRot = FMath::RInterpTo(
		ViewModelRoot->GetRelativeRotation(),
		TargetViewModelRot,
		DeltaTime,
		FeelInterpSpeed
	);

	ViewModelRoot->SetRelativeRotation(NewViewModelRot);
}

float UFlightMovementComponentBase::GetMovementMassFactor() const
{
	const ACharacter* OwnerCharacter = GetFlightOwnerCharacter();
	const UCharacterMovementComponent* CharacterMovement = OwnerCharacter
		? OwnerCharacter->GetCharacterMovement()
		: nullptr;

	if (!CharacterMovement)
	{
		return 1.0f;
	}

	const float NormalizedMass = FMath::Clamp(CharacterMovement->Mass / 25.0f, 0.5f, 3.0f);
	return FMath::Sqrt(NormalizedMass);
}

float UFlightMovementComponentBase::ResolveInertialReboundAxis(
	float CurrentValue,
	float DesiredTarget,
	float MaxAbsValue,
	float SpeedAlpha,
	float MassFactor,
	float& PreviousDesiredTarget,
	bool& bReboundActive,
	float& ReboundTarget,
	float& OutInterpSpeedMultiplier) const
{
	OutInterpSpeedMultiplier = 1.0f;

	const float TriggerMagnitude = FMath::Max(MaxAbsValue * 0.25f, 0.75f);
	const float CurrentMagnitude = FMath::Abs(CurrentValue);
	const float DesiredMagnitude = FMath::Abs(DesiredTarget);
	const float PreviousMagnitude = FMath::Abs(PreviousDesiredTarget);
	const bool bHadMeaningfulTilt = CurrentMagnitude > TriggerMagnitude || PreviousMagnitude > TriggerMagnitude;
	const bool bReleased = PreviousMagnitude > TriggerMagnitude && DesiredMagnitude < PreviousMagnitude * 0.45f;
	const bool bReversed = PreviousDesiredTarget * DesiredTarget < -0.1f;

	if (!bReboundActive && bHadMeaningfulTilt && (bReleased || bReversed))
	{
		const float SpeedScale = FMath::Lerp(0.35f, 1.0f, SpeedAlpha);
		const float MassScale = FMath::Clamp(1.0f / FMath::Max(MassFactor, 0.1f), 0.35f, 1.5f);
		const float MaxReboundMagnitude = MaxAbsValue * InertialTiltReboundMaxScale;
		const float ReboundMagnitude = FMath::Clamp(
			CurrentMagnitude * InertialTiltReboundRatio * SpeedScale * MassScale,
			0.0f,
			MaxReboundMagnitude
		);

		ReboundTarget = -FMath::Sign(CurrentValue) * ReboundMagnitude;
		bReboundActive = true;
		OutInterpSpeedMultiplier = FMath::Max(InertialTiltReboundInterpMultiplier, 1.0f);
		PreviousDesiredTarget = DesiredTarget;
		return ReboundTarget;
	}

	if (bReboundActive)
	{
		OutInterpSpeedMultiplier = FMath::Max(InertialTiltReboundInterpMultiplier, 1.0f);
		const float MovementOverrideAlpha = FMath::Clamp(
			DesiredMagnitude / FMath::Max(MaxAbsValue, 1.0f) * FMath::Lerp(0.5f, 1.0f, SpeedAlpha),
			0.0f,
			1.0f
		);
		const float BlendedTarget = FMath::Lerp(ReboundTarget, DesiredTarget, MovementOverrideAlpha);
		const float CompletionTolerance = FMath::Max(MaxAbsValue * 0.04f, 0.2f);

		if (FMath::Abs(CurrentValue - BlendedTarget) <= CompletionTolerance &&
			DesiredMagnitude < TriggerMagnitude * 0.5f)
		{
			bReboundActive = false;
			OutInterpSpeedMultiplier = FMath::Max(InertialTiltRecoverInterpMultiplier, 1.0f);
			PreviousDesiredTarget = DesiredTarget;
			return 0.0f;
		}

		if (MovementOverrideAlpha > 0.85f)
		{
			bReboundActive = false;
		}

		PreviousDesiredTarget = DesiredTarget;
		return BlendedTarget;
	}

	if (DesiredMagnitude < TriggerMagnitude * 0.5f)
	{
		OutInterpSpeedMultiplier = FMath::Max(InertialTiltRecoverInterpMultiplier, 1.0f);
		PreviousDesiredTarget = DesiredTarget;
		return 0.0f;
	}

	PreviousDesiredTarget = DesiredTarget;
	return DesiredTarget;
}
