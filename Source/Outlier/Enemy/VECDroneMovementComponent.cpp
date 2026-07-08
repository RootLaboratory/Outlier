#include "Enemy/VECDroneMovementComponent.h"

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

bool UVECDroneMovementComponent::CanRunInputMovement() const
{
	const AVECDrone* Drone = GetVECDroneOwner();
	if (!Drone)
	{
		return false;
	}

	return Drone->HasAuthority()
		? Drone->IsEnemyPossessed()
		: Drone->IsLocallyControlled();
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

AVECDrone* UVECDroneMovementComponent::GetVECDroneOwner() const
{
	return Cast<AVECDrone>(GetOwner());
}
