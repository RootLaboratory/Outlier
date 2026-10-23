#include "Drone/Partner/PartnerShieldRecipientComponent.h"

#include "AbilitySystemGlobals.h"
#include "Drone/Partner/PartnerShieldSphere.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/World.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GAS/Attributes/OutlierShieldAttributeSet.h"
#include "GAS/Attributes/OutlierVitalAttributeSet.h"
#include "TimerManager.h"

UPartnerShieldRecipientComponent::UPartnerShieldRecipientComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UOutlierAbilitySystemComponent* UPartnerShieldRecipientComponent::GetASC() const
{
	return Cast<UOutlierAbilitySystemComponent>(UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner()));
}

bool UPartnerShieldRecipientComponent::ApplyShield(float Amount, float Duration, float HealthRegenRate)
{
	UOutlierAbilitySystemComponent* ASC = GetASC();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !ASC || !GetWorld()
		|| !FMath::IsFinite(Amount) || !FMath::IsFinite(Duration) || !FMath::IsFinite(HealthRegenRate)
		|| HealthRegenRate < 0.0f) return false;
	const UOutlierShieldAttributeSet* Set = ASC->GetSet<UOutlierShieldAttributeSet>();
	if (!Set) return false;
	const float ClampedAmount = FMath::Max(Amount, 0.0f);
	if (!ASC->ApplyPartnerShieldDeltaToSelf(ClampedAmount - Set->GetPartnerShield(),
		ClampedAmount - Set->GetMaxPartnerShield())) return false;
	ShieldDuration = Duration;
	ShieldHealthRegenRate = HealthRegenRate;
	GetWorld()->GetTimerManager().SetTimer(DecayTimer, this,
		&UPartnerShieldRecipientComponent::UpdateDecay, 1.0f / 60.0f, true);
	return true;
}

bool UPartnerShieldRecipientComponent::HasShield() const
{
	const UOutlierAbilitySystemComponent* ASC = GetASC();
	const UOutlierShieldAttributeSet* Shield = ASC ? ASC->GetSet<UOutlierShieldAttributeSet>() : nullptr;
	const UOutlierVitalAttributeSet* Vital = ASC ? ASC->GetSet<UOutlierVitalAttributeSet>() : nullptr;
	return Shield && Shield->GetPartnerShield() > 0.0f && (!Vital || Vital->GetHealth() > 0.0f);
}

void UPartnerShieldRecipientComponent::UpdateDecay()
{
	UOutlierAbilitySystemComponent* ASC = GetASC();
	if (!ASC || !GetOwner()->HasAuthority()) return;
	const UOutlierShieldAttributeSet* Set = ASC->GetSet<UOutlierShieldAttributeSet>();
	if (!Set) return;
	const UOutlierVitalAttributeSet* Vital = ASC->GetSet<UOutlierVitalAttributeSet>();
	if (Vital && Vital->GetHealth() <= 0.0f)
	{
		ClearShield();
		return;
	}
	if (ShieldDuration <= KINDA_SMALL_NUMBER || Set->GetPartnerShield() <= 0.0f)
	{
		ClearShield();
		return;
	}
	constexpr float Step = 1.0f / 60.0f;
	const float DecayPerSecond = Set->GetMaxPartnerShield() / ShieldDuration;
	// 마지막 틱도 실제로 실드가 남아 있는 시간만 회복한다.
	const float CoveredTime = DecayPerSecond > 0.0f
		? FMath::Min(Step, Set->GetPartnerShield() / DecayPerSecond) : Step;
	if (Vital && ShieldHealthRegenRate > 0.0f)
	{
		ASC->ApplyHealthRecoveryToSelf(Vital->GetMaxHealth() * ShieldHealthRegenRate * CoveredTime);
	}
	// 기존 Shooter와 같은 감소량: 최대 부여량 / 지속 시간 * 1/60.
	ASC->ApplyPartnerShieldDeltaToSelf(-DecayPerSecond * Step, 0.0f);
	if (Set->GetPartnerShield() <= 0.0f)
	{
		GetWorld()->GetTimerManager().ClearTimer(DecayTimer);
		ShieldHealthRegenRate = 0.0f;
		DestroyVisual();
	}
}

void UPartnerShieldRecipientComponent::ShowShield(TSubclassOf<APartnerShieldSphere> VisualClass, APartnerCharacter* Source)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !VisualClass || !HasShield()) return;
	DestroyVisual();
	FActorSpawnParameters Params;
	Params.Owner = GetOwner();
	Params.Instigator = Source;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ShieldVisual = GetWorld()->SpawnActor<APartnerShieldSphere>(VisualClass,
		GetOwner()->GetActorLocation(), GetOwner()->GetActorRotation(), Params);
	if (ShieldVisual)
	{
		ShieldVisual->SetRecipientLocalTransform(ShieldLocalTransform);
		ShieldVisual->InitializeShieldForActor(GetOwner(), Source);
	}
}

void UPartnerShieldRecipientComponent::DestroyVisual()
{
	if (IsValid(ShieldVisual)) ShieldVisual->EndShield();
	ShieldVisual = nullptr;
}

void UPartnerShieldRecipientComponent::ClearShield()
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	ShieldHealthRegenRate = 0.0f;
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(DecayTimer);
	if (UOutlierAbilitySystemComponent* ASC = GetASC())
	{
		if (const UOutlierShieldAttributeSet* Set = ASC->GetSet<UOutlierShieldAttributeSet>())
			ASC->ApplyPartnerShieldDeltaToSelf(-Set->GetPartnerShield(), -Set->GetMaxPartnerShield());
	}
	DestroyVisual();
}

void UPartnerShieldRecipientComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(DecayTimer);
	DestroyVisual();
	Super::EndPlay(EndPlayReason);
}
