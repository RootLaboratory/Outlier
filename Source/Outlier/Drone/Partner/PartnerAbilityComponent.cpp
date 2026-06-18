// Fill out your copyright notice in the Description page of Project Settings.

#include "Drone/Partner/PartnerAbilityComponent.h"

#include "Drone/Partner/PartnerHackComponent.h"
#include "Drone/Partner/PartnerMovementComponent.h"
#include "Drone/Partner/PartnerSupportComponent.h"
#include "GameplayTagContainer.h"

UPartnerAbilityComponent::UPartnerAbilityComponent()
{
}

void UPartnerAbilityComponent::BeginPlay()
{
	Super::BeginPlay();
	RefreshPartnerComponents();

	PrimaryComponentTick.bCanEverTick = false;
}

void UPartnerAbilityComponent::InitializeAbilityHandlers()
{
	RefreshPartnerComponents();

	RegisterAbilityHandler(
		PartnerAbilityComponentTags::EMP(),
		FOutlierAbilityExecuteDelegate::CreateUObject(this, &UPartnerAbilityComponent::ExecuteEMP)
	);

	RegisterAbilityHandler(
		PartnerAbilityComponentTags::Shield(),
		FOutlierAbilityExecuteDelegate::CreateUObject(this, &UPartnerAbilityComponent::ExecuteShield)
	);

	RegisterAbilityHandler(
		PartnerAbilityComponentTags::Hacking(),
		FOutlierAbilityExecuteDelegate::CreateUObject(this, &UPartnerAbilityComponent::ExecuteHacking)
	);

	RegisterAbilityHandler(
		PartnerAbilityComponentTags::Scan(),
		FOutlierAbilityExecuteDelegate::CreateUObject(this, &UPartnerAbilityComponent::ExecuteScan)
	);
}

EOutlierAbilityResult UPartnerAbilityComponent::ExecuteEMP(const FOutlierAbilityRow&)
{
	if (!SupportComponent)
	{
		return EOutlierAbilityResult::NoHandler;
	}

	SupportComponent->TryAreaOfEffect_Server();
	return EOutlierAbilityResult::Success;
}

EOutlierAbilityResult UPartnerAbilityComponent::ExecuteShield(const FOutlierAbilityRow&)
{
	if (!SupportComponent)
	{
		return EOutlierAbilityResult::NoHandler;
	}

	SupportComponent->TryShield_Server();
	return EOutlierAbilityResult::Success;
}

EOutlierAbilityResult UPartnerAbilityComponent::ExecuteHacking(const FOutlierAbilityRow&)
{
	if (!HackComponent)
	{
		return EOutlierAbilityResult::NoHandler;
	}

	return HackComponent->TryHack()
		? EOutlierAbilityResult::Success
		: EOutlierAbilityResult::ExecutionFailed;
}

EOutlierAbilityResult UPartnerAbilityComponent::ExecuteScan(const FOutlierAbilityRow&)
{
	if (!SupportComponent)
	{
		return EOutlierAbilityResult::NoHandler;
	}

	SupportComponent->TryScan_Server();
	return EOutlierAbilityResult::Success;
}

void UPartnerAbilityComponent::RefreshPartnerComponents()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		SupportComponent = nullptr;
		HackComponent = nullptr;
		MovementComponent = nullptr;
		return;
	}

	SupportComponent = Owner->FindComponentByClass<UPartnerSupportComponent>();
	HackComponent = Owner->FindComponentByClass<UPartnerHackComponent>();
	MovementComponent = Owner->FindComponentByClass<UPartnerMovementComponent>();
}
