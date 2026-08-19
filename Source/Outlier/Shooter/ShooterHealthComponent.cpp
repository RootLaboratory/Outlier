// Copyright Epic Games, Inc. All Rights Reserved.

#include "Shooter/ShooterHealthComponent.h"
#include "Shooter/ShooterCharacter.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "OutlierNetUtils.h"
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
		UE_LOG(LogTemp, Warning, TEXT("%s %s ApplyDamageInternal blocked Dead=%d Damage=%.1f"), OutlierNet::GetNetPrefix(ShooterCharacter), *ShooterCharacter->GetName(), ShooterCharacter->IsDead() ? 1 : 0, DamageAmount);
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
		AbilitySystem->ApplyShieldRecoveryToSelf(ShieldRecoveryValue);
	}
}
