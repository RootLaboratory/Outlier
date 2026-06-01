// Copyright Epic Games, Inc. All Rights Reserved.

#include "Shooter/ShooterHealthComponent.h"
#include "Shooter/ShooterCharacter.h"
#include "OutlierNetUtils.h"
#include "OutlierGameMode.h"

UShooterHealthComponent::UShooterHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UShooterHealthComponent::ApplyDamage(float DamageAmount)
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

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
}
