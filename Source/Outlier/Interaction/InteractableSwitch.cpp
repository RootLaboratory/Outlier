#include "Interaction/InteractableSwitch.h"

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

	if (bCanToggleDoor)
	{
		TargetDoor->ToggleDoor();
	}
	else
	{
		TargetDoor->SetDoorOpen(true);
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
	// 현재 활성화는 기존 Multicast로, 재생성/늦은 접속은 RepNotify로 들어온다.
	// 둘이 같은 클라이언트에 도착해도 BP 표현 이벤트는 한 번만 실행한다.
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
