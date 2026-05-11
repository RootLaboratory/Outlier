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
	UE_LOG(LogTemp, Error, TEXT("SetCoolTime"));

	if (!AbilityIcon || !M_ShooterAbilityCoolTimeUI || !DefaultIconBrush.GetResourceObject())
	{
		return;
	}

	CoolTime = InCoolTime; // Chatacter 의 TotalCoolTime;
	AbilityIcon->SetBrushFromMaterial(M_ShooterAbilityCoolTimeUI);
	ShooterAbilityMID = AbilityIcon->GetDynamicMaterial();

	UObject* Resource = DefaultIconBrush.GetResourceObject();

	if (UTexture* IconTexture = Cast<UTexture>(Resource))
	{
		ShooterAbilityMID->SetTextureParameterValue(TEXT("IconTexture"), IconTexture);
		UE_LOG(LogTemp, Error, TEXT("SetTextureParameterValue"));

	}
	bCooldowning = true;
}

void UShooterAbilitySectionUI::UpdateCoolTime(float delta)
{
	AccumulatedTime += delta;

	UE_LOG(LogTemp, Error, TEXT("UpdateCoolTime: %f") , AccumulatedTime);

	if (AccumulatedTime >= CoolTime)
	{
		CooldownDone();
		return;
	}

	float Progress = AccumulatedTime / CoolTime;
	ShooterAbilityMID->SetScalarParameterValue(TEXT("CooldownProgress"), Progress);
}

bool UShooterAbilitySectionUI::IsCooldowning()
{
	if (!IsUnLock()) return false;

	return bCooldowning;
}

void UShooterAbilitySectionUI::CooldownDone()
{

	UE_LOG(LogTemp, Error, TEXT("CooldownDone"));

	bCooldowning = false;
	AccumulatedTime = 0.f;
	CoolTime = 0.f;
	ShooterAbilityMID = nullptr;
	AbilityIcon->SetBrush(DefaultIconBrush);
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


