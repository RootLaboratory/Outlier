#include "Interaction/InteractionStatMachine.h"

#include "Drone/Partner/HackableComponent.h"
#include "Drone/Partner/HackGameplayTags.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Interaction/InteractableComponent.h"
#include "OutlierPlayerState.h"
#include "Room/RoomTagComponent.h"
#include "Shooter/ShooterCharacter.h"
#include "UI/StatAllocatorWidget.h"
#include "UI/UILayerGameplayTags.h"
#include "UI/UILayerTypes.h"

namespace InteractionStatMachineTags
{
	static FGameplayTag Locked()
	{
		return OutlierGameplayTags::State::Locked();
	}

	static FGameplayTag Concurrent()
	{
		return FGameplayTag::RequestGameplayTag(FName(TEXT("Interact.Session.Concurrent")), false);
	}

}

AInteractionStatMachine::AInteractionStatMachine()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	InteractableComponent = CreateDefaultSubobject<UInteractableComponent>(TEXT("InteractableComponent"));
	HackableComponent = CreateDefaultSubobject<UHackableComponent>(TEXT("HackableComponent"));
	RoomTagComponent = CreateDefaultSubobject<URoomTagComponent>(TEXT("RoomTagComponent"));
}

UInteractableComponent* AInteractionStatMachine::GetInteractableComponent() const
{
	return InteractableComponent;
}

bool AInteractionStatMachine::Interact(AFirstPersonCharacter* Interactor)
{
	if (!HasAuthority()
		|| !Interactor
		|| !InteractableComponent
		|| !StatAllocatorWidgetClass
		|| IsInteractionBlocked())
	{
		return false;
	}

	if (!InteractableComponent->CanInteract(Interactor->GetOwnedGameplayTagsForQuery()))
	{
		return false;
	}

	AShooterCharacter* ShooterCharacter = nullptr;
	APartnerCharacter* PartnerCharacter = nullptr;
	ResolvePairCharacters(Interactor, ShooterCharacter, PartnerCharacter);
	AOutlierPlayerState* ShooterPlayerState = ShooterCharacter
		? ShooterCharacter->GetPlayerState<AOutlierPlayerState>()
		: nullptr;
	AOutlierPlayerState* PartnerPlayerState = PartnerCharacter
		? PartnerCharacter->GetPlayerState<AOutlierPlayerState>()
		: nullptr;
	AOutlierPlayerState* HackingPartnerPlayerState = CachedHackingPartnerPlayerState.Get();

	if (!ShooterCharacter
		|| !PartnerCharacter
		|| !ShooterPlayerState
		|| !PartnerPlayerState
		|| !HackingPartnerPlayerState
		|| Interactor != ShooterCharacter
		|| PartnerPlayerState != HackingPartnerPlayerState
		|| CachedHackingPairId == INDEX_NONE
		|| ShooterPlayerState->GetPairId() != CachedHackingPairId
		|| PartnerPlayerState->GetPairId() != CachedHackingPairId)
	{
		UE_LOG(LogTemp, Warning, TEXT("[StatMachine] Unsupported interactor Actor=%s Interactor=%s"),
			*GetName(), *GetNameSafe(Interactor));
		return false;
	}

	const FGameplayTag MachineRoomTag = GetCurrentRoomTag();
	const FGameplayTag PartnerRoomTag = PartnerCharacter->GetCurrentRoomTag();
	if (!MachineRoomTag.IsValid() || PartnerRoomTag != MachineRoomTag)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[StatMachine] Partner room mismatch Actor=%s Partner=%s MachineRoom=%s PartnerRoom=%s"),
			*GetName(),
			*GetNameSafe(PartnerCharacter),
			*MachineRoomTag.ToString(),
			*PartnerRoomTag.ToString());
		return false;
	}

	AFirstPersonPlayerController* ShooterController =
		Cast<AFirstPersonPlayerController>(ShooterCharacter->GetController());

	AFirstPersonPlayerController* PartnerController =
		Cast<AFirstPersonPlayerController>(PartnerCharacter->GetController());
	if (!ShooterController || !PartnerController)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[StatMachine] Pair controller is missing Actor=%s ShooterController=%s PartnerController=%s"),
			*GetName(),
			*GetNameSafe(ShooterController),
			*GetNameSafe(PartnerController));
		return false;
	}

	if (ShooterPlayerState)
	{
		ShooterPlayerState->SetStatAllocatorExitPending(false);
	}

	if (PartnerPlayerState)
	{
		PartnerPlayerState->SetStatAllocatorExitPending(false);
	}

	FUILayerPushRequest PushRequest;
	PushRequest.WidgetClass = StatAllocatorWidgetClass;
	PushRequest.LayerTag = UILayerTags::GameMenu();
	PushRequest.InputModeTag = FirstPersonInputModeTags::UI();
	PushRequest.RequestOwner = this;
	PushRequest.ContextActors = {ShooterCharacter, PartnerCharacter};
	PushRequest.FocusTarget = EUILayerFocusTarget::Widget;
	PushRequest.bShowCursor = true;

	ShooterController->ClientPushUILayer(PushRequest);
	PartnerController->ClientPushUILayer(PushRequest);
	return true;
}

UHackableComponent* AInteractionStatMachine::GetHackableComponent() const
{
	return HackableComponent;
}

void AInteractionStatMachine::HandleHackEffect(
	FGameplayTag EffectTag,
	const FHackResultContext& Context)
{
	if (!HasAuthority()
		|| Context.Result != EHackResult::Success
		|| EffectTag != HackGameplayTags::Effect::Unblock())
	{
		return;
	}

	ApplyUnblockEffect(Context);
}

FGameplayTag AInteractionStatMachine::GetCurrentRoomTag() const
{
	return RoomTagComponent ? RoomTagComponent->GetCurrentRoomTag() : FGameplayTag();
}

FGameplayTag AInteractionStatMachine::GetDefaultRoomTag() const
{
	return RoomTagComponent ? RoomTagComponent->GetDefaultRoomTag() : FGameplayTag();
}

URoomTagComponent* AInteractionStatMachine::GetRoomTagComp() const
{
	return RoomTagComponent;
}

bool AInteractionStatMachine::IsInteractionBlocked() const
{
	const FGameplayTag LockedTag = InteractionStatMachineTags::Locked();
	return InteractableComponent
		&& LockedTag.IsValid()
		&& InteractableComponent->InteractableTags.HasTagExact(LockedTag);
}

void AInteractionStatMachine::ResolvePairCharacters(
	AFirstPersonCharacter* Interactor,
	AShooterCharacter*& OutShooterCharacter,
	APartnerCharacter*& OutPartnerCharacter) const
{
	OutShooterCharacter = Cast<AShooterCharacter>(Interactor);
	OutPartnerCharacter = Cast<APartnerCharacter>(Interactor);

	const AOutlierPlayerState* PlayerState =
		Interactor ? Interactor->GetPlayerState<AOutlierPlayerState>() : nullptr;
	if (!PlayerState)
	{
		return;
	}

	if (!OutShooterCharacter)
	{
		OutShooterCharacter = PlayerState->GetShooterCharacter();
	}

	if (!OutPartnerCharacter)
	{
		OutPartnerCharacter = PlayerState->GetPartnerCharacter();
	}
}

void AInteractionStatMachine::ApplyUnblockEffect(const FHackResultContext& Context)
{
	if (!InteractableComponent || !IsInteractionBlocked())
	{
		return;
	}

	APartnerCharacter* HackingPartner = Cast<APartnerCharacter>(Context.InstigatorActor);
	if (!IsValid(HackingPartner))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[StatMachine] Unblock rejected: hacking partner is invalid Actor=%s Instigator=%s"),
			*GetName(),
			*GetNameSafe(Context.InstigatorActor));
		return;
	}

	AOutlierPlayerState* HackingPartnerPlayerState =
		HackingPartner->GetPlayerState<AOutlierPlayerState>();
	if (!HackingPartnerPlayerState || HackingPartnerPlayerState->GetPairId() == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[StatMachine] Unblock rejected: hacking partner PlayerState invalid Actor=%s Partner=%s PlayerState=%s PairId=%d"),
			*GetName(),
			*GetNameSafe(HackingPartner),
			*GetNameSafe(HackingPartnerPlayerState),
			HackingPartnerPlayerState ? HackingPartnerPlayerState->GetPairId() : INDEX_NONE);
		return;
	}

	CachedHackingPartnerPlayerState = HackingPartnerPlayerState;
	CachedHackingPairId = HackingPartnerPlayerState->GetPairId();

	const FGameplayTag LockedTag = InteractionStatMachineTags::Locked();
	const FGameplayTag ConcurrentTag = InteractionStatMachineTags::Concurrent();
	const FGameplayTag ShooterRoleTag = OutlierGameplayTags::Actor::Role::Shooter();
	
	InteractableComponent->InteractableTags.RemoveTag(LockedTag);

	if (ShooterRoleTag.IsValid())
	{
		InteractableComponent->InteractableTags.AddTag(ShooterRoleTag);
	}

	if (HackableComponent)
	{
		HackableComponent->MarkAsHackedOnce();
	}

}
