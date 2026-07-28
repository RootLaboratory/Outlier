#include "Enemy/VECDroneAnimInstance.h"

#include "Components/SkeletalMeshComponent.h"
#include "Enemy/VECDrone.h"
#include "OutlierNetUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogVECAnimation, Log, All);

namespace
{
	const TCHAR* GetExpectedStateName(EEnemyAttackPhase Phase)
	{
		switch (Phase)
		{
		case EEnemyAttackPhase::Idle:
			return TEXT("Idle");
		case EEnemyAttackPhase::Telegraph:
			return TEXT("Gather");
		case EEnemyAttackPhase::Firing:
			return TEXT("Firing");
		case EEnemyAttackPhase::Recover:
			return TEXT("Return");
		default:
			return TEXT("Unknown");
		}
	}
}

void UVECDroneAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	CacheOwningVECDrone();
	RefreshAnimationState();
	LogAnimationConfiguration();
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

	if (!bHasLoggedAttackPhase || LastLoggedAttackPhase != AttackPhase)
	{
		UE_LOG(
			LogVECAnimation,
			Log,
			TEXT("[VECAnimDebug][ObservedPhase] %s Actor=%s View=%s AnimInstance=%s Phase=%s Idle=%d Gather=%d Firing=%d Recover=%d"),
			OutlierNet::GetNetPrefix(CachedVECDrone),
			*GetNameSafe(CachedVECDrone),
			GetPresentationName(),
			*GetName(),
			*UEnum::GetValueAsString(AttackPhase),
			bIsAttackIdle ? 1 : 0,
			bIsGathering ? 1 : 0,
			bIsFiring ? 1 : 0,
			bIsRecovering ? 1 : 0);

		LastLoggedAttackPhase = AttackPhase;
		bHasLoggedAttackPhase = true;
		ConsecutiveMismatchFrames = 0;
		bHasLoggedMismatch = false;
	}
}

void UVECDroneAnimInstance::NativePostEvaluateAnimation()
{
	Super::NativePostEvaluateAnimation();

	const FName GraphState = GetCurrentStateName(0);
	if (!bHasLoggedGraphState || LastLoggedGraphState != GraphState)
	{
		UE_LOG(
			LogVECAnimation,
			Log,
			TEXT("[VECAnimDebug][GraphState] %s Actor=%s View=%s AnimInstance=%s Phase=%s State=%s"),
			OutlierNet::GetNetPrefix(CachedVECDrone),
			*GetNameSafe(CachedVECDrone),
			GetPresentationName(),
			*GetName(),
			*UEnum::GetValueAsString(AttackPhase),
			*GraphState.ToString());

		LastLoggedGraphState = GraphState;
		bHasLoggedGraphState = true;
	}

	const FName ExpectedState(GetExpectedStateName(AttackPhase));
	if (GraphState == ExpectedState)
	{
		ConsecutiveMismatchFrames = 0;
		bHasLoggedMismatch = false;
		return;
	}

	++ConsecutiveMismatchFrames;
	if (ConsecutiveMismatchFrames >= 2 && !bHasLoggedMismatch)
	{
		const USkeletalMeshComponent* MeshComponent = GetSkelMeshComponent();
		UE_LOG(
			LogVECAnimation,
			Warning,
			TEXT("[VECAnimDebug][Mismatch] %s Actor=%s View=%s AnimInstance=%s AnimClass=%s Phase=%s ExpectedState=%s ActualState=%s Mesh=%s Tick=%d Pause=%d VisibilityTick=%d RecentlyRendered=%d"),
			OutlierNet::GetNetPrefix(CachedVECDrone),
			*GetNameSafe(CachedVECDrone),
			GetPresentationName(),
			*GetName(),
			*GetNameSafe(GetClass()),
			*UEnum::GetValueAsString(AttackPhase),
			GetExpectedStateName(AttackPhase),
			*GraphState.ToString(),
			*GetNameSafe(MeshComponent),
			MeshComponent && MeshComponent->IsComponentTickEnabled() ? 1 : 0,
			MeshComponent && MeshComponent->bPauseAnims ? 1 : 0,
			MeshComponent ? static_cast<int32>(MeshComponent->VisibilityBasedAnimTickOption) : INDEX_NONE,
			MeshComponent && MeshComponent->WasRecentlyRendered() ? 1 : 0);
		bHasLoggedMismatch = true;
	}
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

void UVECDroneAnimInstance::LogAnimationConfiguration() const
{
	const USkeletalMeshComponent* MeshComponent = GetSkelMeshComponent();
	UE_LOG(
		LogVECAnimation,
		Log,
		TEXT("[VECAnimDebug][Init] %s Actor=%s View=%s AnimInstance=%s AnimClass=%s Mesh=%s SkeletalMesh=%s Owner=%s Tick=%d Pause=%d VisibilityTick=%d URO=%d RecentlyRendered=%d"),
		OutlierNet::GetNetPrefix(CachedVECDrone),
		*GetNameSafe(CachedVECDrone),
		GetPresentationName(),
		*GetName(),
		*GetNameSafe(GetClass()),
		*GetNameSafe(MeshComponent),
		MeshComponent ? *GetNameSafe(MeshComponent->GetSkeletalMeshAsset()) : TEXT("None"),
		MeshComponent ? *GetNameSafe(MeshComponent->GetOwner()) : TEXT("None"),
		MeshComponent && MeshComponent->IsComponentTickEnabled() ? 1 : 0,
		MeshComponent && MeshComponent->bPauseAnims ? 1 : 0,
		MeshComponent ? static_cast<int32>(MeshComponent->VisibilityBasedAnimTickOption) : INDEX_NONE,
		MeshComponent && MeshComponent->bEnableUpdateRateOptimizations ? 1 : 0,
		MeshComponent && MeshComponent->WasRecentlyRendered() ? 1 : 0);
}

const TCHAR* UVECDroneAnimInstance::GetPresentationName() const
{
	const USkeletalMeshComponent* MeshComponent = GetSkelMeshComponent();
	if (!IsValid(CachedVECDrone) || !MeshComponent)
	{
		return TEXT("Unknown");
	}

	return MeshComponent == CachedVECDrone->GetFirstPersonMesh()
		? TEXT("FP")
		: TEXT("TP");
}
