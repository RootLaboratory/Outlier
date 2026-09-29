// Fill out your copyright notice in the Description page of Project Settings.

#include "ShooterStatusBarWidget.h"

#include "AmmoUI.h"
#include "ShooterCurrentAbilityIcon.h"
#include "ShooterCurrentWeaponIcon.h"

void UShooterStatusBarWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[ShooterHUD][StatusBar] Construct Widget=%s Class=%s Ammo=%s Ability=%s Weapon=%s"),
		*GetNameSafe(this),
		*GetNameSafe(GetClass()),
		*GetNameSafe(AmmoUI),
		*GetNameSafe(CurrentAbilityUI),
		*GetNameSafe(CurrentWeaponUI));
}
