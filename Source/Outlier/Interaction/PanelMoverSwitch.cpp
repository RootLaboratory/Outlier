#include "Interaction/PanelMoverSwitch.h"

#include "Drone/Partner/HackableComponent.h"
#include "Drone/Partner/HackGameplayTags.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Interaction/InteractableComponent.h"
#include "Interaction/PanelMover.h"

APanelMoverSwitch::APanelMoverSwitch()
{
	InteractableComponent->InteractableTags.AddTag(OutlierGameplayTags::Interact::Target::PannelSwitch());
	InteractableComponent->BlockedInteractorTags.AddTag(OutlierGameplayTags::Actor::Role::Partner());

	HackableComponent->HackTags.AddTag(HackGameplayTags::Target::NonPossessable());
	HackableComponent->HackTags.AddTag(HackGameplayTags::Info::PannelSwitch());
	HackableComponent->HackTags.AddTag(HackGameplayTags::MiniGame::SpinningCircle());
	HackableComponent->HackTags.AddTag(HackGameplayTags::Time::Unlimited());
	HackableComponent->SuccessEffectTags.AddTag(HackGameplayTags::Effect::Move());
}

bool APanelMoverSwitch::ActivateTarget()
{
	if (!TargetPanel)
	{
		return false;
	}

	return TargetPanel->MoveToTarget();
}
