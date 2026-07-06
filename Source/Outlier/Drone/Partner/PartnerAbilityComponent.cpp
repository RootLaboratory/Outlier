// Fill out your copyright notice in the Description page of Project Settings.

#include "Drone/Partner/PartnerAbilityComponent.h"

#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerEMPComponent.h"
#include "Drone/Partner/PartnerHackComponent.h"
#include "Drone/Partner/PartnerMovementComponent.h"
#include "Drone/Partner/PartnerSupportComponent.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "GameplayTagContainer.h"
#include "LocalPlayerUISubSystem.h"

namespace
{
	FOutlierAbilityRow MakePartnerAbilityRow(
		FGameplayTag AbilityTag,
		const TCHAR* CooldownTagName,
		const TCHAR* InputTagName,
		float CooldownSeconds,
		float RangeCm)
	{
		FOutlierAbilityRow AbilityRow;
		AbilityRow.AbilityTag = AbilityTag;
		AbilityRow.CooldownTag = FGameplayTag::RequestGameplayTag(FName(CooldownTagName));
		AbilityRow.InputTag = FGameplayTag::RequestGameplayTag(FName(InputTagName));
		AbilityRow.CooldownSeconds = CooldownSeconds;
		AbilityRow.RangeCm = RangeCm;
		AbilityRow.bDefaultLocked = false;
		return AbilityRow;
	}
}

UPartnerAbilityComponent::UPartnerAbilityComponent()
{
}

void UPartnerAbilityComponent::BeginPlay()
{
	Super::BeginPlay();
	RefreshPartnerComponents();
	RefreshCachedPartnerAbilityData();

	PrimaryComponentTick.bCanEverTick = false;
}

void UPartnerAbilityComponent::InitializeAbilityHandlers()
{
	RefreshPartnerComponents();

	RegisterAbilityHandler(
		OutlierGameplayTags::Ability::Partner::EMP(),
		FOutlierAbilityExecuteDelegate::CreateUObject(this, &UPartnerAbilityComponent::ExecuteEMP)
	);

	RegisterAbilityHandler(
		OutlierGameplayTags::Ability::Partner::Shield(),
		FOutlierAbilityExecuteDelegate::CreateUObject(this, &UPartnerAbilityComponent::ExecuteShield)
	);

	RegisterAbilityHandler(
		OutlierGameplayTags::Ability::Partner::Hacking(),
		FOutlierAbilityExecuteDelegate::CreateUObject(this, &UPartnerAbilityComponent::ExecuteHacking)
	);

	RegisterAbilityHandler(
		OutlierGameplayTags::Ability::Partner::Scan(),
		FOutlierAbilityExecuteDelegate::CreateUObject(this, &UPartnerAbilityComponent::ExecuteScan)
	);
}

EOutlierAbilityResult UPartnerAbilityComponent::GetAdditionalActivationFailureReason(const FOutlierAbilityRow& AbilityRow) const
{
	const bool bIsPartnerAbility =
		AbilityRow.AbilityTag == OutlierGameplayTags::Ability::Partner::Hacking()
		|| AbilityRow.AbilityTag == OutlierGameplayTags::Ability::Partner::EMP();

	if (!bIsPartnerAbility)
	{
		return EOutlierAbilityResult::RequestSent;
	}

	const APartnerCharacter* PartnerCharacter = Cast<APartnerCharacter>(GetOwner());
	if (!PartnerCharacter)
	{
		return EOutlierAbilityResult::ExecutionFailed;
	}

	return PartnerCharacter->CanAcceptInput()
		? EOutlierAbilityResult::RequestSent
		: EOutlierAbilityResult::Locked;
}

bool UPartnerAbilityComponent::ShouldBypassCooldownForActivation(const FOutlierAbilityRow& AbilityRow) const
{
	if (AbilityRow.AbilityTag == OutlierGameplayTags::Ability::Partner::Hacking())
	{
		return HackComponent && HackComponent->IsHackInteractionActive();
	}

	if (AbilityRow.AbilityTag == OutlierGameplayTags::Ability::Partner::EMP())
	{
		return EMPComponent && EMPComponent->IsEMPInteractionActive();
	}

	return false;
}

void UPartnerAbilityComponent::HandleAbilityCooldownCommitted(const FOutlierAbilityRow& AbilityRow, float CooldownEndTime)
{
	const APartnerCharacter* PartnerCharacter = Cast<APartnerCharacter>(GetOwner());
	if (!PartnerCharacter)
	{
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(PartnerCharacter->GetController());
	if (!PlayerController)
	{
		return;
	}

	if (PlayerController->IsLocalController())
	{
		NotifyAbilityCooldownUI(AbilityRow.AbilityTag, AbilityRow.CooldownSeconds, CooldownEndTime);
		return;
	}

	ClientNotifyAbilityCooldown(AbilityRow.AbilityTag, AbilityRow.CooldownSeconds, CooldownEndTime);
}

EOutlierAbilityResult UPartnerAbilityComponent::ExecuteEMP(const FOutlierAbilityRow&)
{
	if (!EMPComponent)
	{
		return EOutlierAbilityResult::NoHandler;
	}

	EMPComponent->TryEMP();
	return EOutlierAbilityResult::RequestSent;
}

EOutlierAbilityResult UPartnerAbilityComponent::ExecuteShield(const FOutlierAbilityRow&)
{
	if (!SupportComponent)
	{
		return EOutlierAbilityResult::NoHandler;
	}

	SupportComponent->TryShield_Server();
	return EOutlierAbilityResult::RequestSent;
}

EOutlierAbilityResult UPartnerAbilityComponent::ExecuteHacking(const FOutlierAbilityRow&)
{
	if (!HackComponent)
	{
		return EOutlierAbilityResult::NoHandler;
	}

	HackComponent->TryHack();
	return EOutlierAbilityResult::RequestSent;
}

EOutlierAbilityResult UPartnerAbilityComponent::ExecuteScan(const FOutlierAbilityRow&)
{
	if (!SupportComponent)
	{
		return EOutlierAbilityResult::NoHandler;
	}

	SupportComponent->TryScan_Server();
	return EOutlierAbilityResult::RequestSent;
}

void UPartnerAbilityComponent::RefreshPartnerComponents()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		SupportComponent = nullptr;
		HackComponent = nullptr;
		EMPComponent = nullptr;
		MovementComponent = nullptr;
		return;
	}

	SupportComponent = Owner->FindComponentByClass<UPartnerSupportComponent>();
	HackComponent = Owner->FindComponentByClass<UPartnerHackComponent>();
	EMPComponent = Owner->FindComponentByClass<UPartnerEMPComponent>();
	MovementComponent = Owner->FindComponentByClass<UPartnerMovementComponent>();
}

void UPartnerAbilityComponent::RefreshCachedPartnerAbilityData()
{
	RefreshPartnerComponents();

	const APartnerCharacter* PartnerCharacter = Cast<APartnerCharacter>(GetOwner());
	if (!PartnerCharacter)
	{
		return;
	}

	FPartnerHackAbilityData HackAbilityData;
	HackAbilityData.CandidateRange = PartnerCharacter->HackRange;
	HackAbilityData.EffectiveRange = PartnerCharacter->HackEffectiveRange;
	HackAbilityData.MiniGameTime = PartnerCharacter->HackMiniGameTime;
	HackAbilityData.FailPenaltyTime = PartnerCharacter->HackFailPenaltyTime;
	HackAbilityData.bRequireLineOfSight = PartnerCharacter->bRequireLineOfSight;

	FPartnerEMPAbilityData EMPAbilityData;
	EMPAbilityData.EMPRange = PartnerCharacter->AreaOfEffectRange;
	EMPAbilityData.MarkingTime = PartnerCharacter->EMPMarkingTime;
	EMPAbilityData.StunDuration = PartnerCharacter->EMPStunDuration;
	EMPAbilityData.MaxTargets = PartnerCharacter->EMPMaxTargets;

	CachePartnerAbilityData(
		HackAbilityData,
		PartnerCharacter->HackCooldown,
		EMPAbilityData,
		PartnerCharacter->AreaOfEffectCooldown
	);

	if (const AActor* Owner = GetOwner(); Owner && Owner->HasAuthority())
	{
		ClientSyncPartnerAbilityData(
			HackAbilityData,
			PartnerCharacter->HackCooldown,
			EMPAbilityData,
			PartnerCharacter->AreaOfEffectCooldown
		);
	}
}

void UPartnerAbilityComponent::CachePartnerAbilityData(
	const FPartnerHackAbilityData& HackAbilityData,
	float HackCooldownSeconds,
	const FPartnerEMPAbilityData& EMPAbilityData,
	float EMPCooldownSeconds)
{
	RefreshPartnerComponents();

	CacheAbilityRow(MakePartnerAbilityRow(
		OutlierGameplayTags::Ability::Partner::Hacking(),
		TEXT("Cooldown.Partner.Hacking"),
		TEXT("Input.Partner.Hacking"),
		HackCooldownSeconds,
		HackAbilityData.CandidateRange
	));

	CacheAbilityRow(MakePartnerAbilityRow(
		OutlierGameplayTags::Ability::Partner::EMP(),
		TEXT("Cooldown.Partner.EMP"),
		TEXT("Input.Partner.EMP"),
		EMPCooldownSeconds,
		EMPAbilityData.EMPRange
	));

	if (HackComponent)
	{
		HackComponent->CacheAbilityData(HackAbilityData);
	}

	if (EMPComponent)
	{
		EMPComponent->CacheAbilityData(EMPAbilityData);
	}
}

void UPartnerAbilityComponent::NotifyAbilityCooldownUI(FGameplayTag AbilityTag, float CooldownSeconds, float CooldownEndTime) const
{
	const APartnerCharacter* PartnerCharacter = Cast<APartnerCharacter>(GetOwner());
	if (!PartnerCharacter)
	{
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(PartnerCharacter->GetController());
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
	if (!LocalPlayer)
	{
		return;
	}

	float CooldownSecondsForUI = CooldownSeconds;
	if (CooldownEndTime > 0.0f)
	{
		float ServerTimeSeconds = 0.0f;
		if (const UWorld* World = GetWorld())
		{
			if (const AGameStateBase* GameState = World->GetGameState())
			{
				ServerTimeSeconds = GameState->GetServerWorldTimeSeconds();
			}
			else
			{
				ServerTimeSeconds = World->GetTimeSeconds();
			}
		}

		CooldownSecondsForUI = FMath::Clamp(CooldownEndTime - ServerTimeSeconds, 0.0f, CooldownSeconds);
	}

	if (CooldownSecondsForUI <= 0.0f)
	{
		return;
	}

	if (ULocalPlayerUISubSystem* UISubsystem = LocalPlayer->GetSubsystem<ULocalPlayerUISubSystem>())
	{
		UISubsystem->OnAbilityUsed(AbilityTag, CooldownSecondsForUI);
	}
}

void UPartnerAbilityComponent::ClientSyncPartnerAbilityData_Implementation(
	const FPartnerHackAbilityData& HackAbilityData,
	float HackCooldownSeconds,
	const FPartnerEMPAbilityData& EMPAbilityData,
	float EMPCooldownSeconds)
{
	CachePartnerAbilityData(
		HackAbilityData,
		HackCooldownSeconds,
		EMPAbilityData,
		EMPCooldownSeconds
	);
}

void UPartnerAbilityComponent::ClientNotifyAbilityCooldown_Implementation(FGameplayTag AbilityTag, float CooldownSeconds, float CooldownEndTime)
{
	NotifyAbilityCooldownUI(AbilityTag, CooldownSeconds, CooldownEndTime);
}
