// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/ShooterAbilitySectionUI.h"

const EShooterAbility& UShooterAbilitySectionUI::GetAbility()
{
	return BindAbility;
}

void UShooterAbilitySectionUI::SetAbility(EShooterAbility InAbility)
{
	BindAbility = InAbility;
}

void UShooterAbilitySectionUI::AbilityUnLock()
{
	bAbilityUnlocked = true;
}

bool UShooterAbilitySectionUI::IsUnLock()
{
	return bAbilityUnlocked;
}
void UShooterAbilitySectionUI::SetHovered(bool bInHovered)
{
	bHovered = bInHovered;
	OnVisualStateChanged();
}

