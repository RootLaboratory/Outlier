#include "Interaction/InteractableSwitch.h"

#include "Audio/OutlierAudioSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "Interaction/InteractableComponent.h"
#include "Interaction/InteractableDoor.h"
#include "Net/UnrealNetwork.h"
#include "Save/OutlierSaveSubSystem.h"

AInteractableSwitch::AInteractableSwitch()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SwitchRoot"));

	SwitchMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SwitchMesh"));
	SwitchMesh->SetupAttachment(RootComponent);

	InteractableComponent = CreateDefaultSubobject<UInteractableComponent>(TEXT("InteractableComponent"));
}

UInteractableComponent* AInteractableSwitch::GetInteractableComponent() const
{
	return InteractableComponent;
}

void AInteractableSwitch::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
	{
		return;
	}
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

void AInteractableSwitch::EndPlay(const EEndPlayReason::Type EndPlayReason)
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

bool AInteractableSwitch::Interact(AFirstPersonCharacter* Interactor)
{
	if (!HasAuthority() || !Interactor || !InteractableComponent)
	{
		return false;
	}

	const FGameplayTagContainer InteractorTags = Interactor->GetOwnedGameplayTagsForQuery();
	if (!InteractableComponent->CanInteract(InteractorTags))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Switch] Interact blocked by tags"));
		return false;
	}

	if (!TargetDoor)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Switch] TargetDoor is null Actor=%s"), *GetName());
		return false;
	}

	const bool bRequestedOpen = bCanToggleDoor ? !TargetDoor->IsDoorOpen() : true;
	if (!TargetDoor->TrySetDoorOpen(bRequestedOpen))
	{
		// 문이 거절한 요청은 스위치 활성화/저장/성공 연출로 이어지지 않는다.
		// 호출자의 기존 실패 경로가 Hold 상태를 정리하므로 별도 안내는 추가하지 않는다.
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

void AInteractableSwitch::OnRep_IsActivated()
{
	if (bIsActivated)
	{
		ApplySwitchActivated(nullptr);
	}
}

void AInteractableSwitch::Multicast_OnSwitchActivated_Implementation(
	AFirstPersonCharacter* Interactor)
{
	ApplySwitchActivated(Interactor);
}

void AInteractableSwitch::ApplySwitchActivated(AFirstPersonCharacter* Interactor)
{
	// ���� Ȱ��ȭ�� ���� Multicast��, �����/���� ������ RepNotify�� ���´�.
	// ���� ���� Ŭ���̾�Ʈ�� �����ص� BP ǥ�� �̺�Ʈ�� �� ���� �����Ѵ�.
	if (bActivationEventApplied)
	{
		return;
	}

	bActivationEventApplied = true;
	OnSwitchActivated(Interactor);
}

void AInteractableSwitch::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AInteractableSwitch, bIsActivated);
}
