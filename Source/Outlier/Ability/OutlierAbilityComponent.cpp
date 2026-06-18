// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ability/OutlierAbilityComponent.h"

#include "Engine/World.h"
#include "TimerManager.h"

UOutlierAbilityComponent::UOutlierAbilityComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UOutlierAbilityComponent::BeginPlay()
{
	Super::BeginPlay();

	RebuildAbilityCache();
	InitializeAbilityHandlers(); //Tag Register
}

void UOutlierAbilityComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		for (TPair<FGameplayTag, FOutlierCooldownState>& Cooldown : CooldownsByCooldownTag)
		{
			World->GetTimerManager().ClearTimer(Cooldown.Value.TimerHandle);
		}
	}

	CooldownsByCooldownTag.Reset();
	AbilityHandlers.Reset();

	Super::EndPlay(EndPlayReason);
}

void UOutlierAbilityComponent::RebuildAbilityCache()
{
	AbilityRowsByAbilityTag.Reset();
	AbilityTagsByInputTag.Reset();

	if (!AbilityDataTable)
	{
		return;
	}

	TArray<FOutlierAbilityRow*> Rows;
	AbilityDataTable->GetAllRows<FOutlierAbilityRow>(TEXT("OutlierAbilityRows"), Rows);

	for (const FOutlierAbilityRow* Row : Rows)
	{
		if (!Row || !Row->AbilityTag.IsValid())
		{
			continue;
		}

		AbilityRowsByAbilityTag.Add(Row->AbilityTag, *Row);

		if (Row->InputTag.IsValid())
		{
			AbilityTagsByInputTag.Add(Row->InputTag, Row->AbilityTag);
		}
	}
}

EOutlierAbilityResult UOutlierAbilityComponent::TryActivateAbilityByTag(FGameplayTag AbilityTag)
{
	const FOutlierAbilityRow* AbilityRow = FindAbilityRowByAbilityTag(AbilityTag);
	if (!AbilityRow)
	{
		UE_LOG(LogTemp, Error, TEXT("AbilityRow Null"));
		return EOutlierAbilityResult::InvalidAbility;
	}

	const EOutlierAbilityResult FailureReason = GetActivationFailureReason(AbilityTag);
	if (FailureReason != EOutlierAbilityResult::Success)
	{
		UE_LOG(LogTemp, Error, TEXT("AbilityRow Null"));

		return FailureReason;
	}

	const EOutlierAbilityResult ExecuteResult = ExecuteAbilityInternal(*AbilityRow);
	if (ExecuteResult != EOutlierAbilityResult::Success)
	{
		return ExecuteResult;
	}

	CommitCooldown(*AbilityRow);
	return EOutlierAbilityResult::Success;
}

bool UOutlierAbilityComponent::CanActivateAbilityByTag(FGameplayTag AbilityTag) const
{
	return GetActivationFailureReason(AbilityTag) == EOutlierAbilityResult::Success;
}

EOutlierAbilityResult UOutlierAbilityComponent::GetActivationFailureReason(FGameplayTag AbilityTag) const
{
	const FOutlierAbilityRow* AbilityRow = FindAbilityRowByAbilityTag(AbilityTag);
	if (!AbilityRow)
	{
		return EOutlierAbilityResult::InvalidAbility;
	}

	if (IsAbilityLocked(AbilityTag))
	{
		return EOutlierAbilityResult::Locked;
	}

	if (IsOnCooldown(AbilityRow->CooldownTag))
	{
		return EOutlierAbilityResult::Cooldown;
	}

	return EOutlierAbilityResult::Success;
}

bool UOutlierAbilityComponent::IsOnCooldown(FGameplayTag CooldownTag) const
{
	return CooldownTag.IsValid() && HasRuntimeTag(CooldownTag);
}

float UOutlierAbilityComponent::GetCooldownRemaining(FGameplayTag CooldownTag) const
{
	if (!CooldownTag.IsValid())
	{
		return 0.0f;
	}

	const FOutlierCooldownState* CooldownState = CooldownsByCooldownTag.Find(CooldownTag);
	const UWorld* World = GetWorld();
	if (!CooldownState || !World)
	{
		return 0.0f;
	}

	return FMath::Max(0.0f, CooldownState->EndTime - World->GetTimeSeconds());
}

bool UOutlierAbilityComponent::IsAbilityLocked(FGameplayTag AbilityTag) const
{
	if (!AbilityTag.IsValid())
	{
		return true;
	}

	if (RuntimeUnlockedAbilities.HasTagExact(AbilityTag))
	{
		return false;
	}

	if (RuntimeLockedAbilities.HasTagExact(AbilityTag))
	{
		return true;
	}

	const FOutlierAbilityRow* AbilityRow = FindAbilityRowByAbilityTag(AbilityTag);
	return AbilityRow ? AbilityRow->bDefaultLocked : true;
}

void UOutlierAbilityComponent::LockAbility(FGameplayTag AbilityTag)
{
	if (!AbilityTag.IsValid())
	{
		return;
	}

	RuntimeUnlockedAbilities.RemoveTag(AbilityTag);
	RuntimeLockedAbilities.AddTag(AbilityTag);
}

void UOutlierAbilityComponent::UnlockAbility(FGameplayTag AbilityTag)
{
	if (!AbilityTag.IsValid())
	{
		return;
	}

	RuntimeLockedAbilities.RemoveTag(AbilityTag);
	RuntimeUnlockedAbilities.AddTag(AbilityTag);
}

void UOutlierAbilityComponent::ClearAbilityLockOverride(FGameplayTag AbilityTag)
{
	if (!AbilityTag.IsValid())
	{
		return;
	}

	RuntimeLockedAbilities.RemoveTag(AbilityTag);
	RuntimeUnlockedAbilities.RemoveTag(AbilityTag);
}

bool UOutlierAbilityComponent::HasRuntimeTag(FGameplayTag Tag) const
{
	return Tag.IsValid() && RuntimeTags.HasTagExact(Tag);
}

void UOutlierAbilityComponent::AddRuntimeTag(FGameplayTag Tag)
{
	if (Tag.IsValid())
	{
		RuntimeTags.AddTag(Tag);
	}
}

void UOutlierAbilityComponent::RemoveRuntimeTag(FGameplayTag Tag)
{
	if (Tag.IsValid())
	{
		RuntimeTags.RemoveTag(Tag);
	}
}

FGameplayTag UOutlierAbilityComponent::ResolveAbilityTagFromInputTag(FGameplayTag InputTag) const
{
	if (const FGameplayTag* AbilityTag = AbilityTagsByInputTag.Find(InputTag))
	{
		return *AbilityTag;
	}

	return FGameplayTag();
}

const FOutlierAbilityRow* UOutlierAbilityComponent::FindAbilityRowByAbilityTag(FGameplayTag AbilityTag) const
{
	return AbilityRowsByAbilityTag.Find(AbilityTag);
}

const FOutlierAbilityRow* UOutlierAbilityComponent::FindAbilityRowByInputTag(FGameplayTag InputTag) const
{
	const FGameplayTag AbilityTag = ResolveAbilityTagFromInputTag(InputTag);
	return AbilityTag.IsValid() ? FindAbilityRowByAbilityTag(AbilityTag) : nullptr;
}

void UOutlierAbilityComponent::RegisterAbilityHandler(FGameplayTag AbilityTag, FOutlierAbilityExecuteDelegate Handler)
{
	if (AbilityTag.IsValid() && Handler.IsBound())
	{
		AbilityHandlers.Add(AbilityTag, MoveTemp(Handler));
	}
}

void UOutlierAbilityComponent::UnregisterAbilityHandler(FGameplayTag AbilityTag)
{
	if (AbilityTag.IsValid())
	{
		AbilityHandlers.Remove(AbilityTag);
	}
}

void UOutlierAbilityComponent::InitializeAbilityHandlers()
{
}

EOutlierAbilityResult UOutlierAbilityComponent::ExecuteAbilityInternal(const FOutlierAbilityRow& AbilityRow)
{
	FOutlierAbilityExecuteDelegate* Handler = AbilityHandlers.Find(AbilityRow.AbilityTag);
	if (!Handler || !Handler->IsBound())
	{
		return EOutlierAbilityResult::NoHandler;
	}

	return Handler->Execute(AbilityRow);
}

void UOutlierAbilityComponent::CommitCooldown(const FOutlierAbilityRow& AbilityRow)
{
	if (!AbilityRow.CooldownTag.IsValid() || AbilityRow.CooldownSeconds <= 0.0f)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FOutlierCooldownState& CooldownState = CooldownsByCooldownTag.FindOrAdd(AbilityRow.CooldownTag);
	World->GetTimerManager().ClearTimer(CooldownState.TimerHandle);

	AddRuntimeTag(AbilityRow.CooldownTag);
	CooldownState.EndTime = World->GetTimeSeconds() + AbilityRow.CooldownSeconds;

	World->GetTimerManager().SetTimer(
		CooldownState.TimerHandle,
		FTimerDelegate::CreateUObject(this, &UOutlierAbilityComponent::ClearCooldownTag, AbilityRow.CooldownTag),
		AbilityRow.CooldownSeconds,
		false
	);
}

void UOutlierAbilityComponent::ClearCooldownTag(FGameplayTag CooldownTag)
{
	RemoveRuntimeTag(CooldownTag);
	CooldownsByCooldownTag.Remove(CooldownTag);
}
