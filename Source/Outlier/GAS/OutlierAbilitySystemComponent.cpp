#include "GAS/OutlierAbilitySystemComponent.h"

#include "GameFramework/Pawn.h"
#include "GAS/Attributes/OutlierVitalAttributeSet.h"
#include "GAS/Effects/OutlierGameplayEffects.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Outlier.h"

namespace
{
FGameplayEffectSpecHandle MakeSelfEffectSpec(
	UOutlierAbilitySystemComponent& AbilitySystem,
	TSubclassOf<UGameplayEffect> EffectClass,
	AController* Instigator,
	AActor* EffectCauser)
{
	FGameplayEffectContextHandle Context = AbilitySystem.MakeEffectContext();
	Context.AddInstigator(Instigator ? Instigator->GetPawn() : nullptr, EffectCauser);
	Context.AddSourceObject(EffectCauser);
	return AbilitySystem.MakeOutgoingSpec(EffectClass, 1.0f, Context);
}

bool ApplySpecToSelf(UOutlierAbilitySystemComponent& AbilitySystem, FGameplayEffectSpecHandle& SpecHandle)
{
	return SpecHandle.IsValid()
		&& AbilitySystem.ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get()).WasSuccessfullyApplied();
}
}

UOutlierAbilitySystemComponent::UOutlierAbilitySystemComponent()
{
	SetIsReplicatedByDefault(true);
}

void UOutlierAbilitySystemComponent::InitializeForPawn(APawn* Pawn)
{
	if (!ensure(Pawn) || GetOwner() != Pawn)
	{
		return;
	}

	if (GetOwnerActor() != Pawn || GetAvatarActor() != Pawn)
	{
		InitAbilityActorInfo(Pawn, Pawn);
	}
	else
	{
		RefreshAbilityActorInfo();
	}

	UE_LOG(
		LogOutlier,
		Verbose,
		TEXT("[GAS.Init] Actor=%s Owner=%s Avatar=%s Role=%d RepMode=%d"),
		*GetNameSafe(Pawn),
		*GetNameSafe(GetOwnerActor()),
		*GetNameSafe(GetAvatarActor()),
		static_cast<int32>(Pawn->GetLocalRole()),
		static_cast<int32>(ReplicationMode));
}

void UOutlierAbilitySystemComponent::ClearForPawn(const APawn* Pawn)
{
	if (GetOwnerActor() == Pawn || GetAvatarActor() == Pawn)
	{
		ClearActorInfo();
	}
}

bool UOutlierAbilitySystemComponent::ApplyDamageToSelf(
	float DamageAmount,
	AController* Instigator,
	AActor* DamageCauser,
	const FGameplayTag& DamageTag)
{
	if (!IsOwnerActorAuthoritative() || DamageAmount <= 0.0f
		|| HasMatchingGameplayTag(OutlierGameplayTags::State::DamageImmune()))
	{
		return false;
	}

	FGameplayEffectSpecHandle SpecHandle = MakeSelfEffectSpec(
		*this,
		UOutlierDamageGameplayEffect::StaticClass(),
		Instigator,
		DamageCauser);
	if (!SpecHandle.IsValid())
	{
		return false;
	}

	SpecHandle.Data->SetSetByCallerMagnitude(OutlierGameplayTags::Data::Damage(), DamageAmount);
	if (DamageTag.IsValid())
	{
		SpecHandle.Data->AddDynamicAssetTag(DamageTag);
	}
	return ApplySpecToSelf(*this, SpecHandle);
}

bool UOutlierAbilitySystemComponent::ApplyShieldRecoveryToSelf(float Amount)
{
	if (!IsOwnerActorAuthoritative() || Amount <= 0.0f)
	{
		return false;
	}

	FGameplayEffectSpecHandle SpecHandle = MakeSelfEffectSpec(
		*this,
		UOutlierShieldRecoveryGameplayEffect::StaticClass(),
		nullptr,
		GetAvatarActor());
	if (!SpecHandle.IsValid())
	{
		return false;
	}

	SpecHandle.Data->SetSetByCallerMagnitude(OutlierGameplayTags::Data::ShieldRecovery(), Amount);
	return ApplySpecToSelf(*this, SpecHandle);
}

bool UOutlierAbilitySystemComponent::ApplyPartnerShieldDeltaToSelf(
	float PartnerShieldDelta,
	float MaxPartnerShieldDelta)
{
	if (!IsOwnerActorAuthoritative())
	{
		return false;
	}

	FGameplayEffectSpecHandle SpecHandle = MakeSelfEffectSpec(
		*this,
		UOutlierPartnerShieldGameplayEffect::StaticClass(),
		nullptr,
		GetAvatarActor());
	if (!SpecHandle.IsValid())
	{
		return false;
	}

	SpecHandle.Data->SetSetByCallerMagnitude(
		OutlierGameplayTags::Data::PartnerShield(),
		PartnerShieldDelta);
	SpecHandle.Data->SetSetByCallerMagnitude(
		OutlierGameplayTags::Data::MaxPartnerShield(),
		MaxPartnerShieldDelta);
	return ApplySpecToSelf(*this, SpecHandle);
}

bool UOutlierAbilitySystemComponent::ApplyDeadStateToSelf()
{
	if (!IsOwnerActorAuthoritative() || HasMatchingGameplayTag(OutlierGameplayTags::State::Dead()))
	{
		return false;
	}

	FGameplayEffectSpecHandle SpecHandle = MakeSelfEffectSpec(
		*this,
		UOutlierDeadGameplayEffect::StaticClass(),
		nullptr,
		GetAvatarActor());
	return ApplySpecToSelf(*this, SpecHandle);
}

bool UOutlierAbilitySystemComponent::InitializeVitalityToSelf(float MaxHealth)
{
	if (!IsOwnerActorAuthoritative() || MaxHealth <= 0.0f)
	{
		return false;
	}

	FGameplayEffectSpecHandle SpecHandle = MakeSelfEffectSpec(
		*this,
		UOutlierVitalityInitializationGameplayEffect::StaticClass(),
		nullptr,
		GetAvatarActor());
	if (!SpecHandle.IsValid())
	{
		return false;
	}

	SpecHandle.Data->SetSetByCallerMagnitude(OutlierGameplayTags::Data::MaxHealth(), MaxHealth);
	SpecHandle.Data->SetSetByCallerMagnitude(OutlierGameplayTags::Data::Health(), MaxHealth);
	return ApplySpecToSelf(*this, SpecHandle);
}

bool UOutlierAbilitySystemComponent::RestoreHealthToMax()
{
	if (!IsOwnerActorAuthoritative())
	{
		return false;
	}

	const float MaxHealth = GetNumericAttribute(UOutlierVitalAttributeSet::GetMaxHealthAttribute());
	if (MaxHealth <= 0.0f)
	{
		return false;
	}

	FGameplayEffectSpecHandle SpecHandle = MakeSelfEffectSpec(
		*this,
		UOutlierHealthRecoveryGameplayEffect::StaticClass(),
		nullptr,
		GetAvatarActor());
	if (!SpecHandle.IsValid())
	{
		return false;
	}

	SpecHandle.Data->SetSetByCallerMagnitude(OutlierGameplayTags::Data::Health(), MaxHealth);
	return ApplySpecToSelf(*this, SpecHandle);
}

FActiveGameplayEffectHandle UOutlierAbilitySystemComponent::ApplyRebootStateToSelf(float DurationSeconds)
{
	if (!IsOwnerActorAuthoritative() || DurationSeconds <= 0.0f)
	{
		return FActiveGameplayEffectHandle();
	}

	FGameplayEffectSpecHandle SpecHandle = MakeSelfEffectSpec(
		*this,
		UOutlierRebootGameplayEffect::StaticClass(),
		nullptr,
		GetAvatarActor());
	if (!SpecHandle.IsValid())
	{
		return FActiveGameplayEffectHandle();
	}

	SpecHandle.Data->SetDuration(DurationSeconds, true);
	return ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
}

FActiveGameplayEffectHandle UOutlierAbilitySystemComponent::ApplyDamageImmuneStateToSelf()
{
	if (!IsOwnerActorAuthoritative())
	{
		return FActiveGameplayEffectHandle();
	}

	FGameplayEffectSpecHandle SpecHandle = MakeSelfEffectSpec(
		*this,
		UOutlierDamageImmuneGameplayEffect::StaticClass(),
		nullptr,
		GetAvatarActor());
	return SpecHandle.IsValid()
		? ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get())
		: FActiveGameplayEffectHandle();
}

bool UOutlierAbilitySystemComponent::RemoveActiveEffectFromSelf(FActiveGameplayEffectHandle Handle)
{
	return IsOwnerActorAuthoritative() && Handle.IsValid() && RemoveActiveGameplayEffect(Handle);
}
