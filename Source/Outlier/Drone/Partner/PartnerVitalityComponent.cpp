#include "Drone/Partner/PartnerVitalityComponent.h"

#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerSurvivalDataRow.h"
#include "GAS/Data/OutlierVitalityDataRow.h"
#include "GAS/Attributes/OutlierVitalAttributeSet.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Outlier.h"

namespace
{
	template <typename RowType>
	const RowType* ResolveStrictRow(
		const FDataTableRowHandle& RowHandle,
		const TCHAR* Label,
		FString& OutError)
	{
		if (!RowHandle.DataTable)
		{
			OutError = FString::Printf(TEXT("%s DataTable is null."), Label);
			return nullptr;
		}

		if (RowHandle.RowName.IsNone())
		{
			OutError = FString::Printf(TEXT("%s row name is None."), Label);
			return nullptr;
		}

		if (RowHandle.DataTable->GetRowStruct() != RowType::StaticStruct())
		{
			OutError = FString::Printf(
				TEXT("%s DataTable '%s' has row type '%s'; expected '%s'."),
				Label,
				*GetNameSafe(RowHandle.DataTable),
				*GetNameSafe(RowHandle.DataTable->GetRowStruct()),
				*GetNameSafe(RowType::StaticStruct()));
			return nullptr;
		}

		const uint8* RowData = RowHandle.DataTable->FindRowUnchecked(RowHandle.RowName);
		if (!RowData)
		{
			OutError = FString::Printf(
				TEXT("%s row '%s' is missing from DataTable '%s'."),
				Label,
				*RowHandle.RowName.ToString(),
				*GetNameSafe(RowHandle.DataTable));
			return nullptr;
		}

		return reinterpret_cast<const RowType*>(RowData);
	}
}

UPartnerVitalityComponent::UPartnerVitalityComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UPartnerVitalityComponent::ValidateDataTableRows(
	const FDataTableRowHandle& VitalityRow,
	const FDataTableRowHandle& SurvivalRow,
	float& OutMaxHealth,
	float& OutRebootTime,
	FString& OutError)
{
	OutMaxHealth = 0.0f;
	OutRebootTime = 0.0f;
	OutError.Reset();

	const FOutlierVitalityDataRow* ResolvedVitalityRow = ResolveStrictRow<FOutlierVitalityDataRow>(
		VitalityRow,
		TEXT("Partner vitality"),
		OutError);
	if (!ResolvedVitalityRow)
	{
		return false;
	}

	if (ResolvedVitalityRow->MaxHealth <= 0.0f)
	{
		OutError = FString::Printf(
			TEXT("Partner vitality row '%s' has non-positive MaxHealth: %g."),
			*VitalityRow.RowName.ToString(),
			ResolvedVitalityRow->MaxHealth);
		return false;
	}

	const FPartnerSurvivalDataRow* ResolvedSurvivalRow = ResolveStrictRow<FPartnerSurvivalDataRow>(
		SurvivalRow,
		TEXT("Partner survival"),
		OutError);
	if (!ResolvedSurvivalRow)
	{
		return false;
	}

	if (ResolvedSurvivalRow->RebootTime <= 0.0f)
	{
		OutError = FString::Printf(
			TEXT("Partner survival row '%s' has non-positive RebootTime: %g."),
			*SurvivalRow.RowName.ToString(),
			ResolvedSurvivalRow->RebootTime);
		return false;
	}

	OutMaxHealth = ResolvedVitalityRow->MaxHealth;
	OutRebootTime = ResolvedSurvivalRow->RebootTime;
	return true;
}

bool UPartnerVitalityComponent::InitializeFromDataTables(
	const FDataTableRowHandle& VitalityRow,
	const FDataTableRowHandle& SurvivalRow)
{
	if (bInitialized)
	{
		return true;
	}

	APartnerCharacter* Partner = Cast<APartnerCharacter>(GetOwner());
	if (!Partner)
	{
		return FailInitialization(TEXT("Component owner is not an APartnerCharacter."));
	}

	if (!Partner->HasAuthority())
	{
		return false;
	}

	float MaxHealth = 0.0f;
	float RebootTime = 0.0f;
	FString ValidationError;
	if (!ValidateDataTableRows(VitalityRow, SurvivalRow, MaxHealth, RebootTime, ValidationError))
	{
		return FailInitialization(ValidationError);
	}

	UOutlierAbilitySystemComponent* AbilitySystem = Partner->GetOutlierAbilitySystemComponent();
	if (!AbilitySystem)
	{
		return FailInitialization(TEXT("Partner has no Outlier AbilitySystemComponent."));
	}

	if (!AbilitySystem->InitializeVitalityToSelf(MaxHealth))
	{
		return FailInitialization(FString::Printf(
			TEXT("GAS vitality initialization failed for MaxHealth %g."),
			MaxHealth));
	}

	ConfiguredRebootTime = RebootTime;
	bInitialized = true;
	BindObservers();
	return true;
}

void UPartnerVitalityComponent::BindObservers()
{
	if (bObserversBound || bOwnerTearingDown)
	{
		return;
	}

	APartnerCharacter* Partner = Cast<APartnerCharacter>(GetOwner());
	UOutlierAbilitySystemComponent* AbilitySystem = ResolveAbilitySystem();
	if (!Partner || !AbilitySystem)
	{
		return;
	}

	HealthChangedDelegateHandle = AbilitySystem
		->GetGameplayAttributeValueChangeDelegate(UOutlierVitalAttributeSet::GetHealthAttribute())
		.AddUObject(this, &UPartnerVitalityComponent::HandleHealthChanged);
	MaxHealthChangedDelegateHandle = AbilitySystem
		->GetGameplayAttributeValueChangeDelegate(UOutlierVitalAttributeSet::GetMaxHealthAttribute())
		.AddUObject(this, &UPartnerVitalityComponent::HandleMaxHealthChanged);
	GameplayEffectRemovedDelegateHandle = AbilitySystem
		->OnAnyGameplayEffectRemovedDelegate()
		.AddUObject(this, &UPartnerVitalityComponent::HandleGameplayEffectRemoved);
	checkf(
		HealthChangedDelegateHandle.IsValid()
			&& MaxHealthChangedDelegateHandle.IsValid()
			&& GameplayEffectRemovedDelegateHandle.IsValid(),
		TEXT("Partner vitality observers must bind on the AbilitySystemComponent."));
	bObserversBound = true;
	RefreshDamagePresentation();
}

void UPartnerVitalityComponent::UnbindObservers()
{
	if (!bObserversBound)
	{
		return;
	}

	if (UOutlierAbilitySystemComponent* AbilitySystem = ResolveAbilitySystem())
	{
		AbilitySystem
			->GetGameplayAttributeValueChangeDelegate(UOutlierVitalAttributeSet::GetHealthAttribute())
			.Remove(HealthChangedDelegateHandle);
		AbilitySystem
			->GetGameplayAttributeValueChangeDelegate(UOutlierVitalAttributeSet::GetMaxHealthAttribute())
			.Remove(MaxHealthChangedDelegateHandle);
		AbilitySystem
			->OnAnyGameplayEffectRemovedDelegate()
			.Remove(GameplayEffectRemovedDelegateHandle);
	}

	HealthChangedDelegateHandle.Reset();
	MaxHealthChangedDelegateHandle.Reset();
	GameplayEffectRemovedDelegateHandle.Reset();
	bObserversBound = false;
}

void UPartnerVitalityComponent::BeginOwnerTeardown()
{
	if (bOwnerTearingDown)
	{
		return;
	}

	bOwnerTearingDown = true;
	RemoveRebootEffect();
	SetEnemyPossessionProtection(false);
	UnbindObservers();
}

bool UPartnerVitalityComponent::IsRebooting() const
{
	const UOutlierAbilitySystemComponent* AbilitySystem = ResolveAbilitySystem();
	return RebootEffectHandle.IsValid()
		&& AbilitySystem
		&& AbilitySystem->GetActiveGameplayEffect(RebootEffectHandle)
		&& AbilitySystem->HasMatchingGameplayTag(OutlierGameplayTags::State::Rebooting());
}

bool UPartnerVitalityComponent::RemoveRebootEffect()
{
	UOutlierAbilitySystemComponent* AbilitySystem = ResolveAbilitySystem();
	if (!AbilitySystem || !RebootEffectHandle.IsValid())
	{
		return false;
	}

	return AbilitySystem->RemoveActiveEffectFromSelf(RebootEffectHandle);
}

void UPartnerVitalityComponent::SetEnemyPossessionProtection(bool bEnabled)
{
	APartnerCharacter* Partner = Cast<APartnerCharacter>(GetOwner());
	UOutlierAbilitySystemComponent* AbilitySystem = ResolveAbilitySystem();
	if (!Partner || !Partner->HasAuthority() || !AbilitySystem)
	{
		return;
	}

	if (bEnabled)
	{
		if (PossessionProtectionEffectHandle.IsValid()
			&& AbilitySystem->GetActiveGameplayEffect(PossessionProtectionEffectHandle))
		{
			return;
		}

		PossessionProtectionEffectHandle = AbilitySystem->ApplyDamageImmuneStateToSelf();
		return;
	}

	if (PossessionProtectionEffectHandle.IsValid())
	{
		AbilitySystem->RemoveActiveEffectFromSelf(PossessionProtectionEffectHandle);
		PossessionProtectionEffectHandle.Invalidate();
	}
}

void UPartnerVitalityComponent::HandleHealthChanged(const FOnAttributeChangeData& ChangeData)
{
	RefreshDamagePresentation();

	APartnerCharacter* Partner = Cast<APartnerCharacter>(GetOwner());
	if (bOwnerTearingDown
		|| !Partner
		|| !Partner->HasAuthority()
		|| ChangeData.OldValue <= 0.0f
		|| ChangeData.NewValue > 0.0f
		|| IsRebooting())
	{
		return;
	}

	EnterReboot();
}

void UPartnerVitalityComponent::HandleMaxHealthChanged(const FOnAttributeChangeData& ChangeData)
{
	(void)ChangeData;
	RefreshDamagePresentation();
}

void UPartnerVitalityComponent::HandleGameplayEffectRemoved(const FActiveGameplayEffect& RemovedEffect)
{
	if (!RebootEffectHandle.IsValid()
		|| RemovedEffect.Handle != RebootEffectHandle)
	{
		return;
	}

	RebootEffectHandle.Invalidate();
	CompleteReboot();
}

void UPartnerVitalityComponent::EnterReboot()
{
	APartnerCharacter* Partner = Cast<APartnerCharacter>(GetOwner());
	UOutlierAbilitySystemComponent* AbilitySystem = ResolveAbilitySystem();
	if (!Partner || !Partner->HasAuthority() || !AbilitySystem || bOwnerTearingDown || IsRebooting())
	{
		return;
	}

	RebootEffectHandle = AbilitySystem->ApplyRebootStateToSelf(ConfiguredRebootTime);
	if (!RebootEffectHandle.IsValid())
	{
		return;
	}

	AbilitySystem->CancelAllAbilities();
	FGameplayTagContainer DebuffTags;
	DebuffTags.AddTag(OutlierGameplayTags::Effect::Debuff());
	AbilitySystem->RemoveActiveEffects(FGameplayEffectQuery::MakeQuery_MatchAnyEffectTags(DebuffTags));
	Partner->StopActionsForReboot();
	Partner->RefreshEnemyDetectionForVitality();
}

void UPartnerVitalityComponent::CompleteReboot()
{
	APartnerCharacter* Partner = Cast<APartnerCharacter>(GetOwner());
	UOutlierAbilitySystemComponent* AbilitySystem = ResolveAbilitySystem();
	if (!Partner || !Partner->HasAuthority() || !AbilitySystem || bOwnerTearingDown)
	{
		return;
	}

	AbilitySystem->RestoreHealthToMax();
	Partner->RefreshEnemyDetectionForVitality();
}

void UPartnerVitalityComponent::RefreshDamagePresentation() const
{
	const APartnerCharacter* Partner = Cast<APartnerCharacter>(GetOwner());
	const UOutlierAbilitySystemComponent* AbilitySystem = ResolveAbilitySystem();
	if (!Partner || !Partner->IsLocallyControlled() || !AbilitySystem)
	{
		return;
	}

	const float MaxHealth = AbilitySystem->GetNumericAttribute(UOutlierVitalAttributeSet::GetMaxHealthAttribute());
	const float Health = AbilitySystem->GetNumericAttribute(UOutlierVitalAttributeSet::GetHealthAttribute());
	if (MaxHealth <= 0.0f || Health >= MaxHealth)
	{
		Partner->NullifyDamagedEvenet();
		return;
	}

	const float RemainingHealthRatio = FMath::Clamp(Health / MaxHealth, 0.0f, 1.0f);
	Partner->ApplyDamagedEvent(RemainingHealthRatio);
}

UOutlierAbilitySystemComponent* UPartnerVitalityComponent::ResolveAbilitySystem() const
{
	const APartnerCharacter* Partner = Cast<APartnerCharacter>(GetOwner());
	return Partner ? Partner->GetOutlierAbilitySystemComponent() : nullptr;
}

bool UPartnerVitalityComponent::FailInitialization(const FString& Error) const
{
#if UE_BUILD_SHIPPING
	UE_LOG(
		LogOutlier,
		Error,
		TEXT("[PartnerVitality] Initialization rejected for '%s': %s"),
		*GetNameSafe(GetOwner()),
		*Error);
#else
	checkf(
		false,
		TEXT("[PartnerVitality] Initialization rejected for '%s': %s"),
		*GetNameSafe(GetOwner()),
		*Error);
#endif
	return false;
}
