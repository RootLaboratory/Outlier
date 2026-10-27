#include "Tutorial/Actors/Tuto_HackDoorSwitch.h"

#include "Drone/Partner/HackableComponent.h"
#include "Drone/Partner/HackGameplayTags.h"
#include "GameFramework/Pawn.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Interaction/InteractableComponent.h"
#include "OutlierPlayerState.h"

ATuto_HackDoorSwitch::ATuto_HackDoorSwitch()
{
	EventTriggers.SetNum(2);
	InteractableComponent->InteractableTags.AddTag(OutlierGameplayTags::State::Locked());
	HackableComponent->HackTags.AddTag(HackGameplayTags::Target::NonPossessable());
	HackableComponent->SuccessEffectTags.AddTag(UnblockEffectTag);
}

void ATuto_HackDoorSwitch::HandleHackEffect(FGameplayTag EffectTag, const FHackResultContext& Context)
{
	const bool bWasBlocked = IsInteractionBlocked();
	Super::HandleHackEffect(EffectTag, Context);
	if (HasAuthority() && bWasBlocked && !IsInteractionBlocked())
	{
		const APawn* InstigatorPawn = Cast<APawn>(Context.InstigatorActor);
		TriggerEvent(1, InstigatorPawn ? InstigatorPawn->GetPlayerState<AOutlierPlayerState>() : nullptr);
	}
}
