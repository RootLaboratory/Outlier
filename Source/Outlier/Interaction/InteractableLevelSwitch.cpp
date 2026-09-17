#include "Interaction/InteractableLevelSwitch.h"

#include "Components/StaticMeshComponent.h"
#include "Drone/Partner/HackableComponent.h"
#include "Drone/Partner/HackGameplayTags.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Interaction/InteractableComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

AInteractableLevelSwitch::AInteractableLevelSwitch()
{
	HackableComponent = CreateDefaultSubobject<UHackableComponent>(TEXT("HackableComponent"));
}

UHackableComponent* AInteractableLevelSwitch::GetHackableComponent() const
{
	return HackableComponent;
}

bool AInteractableLevelSwitch::Interact(AFirstPersonCharacter* Interactor)
{
	if (IsInteractionBlocked())
	{
		UE_LOG(LogTemp, Warning, TEXT("[LevelSwitch] Interact blocked: not hacked yet. Actor=%s"), *GetName());
		return false;
	}

	return Super::Interact(Interactor);
}

bool AInteractableLevelSwitch::IsInteractionBlocked() const
{
	const FGameplayTag LockedTag = OutlierGameplayTags::State::Locked();
	return InteractableComponent
		&& LockedTag.IsValid()
		&& InteractableComponent->InteractableTags.HasTagExact(LockedTag);
}

bool AInteractableLevelSwitch::IsHacked() const
{
	return HackableComponent
		&& HackableComponent->HasHackTag(OutlierGameplayTags::State::HackedOnce());
}

void AInteractableLevelSwitch::BeginPlay()
{
	Super::BeginPlay();
	if (HackableComponent)
	{
		HackableComponent->OnCheckpointHackStateRestored.AddUniqueDynamic(
			this,
			&AInteractableLevelSwitch::HandleCheckpointHackStateRestored);
	}
	if (HasAuthority() && IsHacked() && InteractableComponent)
	{
		InteractableComponent->InteractableTags.RemoveTag(OutlierGameplayTags::State::Locked());
		ForceNetUpdate();
	}

	// Sync visuals to whatever state we start in (also covers late-joining clients,
	// since HackTags/InteractableTags are already replicated by the time BeginPlay runs).
	ApplyMaterialState(IsHacked());
}

void AInteractableLevelSwitch::HandleCheckpointHackStateRestored(bool bHacked)
{
	if (bHacked && HasAuthority() && InteractableComponent)
	{
		InteractableComponent->InteractableTags.RemoveTag(OutlierGameplayTags::State::Locked());
		ForceNetUpdate();
	}
	ApplyMaterialState(bHacked);
}

void AInteractableLevelSwitch::HandleHackEffect(FGameplayTag EffectTag, const FHackResultContext& Context)
{
	if (Context.Result != EHackResult::Success
		|| EffectTag != HackGameplayTags::Effect::Unblock())
	{
		return;
	}

	// Gameplay-state change is server-authoritative only, same as AInteractionStatMachine::ApplyUnblockEffect.
	if (HasAuthority() && IsInteractionBlocked())
	{
		if (InteractableComponent)
		{
			InteractableComponent->InteractableTags.RemoveTag(OutlierGameplayTags::State::Locked());
		}

		if (HackableComponent)
		{
			HackableComponent->MarkAsHackedOnce();
		}
	}

	// HandleHackEffect itself is already invoked on every machine (server + all clients) via
	// UHackableComponent::MulticastTriggerHackEffects, so the visual update needs no extra RPC.
	ApplyMaterialState(true);
}

void AInteractableLevelSwitch::CacheSwitchMaterial()
{
	SwitchMID = nullptr;

	if (!SwitchMesh || SwitchMaterialSlot >= SwitchMesh->GetNumMaterials())
	{
		return;
	}

	SwitchMID = SwitchMesh->CreateAndSetMaterialInstanceDynamic(SwitchMaterialSlot);
}

void AInteractableLevelSwitch::ApplyMaterialState(bool bHacked)
{
	if (!SwitchMID)
	{
		CacheSwitchMaterial();
	}

	if (SwitchMID)
	{
		SwitchMID->SetScalarParameterValue(HackedScalarParamName, bHacked ? 1.0f : 0.0f);
		SwitchMID->SetVectorParameterValue(SwitchColorParamName, bHacked ? HackedColor : LockedColor);
	}

	OnHackedStateChanged(bHacked);
}
