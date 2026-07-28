#include "Enemy/VECDroneAnimInstance.h"

#include "Enemy/VECDrone.h"

void UVECDroneAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	CacheOwningVECDrone();
	RefreshAnimationState();
}

void UVECDroneAnimInstance::NativeUninitializeAnimation()
{
	CachedVECDrone = nullptr;
	ResetAnimationState();

	Super::NativeUninitializeAnimation();
}

void UVECDroneAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!IsValid(CachedVECDrone))
	{
		CacheOwningVECDrone();
	}

	RefreshAnimationState();
}

void UVECDroneAnimInstance::CacheOwningVECDrone()
{
	CachedVECDrone = Cast<AVECDrone>(TryGetPawnOwner());
}

void UVECDroneAnimInstance::RefreshAnimationState()
{
	if (!IsValid(CachedVECDrone))
	{
		ResetAnimationState();
		return;
	}

	AttackPhase = CachedVECDrone->GetAttackPhase();
	bIsAttackIdle = AttackPhase == EEnemyAttackPhase::Idle;
	bIsGathering = AttackPhase == EEnemyAttackPhase::Telegraph;
	bIsFiring = AttackPhase == EEnemyAttackPhase::Firing;
	bIsRecovering = AttackPhase == EEnemyAttackPhase::Recover;
	bIsPossessed = CachedVECDrone->IsEnemyPossessed();
}

void UVECDroneAnimInstance::ResetAnimationState()
{
	AttackPhase = EEnemyAttackPhase::Idle;
	bIsAttackIdle = true;
	bIsGathering = false;
	bIsFiring = false;
	bIsRecovering = false;
	bIsPossessed = false;
}
