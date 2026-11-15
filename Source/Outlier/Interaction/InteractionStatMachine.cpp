#include "Interaction/InteractionStatMachine.h"
#include "Audio/OutlierAudioFeedback.h"
#include "Components/SceneComponent.h"
#include "Net/UnrealNetwork.h"

#include "Audio/OutlierAudioSubsystem.h"
#include "Drone/Partner/HackableComponent.h"
#include "Drone/Partner/HackGameplayTags.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerVitalityComponent.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GAS/Attributes/OutlierShieldAttributeSet.h"
#include "GAS/Attributes/OutlierVitalAttributeSet.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Interaction/InteractableComponent.h"
#include "OutlierPlayerState.h"
#include "Room/RoomTagComponent.h"
#include "Shooter/ShooterCharacter.h"
#include "UI/UpgradeMachineWidget.h"
#include "UI/UILayerGameplayTags.h"
#include "UI/UILayerTypes.h"
#include "Engine/World.h"

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
	// BP에서는 이 기준점의 상대 위치만 조절한다. 별도 UI 위치 보정은 적용하지 않는다.
	InteractionTargetPoint = CreateDefaultSubobject<USceneComponent>(TEXT("InteractionTargetPoint"));
	InteractionTargetPoint->SetupAttachment(GetRootComponent());
	InteractionTargetPoint->bEditableWhenInherited = true;
	InteractableComponent->InteractionTargetPointReference.ComponentProperty = GET_MEMBER_NAME_CHECKED(AInteractionStatMachine, InteractionTargetPoint);
	HackableComponent = CreateDefaultSubobject<UHackableComponent>(TEXT("HackableComponent"));
	HackTargetPoint = CreateDefaultSubobject<USceneComponent>(TEXT("HackTargetPoint"));
	HackTargetPoint->SetupAttachment(SceneRoot);
	HackTargetPoint->bEditableWhenInherited = true;
	HackableComponent->HackTargetPointReference.ComponentProperty = GET_MEMBER_NAME_CHECKED(AInteractionStatMachine, HackTargetPoint);
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
		|| !UpgradeMachineWidgetClass
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
	PushRequest.WidgetClass = UpgradeMachineWidgetClass;
	PushRequest.LayerTag = UILayerTags::GameMenu();
	PushRequest.InputModeTag = FirstPersonInputModeTags::UI();
	PushRequest.RequestOwner = this;
	PushRequest.ContextActors = {ShooterCharacter, PartnerCharacter};
	PushRequest.FocusTarget = EUILayerFocusTarget::Widget;
	PushRequest.bShowCursor = true;

	bIndicateAudio = false;
	OnRep_IndicateAudio();
	ForceNetUpdate();
	// 모든 사용 자격이 확정된 뒤 회복한다. UI를 다시 여는 요청은 기록에 걸려 회복만 생략된다.
	TryRecoverPair(ShooterCharacter, PartnerCharacter, CachedHackingPairId);
	ShooterController->ClientPushUILayer(PushRequest);
	PartnerController->ClientPushUILayer(PushRequest);
	UOutlierAudioSubsystem::PlayTaggedAtLocationFromServer(
		this,
		FGameplayTag::RequestGameplayTag(TEXT("Audio.Type.Interactable")),
		FGameplayTag::RequestGameplayTag(TEXT("Audio.Context.Object.SavePoint.Execute")));
	return true;
}

bool AInteractionStatMachine::TryRecoverPair(AShooterCharacter* Shooter, APartnerCharacter* Partner, int32 PairId)
{
	if (!HasAuthority() || !IsValid(Shooter) || !IsValid(Partner)
		|| PairId == INDEX_NONE || RecoveredPairIds.Contains(PairId) || Shooter->IsDead())
	{
		return false;
	}

	UOutlierAbilitySystemComponent* ShooterASC = Shooter->GetOutlierAbilitySystemComponent();
	UOutlierAbilitySystemComponent* PartnerASC = Partner->GetOutlierAbilitySystemComponent();
	UPartnerVitalityComponent* Vitality = Partner->GetPartnerVitalityComponent();
	if (!ShooterASC || !PartnerASC || !Vitality
		|| Shooter->GetMaxHealth() <= 0.0f || Shooter->GetCurHealth() <= 0.0f
		|| PartnerASC->HasMatchingGameplayTag(OutlierGameplayTags::State::Dead())
		|| PartnerASC->GetNumericAttribute(UOutlierVitalAttributeSet::GetMaxHealthAttribute()) <= 0.0f)
	{
		return false;
	}

	// 효과 제거의 동기 콜백에서도 같은 장치 보상을 다시 실행하지 못하도록 먼저 기록한다.
	RecoveredPairIds.Add(PairId);
	if (Vitality->IsRebooting())
	{
		// 정확한 Reboot 핸들만 제거하면 기존 종료 콜백이 HP·입력·감지를 복구한다.
		// 쿨타임과 별도 빙의 보호 효과는 이 경로에서 제거하지 않는다.
		if (!Vitality->RemoveRebootEffect())
		{
			RecoveredPairIds.Remove(PairId);
			return false;
		}
	}
	else
	{
		PartnerASC->RestoreHealthToMax();
	}

	ShooterASC->RestoreHealthToMax();
	const float MissingShield = ShooterASC->GetNumericAttribute(UOutlierShieldAttributeSet::GetMaxShieldAttribute())
		- ShooterASC->GetNumericAttribute(UOutlierShieldAttributeSet::GetShieldAttribute());
	if (MissingShield > 0.0f)
	{
		ShooterASC->ApplyShieldDeltaToSelf(MissingShield, EOutlierVitalityUpdatePolicy::Immediate);
	}
	return true;
}

UHackableComponent* AInteractionStatMachine::GetHackableComponent() const
{
	return HackableComponent;
}

EScanRevealCondition AInteractionStatMachine::GetScanRevealCondition() const
{
	// Hack.Target 태그가 있을 때만 해킹 대상으로 보고, 나머지는 일반 Interactable 규칙을 따른다.
	return HackableComponent && HackableComponent->IsHackTargetType()
		? EScanRevealCondition::RevealHackable
		: IInteractableInterface::GetScanRevealCondition();
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

	UOutlierAudioSubsystem::PlayTaggedAtLocationFromServer(
		this,
		FGameplayTag::RequestGameplayTag(TEXT("Audio.Type.Interactable")),
		FGameplayTag::RequestGameplayTag(TEXT("Audio.Context.Object.Hack.Active")));
}

void AInteractionStatMachine::BeginPlay()
{
	Super::BeginPlay();
	OnRep_IndicateAudio();
}
void AInteractionStatMachine::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AInteractionStatMachine, bIndicateAudio);
}
void AInteractionStatMachine::OnRep_IndicateAudio()
{
	OutlierAudioFeedback::Loop(this, TEXT("Audio.Context.Object.SavePoint.Indicate"), bIndicateAudio);
}
