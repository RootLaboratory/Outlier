#include "Enemy/VECDroneMovementComponent.h"

#include "Components/SceneComponent.h"
#include "Enemy/VECDrone.h"

UVECDroneMovementComponent::UVECDroneMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UVECDroneMovementComponent::ApplyDroneMoveSpeed(float NewMoveSpeed)
{
	const float ClampedMoveSpeed = FMath::Max(NewMoveSpeed, 0.0f);
	SetMoveSpeed(ClampedMoveSpeed);
	SetBoostSpeed(ClampedMoveSpeed);
	SetVerticalSpeed(ClampedMoveSpeed);
}

ACharacter* UVECDroneMovementComponent::GetFlightOwnerCharacter() const
{
	return GetVECDroneOwner();
}

USceneComponent* UVECDroneMovementComponent::GetFlightVisualTiltRoot() const
{
	const AVECDrone* Drone = GetVECDroneOwner();
	return Drone ? Drone->GetThirdPersonTiltRoot() : nullptr;
}

USceneComponent* UVECDroneMovementComponent::GetFlightViewModelRoot() const
{
	const AVECDrone* Drone = GetVECDroneOwner();
	return Drone ? Drone->GetFirstPersonViewModelRoot() : nullptr;
}

bool UVECDroneMovementComponent::CanRunInputMovement() const
{
	const AVECDrone* Drone = GetVECDroneOwner();
	if (!Drone)
	{
		return false;
	}

	return Drone->HasAuthority()
		? Drone->IsEnemyPossessed() && !Drone->IsPossessedImpactInputLocked()
		: Drone->IsLocallyControlled() && !Drone->IsPossessedImpactInputLocked();
}

bool UVECDroneMovementComponent::ShouldUpdateMovementFeel() const
{
	const AVECDrone* Drone = GetVECDroneOwner();
	if (!Drone || Drone->GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}

	if (Drone->IsEnemyPossessed() && Drone->IsLocallyControlled())
	{
		return true;
	}

	return Drone->WasRecentlyRendered();
}

EFlightInputMode UVECDroneMovementComponent::GetFlightInputMode() const
{
	return EFlightInputMode::Free;
}

void UVECDroneMovementComponent::OnAfterInputMovement(float DeltaTime)
{
	Super::OnAfterInputMovement(DeltaTime);

	// 빙의 중에는 공용 StateTree 대신 Pawn이 짧은 물리 반동 감쇠를 직접 갱신한다.
	if (AVECDrone* Drone = GetVECDroneOwner())
	{
		Drone->UpdatePossessedImpactRecovery(DeltaTime);
	}

	if (ShouldUpdateMovementFeel())
	{
		UpdateAIFacingPitch(DeltaTime);
	}
}

void UVECDroneMovementComponent::UpdateAIFacingPitch(float DeltaTime)
{
	AVECDrone* Drone = GetVECDroneOwner();
	USceneComponent* PitchRoot = Drone ? Drone->GetAIFacingPitchRoot() : nullptr;
	if (!Drone || !PitchRoot)
	{
		return;
	}

	// 컴포넌트가 붙어 있던 최초 상대 회전값(에디터에서 설정한 오프셋)을 기준선으로 잡아두고,
	// 이후로는 그 기준선에 목표 Pitch만 더한다 — 오프셋을 매 프레임 덮어쓰지 않기 위함.
	if (!bAIFacingPitchInitialized)
	{
		BaseAIFacingPitchRotation = PitchRoot->GetRelativeRotation();
		bAIFacingPitchInitialized = true;
	}

	// GetBaseAimRotation()은 빙의 중엔 플레이어 카메라 Pitch, AI 주행 중엔 AIController가
	// RotateTowardLocation 등으로 설정한 ControlRotation.Pitch를 반환한다 — 즉 "누가 조종하든"
	// 같은 소스에서 Pitch를 읽어 이 회전축에 반영할 수 있다.
	const float DesiredPitch = FMath::ClampAngle(
		Drone->GetBaseAimRotation().Pitch,
		-89.0f,
		89.0f
	);
	FRotator TargetRotation = BaseAIFacingPitchRotation;
	TargetRotation.Pitch = FRotator::NormalizeAxis(
		BaseAIFacingPitchRotation.Pitch + DesiredPitch
	);

	PitchRoot->SetRelativeRotation(FMath::RInterpTo(
		PitchRoot->GetRelativeRotation(),
		TargetRotation,
		DeltaTime,
		AIFacingPitchInterpSpeed
	));
}

AVECDrone* UVECDroneMovementComponent::GetVECDroneOwner() const
{
	return Cast<AVECDrone>(GetOwner());
}
