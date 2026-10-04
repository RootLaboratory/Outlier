#include "Interaction/InteractableSwitchBase.h"

#include "Audio/OutlierAudioSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Drone/Partner/HackableComponent.h"
#include "Drone/Partner/HackGameplayTags.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Interaction/InteractableComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Save/OutlierSaveSubSystem.h"

AInteractableSwitchBase::AInteractableSwitchBase()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SwitchRoot"));

	SwitchMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SwitchMesh"));
	SwitchMesh->SetupAttachment(RootComponent);

	InteractableComponent = CreateDefaultSubobject<UInteractableComponent>(TEXT("InteractableComponent"));
	HackableComponent = CreateDefaultSubobject<UHackableComponent>(TEXT("HackableComponent"));
}

UInteractableComponent* AInteractableSwitchBase::GetInteractableComponent() const
{
	return InteractableComponent;
}

UHackableComponent* AInteractableSwitchBase::GetHackableComponent() const
{
	return HackableComponent;
}

void AInteractableSwitchBase::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		if (UOutlierSaveSubSystem* SaveSubsystem = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
			: nullptr)
		{
			bProgressIdRegistered = SaveSubsystem->RegisterWorldProgressId(
				EOutlierWorldProgressType::ActivatedSwitch,
				SwitchId,
				this);
			if (bProgressIdRegistered
				&& SaveSubsystem->HasWorldProgress(EOutlierWorldProgressType::ActivatedSwitch, SwitchId))
			{
				bIsActivated = true;
				InteractableComponent->RestoreUsedState(true);
				ApplySwitchActivated(nullptr);
				ForceNetUpdate();
			}
		}
	}

	// Component BeginPlay의 복원 통보가 먼저 끝났을 수 있다. 구독 후 현재 태그도 읽어 잠금/외형을 맞춘다.
	if (HackableComponent)
	{
		HackableComponent->OnCheckpointHackStateRestored.AddUniqueDynamic(
			this,
			&AInteractableSwitchBase::HandleCheckpointHackStateRestored);
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

void AInteractableSwitchBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bProgressIdRegistered)
	{
		if (UOutlierSaveSubSystem* SaveSubsystem = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
			: nullptr)
		{
			SaveSubsystem->UnregisterWorldProgressId(
				EOutlierWorldProgressType::ActivatedSwitch,
				SwitchId,
				this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

bool AInteractableSwitchBase::Interact(AFirstPersonCharacter* Interactor)
{
	if (!HasAuthority() || !Interactor || !InteractableComponent)
	{
		return false;
	}

	if (IsInteractionBlocked())
	{
		return false;
	}

	const FGameplayTagContainer InteractorTags = Interactor->GetOwnedGameplayTagsForQuery();
	if (!InteractableComponent->CanInteract(InteractorTags))
	{
		return false;
	}

	if (!ActivateTarget())
	{
		return false;
	}

	bIsActivated = true;
	if (bProgressIdRegistered)
	{
		if (UOutlierSaveSubSystem* SaveSubsystem = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
			: nullptr)
		{
			SaveSubsystem->SetWorldProgressState(
				EOutlierWorldProgressType::ActivatedSwitch,
				SwitchId,
				true);
		}
	}

	UOutlierAudioSubsystem::PlayTaggedAtLocationFromServer(
		this,
		FGameplayTag::RequestGameplayTag(TEXT("Audio.Type.Interactable")),
		FGameplayTag::RequestGameplayTag(TEXT("Audio.Context.Object.Door.HandRecognition")));

	Multicast_OnSwitchActivated(Interactor);
	ForceNetUpdate();
	return true;
}

bool AInteractableSwitchBase::IsInteractionBlocked() const
{
	const FGameplayTag LockedTag = OutlierGameplayTags::State::Locked();
	return InteractableComponent
		&& LockedTag.IsValid()
		&& InteractableComponent->InteractableTags.HasTagExact(LockedTag);
}

bool AInteractableSwitchBase::IsHacked() const
{
	return HackableComponent
		&& HackableComponent->HasHackTag(OutlierGameplayTags::State::HackedOnce());
}

void AInteractableSwitchBase::HandleCheckpointHackStateRestored(bool bHacked)
{
	if (bHacked && HasAuthority() && InteractableComponent)
	{
		InteractableComponent->InteractableTags.RemoveTag(OutlierGameplayTags::State::Locked());
		ForceNetUpdate();
	}
	ApplyMaterialState(bHacked);
}

void AInteractableSwitchBase::HandleHackEffect(FGameplayTag EffectTag, const FHackResultContext& Context)
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

void AInteractableSwitchBase::OnRep_IsActivated()
{
	if (bIsActivated)
	{
		ApplySwitchActivated(nullptr);
	}
}

void AInteractableSwitchBase::Multicast_OnSwitchActivated_Implementation(
	AFirstPersonCharacter* Interactor)
{
	ApplySwitchActivated(Interactor);
}

void AInteractableSwitchBase::ApplySwitchActivated(AFirstPersonCharacter* Interactor)
{
	// 실시간 활성화는 Multicast로, 재접속/복원 상태는 RepNotify로 들어온다.
	// 같은 클라이언트에 둘 다 도착해도 BP 표시 이벤트는 한 번만 실행한다.
	if (bActivationEventApplied)
	{
		return;
	}

	bActivationEventApplied = true;
	OnSwitchActivated(Interactor);
}

void AInteractableSwitchBase::CacheSwitchMaterial()
{
	SwitchMID = nullptr;

	if (!SwitchMesh || SwitchMaterialSlot >= SwitchMesh->GetNumMaterials())
	{
		return;
	}

	SwitchMID = SwitchMesh->CreateAndSetMaterialInstanceDynamic(SwitchMaterialSlot);
}

void AInteractableSwitchBase::ApplyMaterialState(bool bHacked)
{
	// 해킹 대상이 아닌 일반 스위치는 머티리얼을 건드리지 않는다.
	if (!HackableComponent || !HackableComponent->IsHackTargetType())
	{
		return;
	}

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

void AInteractableSwitchBase::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AInteractableSwitchBase, bIsActivated);
}
