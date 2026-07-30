// Copyright Epic Games, Inc. All Rights Reserved.

#include "Shooter/ShooterHealthComponent.h"
#include "Shooter/ShooterCharacter.h"
#include "OutlierNetUtils.h"
#include "OutlierGameMode.h"

UShooterHealthComponent::UShooterHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UShooterHealthComponent::ApplyDamage(float DamageAmount)
{
	if (!GetOwner()->HasAuthority()) return;

	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	GetHit();

	if (ShooterCharacter->bIsDead || DamageAmount <= 0.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s %s ApplyDamageInternal blocked Dead=%d Damage=%.1f"), OutlierNet::GetNetPrefix(ShooterCharacter), *ShooterCharacter->GetName(), ShooterCharacter->bIsDead ? 1 : 0, DamageAmount);
		return;
	}

	float RemainingDamage = DamageAmount;

	if (ShooterCharacter->CurPartnerShield > 0.0f)
	{
		const float AbsorbedDamage = FMath::Min(ShooterCharacter->CurPartnerShield, RemainingDamage);
		ShooterCharacter->CurPartnerShield -= AbsorbedDamage;
		RemainingDamage -= AbsorbedDamage;
		ShooterCharacter->BroadcastPartnerShieldState();
	}

	if (RemainingDamage > 0.0f && ShooterCharacter->CurShield > 0.0f)
	{
		const float AbsorbedDamage = FMath::Min(ShooterCharacter->CurShield, RemainingDamage);
		ShooterCharacter->CurShield -= AbsorbedDamage;
		RemainingDamage -= AbsorbedDamage;
		ShooterCharacter->OnRep_CurShield();
	}

	if (RemainingDamage > 0.0f && ShooterCharacter->CurShield <= 0.0f)
	{
		ShooterCharacter->CurHP = FMath::Clamp(ShooterCharacter->CurHP - RemainingDamage, 0.0f, ShooterCharacter->MaxHP);
		ShooterCharacter->OnRep_CurHP();
	}

	if (ShooterCharacter->CurHP <= 0.0f)
	{
		Die();
	}
}

void UShooterHealthComponent::Die()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter || ShooterCharacter->bIsDead)
	{
		return;
	}

	ShooterCharacter->bIsDead = true;
	ShooterCharacter->HandleDeath();

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
		ShooterCharacter->CurShield >= ShooterCharacter->MaxShield)
	{
		return;
	}

	RecoveryAccumulated += DeltaTime;

	if (RecoveryAccumulated < ShieldRecoveryInterval)
	{
		return;
	}

	RecoveryAccumulated = 0.0f;
	ShooterCharacter->CurShield = FMath::Min(
		ShooterCharacter->MaxShield,
		ShooterCharacter->CurShield + ShieldRecoveryValue);

	ShooterCharacter->OnRep_CurShield();
}
