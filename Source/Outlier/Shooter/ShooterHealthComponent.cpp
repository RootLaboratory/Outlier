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

void UShooterHealthComponent::ApplyDamage(
	float DamageAmount,
	AController* Instigator,
	AActor* DamageCauser,
	const FGameplayTag& DamageTag)
{
	if (!GetOwner()->HasAuthority()) return;

	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	GetHit();

	if (ShooterCharacter->IsDead() || DamageAmount <= 0.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s %s ApplyDamageInternal blocked Dead=%d Damage=%.1f"), OutlierNet::GetNetPrefix(ShooterCharacter), *ShooterCharacter->GetName(), ShooterCharacter->IsDead() ? 1 : 0, DamageAmount);
		return;
	}

	if (UOutlierAbilitySystemComponent* AbilitySystem = ShooterCharacter->GetOutlierAbilitySystemComponent())
	{
		AbilitySystem->ApplyDamageToSelf(DamageAmount, Instigator, DamageCauser, DamageTag);
	}
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
	bShieldRecoveryAbled = false;
	HitAccumulated = 0.0f;
	RecoveryAccumulated = 0.0f;
}

void UShooterHealthComponent::HitHistoryRefresh()
{
	bShieldRecoveryAbled = true;
	HitAccumulated = 0.f;
	RecoveryAccumulated = 0.f;
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
		HitAccumulated += DeltaTime;

		if (HitAccumulated < HitInterval)
		{
			return;
		}

		HitAccumulated = 0.0f;
		bShieldRecoveryAbled = true;
	}

	// 경계 밖에서는 Shield 회복만 차단
	if (ShooterCharacter->bSuitDisabledByPartnerBoundary ||
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
