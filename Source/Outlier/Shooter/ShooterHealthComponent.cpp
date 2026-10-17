// Copyright Epic Games, Inc. All Rights Reserved.

#include "Shooter/ShooterHealthComponent.h"
#include "Shooter/ShooterCharacter.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "OutlierGameMode.h"

UShooterHealthComponent::UShooterHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

bool UShooterHealthComponent::ApplyDamage(
	float DamageAmount,
	AController* Instigator,
	AActor* DamageCauser,
	const FGameplayTag& DamageTag)
{
	if (!GetOwner()->HasAuthority()) return false;

	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return false;
	}

	GetHit();

	if (ShooterCharacter->IsDead() || DamageAmount <= 0.0f)
	{
		return false;
	}

	if (UOutlierAbilitySystemComponent* AbilitySystem = ShooterCharacter->GetOutlierAbilitySystemComponent())
	{
		return AbilitySystem->ApplyDamageToSelf(DamageAmount, Instigator, DamageCauser, DamageTag);
	}

	return false;
}

void UShooterHealthComponent::Die()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter || ShooterCharacter->IsDead())
	{
		return;
	}

	if (!ShooterCharacter->GetOutlierAbilitySystemComponent()->ApplyDeadStateToSelf())
	{
		return;
	}

	if (ShooterCharacter->HasAuthority())
	{
		if (AOutlierGameMode* GM = ShooterCharacter->GetWorld()->GetAuthGameMode<AOutlierGameMode>())
		{
			GM->HandlePlayerDeath(ShooterCharacter);
		}
	}


	HitHistoryRefresh();
}

void UShooterHealthComponent::GetHit()
{
	DelayShieldRecovery(HitInterval);
}

void UShooterHealthComponent::HitHistoryRefresh()
{
	bShieldRecoveryAbled = true;
	ShieldRecoveryDelayRemaining = 0.f;
	RecoveryAccumulated = 0.f;
}

void UShooterHealthComponent::DelayShieldRecovery(float DelaySeconds)
{
	bShieldRecoveryAbled = DelaySeconds <= 0.0f;
	ShieldRecoveryDelayRemaining = FMath::Max(DelaySeconds, 0.0f);
	RecoveryAccumulated = 0.0f;
}

void UShooterHealthComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	// 피격 후 Shield 회복 대기시간
	if (!bShieldRecoveryAbled)
	{
		ShieldRecoveryDelayRemaining = FMath::Max(ShieldRecoveryDelayRemaining - DeltaTime, 0.0f);
		if (ShieldRecoveryDelayRemaining > 0.0f)
		{
			return;
		}
		bShieldRecoveryAbled = true;
	}

	// 과충전 중에는 자연 회복을 막고, 종료 시 별도 회복 지연을 다시 시작한다.
	if (ShooterCharacter->IsWeaponOvercharged()
		|| ShooterCharacter->bSuitDisabledByPartnerBoundary ||
		ShooterCharacter->GetCurShield() >= ShooterCharacter->GetMaxShield())
	{
		return;
	}

	RecoveryAccumulated += DeltaTime;

	if (RecoveryAccumulated < ShieldRecoveryInterval)
	{
		return;
	}

	RecoveryAccumulated = 0.0f;
	if (UOutlierAbilitySystemComponent* AbilitySystem = ShooterCharacter->GetOutlierAbilitySystemComponent())
	{
		// 위 자연 회복 조건과 주기를 통과한 경우에만 반사 보너스를 합산한다.
		// 초당 수치를 기존 회복 주기로 환산하며, 반사 시작/종료가 회복 대기나 누적 시간을 바꾸지 않는다.
		const float ReflectionRecoveryBonus = ShooterCharacter->IsBulletReflecting()
			? AbilitySystem->GetShooterSuitConfig().BulletReflection.ShieldRecoveryBonusPerSecond
				* FMath::Max(ShieldRecoveryInterval, 0.0f)
			: 0.0f;

		// 기존 GAS 효과가 기본 Shield만 회복하고 MaxShield 상한을 적용한다.
		AbilitySystem->ApplyShieldRecoveryToSelf(ShieldRecoveryValue + ReflectionRecoveryBonus);
	}
}
