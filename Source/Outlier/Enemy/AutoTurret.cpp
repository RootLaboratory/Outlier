#include "Enemy/AutoTurret.h"

#include "GameFramework/CharacterMovementComponent.h"

AAutoTurret::AAutoTurret()
{
	ApplyClassStatOverrides();

	if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
	{
		MovementComponent->DisableMovement();
		MovementComponent->MaxWalkSpeed = 0.0f;
		MovementComponent->MaxFlySpeed = 0.0f;
	}
}

void AAutoTurret::ApplyClassStatOverrides()
{
	SetDefaultEnemyType(EEnemyType::Turret);
	RuntimeStat.MoveSpeed = 0.0f;
}

void AAutoTurret::ApplyMovementFromRuntimeStat()
{
	if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
	{
		MovementComponent->DisableMovement();
		MovementComponent->MaxWalkSpeed = 0.0f;
		MovementComponent->MaxFlySpeed = 0.0f;
	}
}
