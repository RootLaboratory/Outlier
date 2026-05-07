// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/ShooterAbilitySectionUI.h"
#include "Components/Image.h"

const EShooterAbility& UShooterAbilitySectionUI::GetAbility()
{
	return BindAbility;
}

void UShooterAbilitySectionUI::SetAbility(EShooterAbility InAbility)
{
	BindAbility = InAbility;
}

void UShooterAbilitySectionUI::NativeConstruct()
{
	DefaultIconBrush = AbilityIcon->GetBrush();
	AbilityIcon->SetVisibility(bAbilityUnlocked ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

	

}

void UShooterAbilitySectionUI::SetCoolTime(float InCoolTime)
{
	CoolTime = InCoolTime;
	AbilityIcon->SetBrushFromMaterial(M_ShooterAbilityCoolTimeUI);

	ShooterAbilityMID = AbilityIcon->GetDynamicMaterial();
	ShooterAbilityMID->SetScalarParameterValue(TEXT("Time"), 1);
	ShooterAbilityMID->SetScalarParameterValue(TEXT("TotalTime"), CoolTime);

}

void UShooterAbilitySectionUI::AbilityUnLock()
{
	bAbilityUnlocked = true;
	AbilityIcon->SetBrush(DefaultIconBrush);
	AbilityIcon->SetVisibility(ESlateVisibility::Visible);
}

bool UShooterAbilitySectionUI::IsUnLock()
{
	return bAbilityUnlocked;
}


