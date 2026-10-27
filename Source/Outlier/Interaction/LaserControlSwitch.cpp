#include "Interaction/LaserControlSwitch.h"

#include "Drone/Partner/HackableComponent.h"
#include "Drone/Partner/HackGameplayTags.h"
#include "Environment/LaserKillFloor.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Interaction/InteractableComponent.h"

ALaserControlSwitch::ALaserControlSwitch()
{
	InteractableComponent->InteractableTags.AddTag(OutlierGameplayTags::Interact::Target::LaserControlSwitch());
	InteractableComponent->InteractableTags.AddTag(OutlierGameplayTags::State::Locked());
	InteractableComponent->InteractableTags.AddTag(OutlierGameplayTags::Actor::Role::Shooter());
	InteractableComponent->BlockedInteractorTags.AddTag(OutlierGameplayTags::Actor::Role::Partner());

	HackableComponent->HackTags.AddTag(HackGameplayTags::Effect::Unblock());
	HackableComponent->HackTags.AddTag(HackGameplayTags::Info::LaserControlSwitch());
	HackableComponent->HackTags.AddTag(HackGameplayTags::MiniGame::SpinningCircle());
	HackableComponent->HackTags.AddTag(HackGameplayTags::Target::NonPossessable());
	HackableComponent->HackTags.AddTag(HackGameplayTags::Time::Unlimited());
	HackableComponent->HackTags.AddTag(HackGameplayTags::Use::Once());
	HackableComponent->SuccessEffectTags.AddTag(HackGameplayTags::Effect::Unblock());

	// Unblock 해킹 성공 시 잠금만 풀고, 레이저 제거는 이후 Shooter 상호작용에서 한다.
}

void ALaserControlSwitch::BeginPlay()
{
	Super::BeginPlay();

	// 체크포인트 리로드로 레벨 배치 레이저가 다시 생겼으면, 저장된 활성화 상태에 맞춰 다시 제거한다.
	if (HasAuthority() && bIsActivated)
	{
		DestroyLaserFloors();
	}
}

bool ALaserControlSwitch::ActivateTarget()
{
	return DestroyLaserFloors();
}

bool ALaserControlSwitch::DestroyLaserFloors()
{
	if (!HasAuthority())
	{
		return false;
	}

	bool bDestroyedAny = false;
	for (ALaserKillFloor* LaserFloor : TargetLaserFloors)
	{
		if (IsValid(LaserFloor) && !LaserFloor->IsActorBeingDestroyed())
		{
			bDestroyedAny |= DeactivateLaserFloor(LaserFloor);
		}
	}
	return bDestroyedAny;
}

bool ALaserControlSwitch::DeactivateLaserFloor(ALaserKillFloor* LaserFloor)
{
	return LaserFloor->Destroy();
}
