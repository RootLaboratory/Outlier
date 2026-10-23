#include "Interaction/PanelMoverSwitch.h"

#include "Drone/Partner/HackableComponent.h"
#include "Drone/Partner/HackGameplayTags.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Interaction/InteractableComponent.h"
#include "Interaction/PanelMover.h"

APanelMoverSwitch::APanelMoverSwitch()
{
	InteractableComponent->InteractableTags.AddTag(OutlierGameplayTags::Interact::Target::PannelSwitch());
	InteractableComponent->InteractableTags.AddTag(OutlierGameplayTags::State::Locked());
	InteractableComponent->InteractableTags.AddTag(OutlierGameplayTags::Actor::Role::Shooter());
	InteractableComponent->BlockedInteractorTags.AddTag(OutlierGameplayTags::Actor::Role::Partner());

	HackableComponent->HackTags.AddTag(HackGameplayTags::Target::NonPossessable());
	HackableComponent->HackTags.AddTag(HackGameplayTags::Info::PannelSwitch());
	HackableComponent->HackTags.AddTag(HackGameplayTags::MiniGame::SpinningCircle());
	HackableComponent->HackTags.AddTag(HackGameplayTags::Time::Unlimited());
	HackableComponent->HackTags.AddTag(HackGameplayTags::Effect::Unblock());
	HackableComponent->HackTags.AddTag(HackGameplayTags::Use::Once());
	HackableComponent->SuccessEffectTags.AddTag(HackGameplayTags::Effect::Unblock());

	// Unblock 해킹은 잠금만 풀고, 실제 판넬 이동은 Shooter의 상호작용에서 처리한다.
}

bool APanelMoverSwitch::ActivateTarget()
{
	if (!TargetPanel)
	{
		return false;
	}

	return TargetPanel->MoveToTarget();
}
