// Copyright Epic Games, Inc. All Rights Reserved.

#include "Shooter/ShooterHealthComponent.h"
#include "Shooter/ShooterCharacter.h"
#include "OutlierNetUtils.h"

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

	const float PreviousHP = ShooterCharacter->CurHP;
	ShooterCharacter->CurHP = FMath::Clamp(ShooterCharacter->CurHP - DamageAmount, 0.0f, ShooterCharacter->MaxHP);
	UE_LOG(LogTemp, Log, TEXT("%s %s ApplyDamageInternal Damage=%.1f HP %.1f -> %.1f"), OutlierNet::GetNetPrefix(ShooterCharacter), *ShooterCharacter->GetName(), DamageAmount, PreviousHP, ShooterCharacter->CurHP);
	ShooterCharacter->UpdateLocalHealthUI();

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
}
