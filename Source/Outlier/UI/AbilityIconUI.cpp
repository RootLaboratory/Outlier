// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/AbilityIconUI.h"
#include "Components/Image.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

FGameplayTag UAbilityIconUI::GetAbilityTag() const
{
	return AbilityTag;
}

void UAbilityIconUI::SetAbility(FGameplayTag InAbility)
{
	AbilityTag = InAbility;
}


void UAbilityIconUI::NativeConstruct()
{
	Super::NativeConstruct();

	if (AbilityIcon)
	{
		DefaultIconBrush = AbilityIcon->GetBrush();
		AbilityIcon->SetVisibility(bAbilityUnlocked ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	if (DisabledImage && DisabledMaterial)
	{
		DisabledMID = UMaterialInstanceDynamic::Create(DisabledMaterial, this);
		DisabledImage->SetBrushFromMaterial(DisabledMID);
	}

	RefreshAbilityEnabledVisual();
}

void UAbilityIconUI::SetCoolTime(float InCoolTime)
{
	UE_LOG(LogTemp, Error, TEXT("SetCoolTime"));

	if (!AbilityIcon || !M_AbilityCoolTimeUI || !DefaultIconBrush.GetResourceObject())
	{
		return;
	}

	CoolTime = InCoolTime; // Chatacter 의 TotalCoolTime;
	AbilityIcon->SetBrushFromMaterial(M_AbilityCoolTimeUI);
	AbilityMID = AbilityIcon->GetDynamicMaterial();
	if (!AbilityMID)
	{
		return;
	}

	UObject* Resource = DefaultIconBrush.GetResourceObject();

	if (UTexture* IconTexture = Cast<UTexture>(Resource))
	{
		AbilityMID->SetTextureParameterValue(TEXT("IconTexture"), IconTexture);
		UE_LOG(LogTemp, Error, TEXT("SetTextureParameterValue"));

	}
	bCooldowning = true;
}

void UAbilityIconUI::UpdateCoolTime(float Delta)
{
	if (!bCooldowning || !AbilityMID || CoolTime <= 0.0f)
	{
		return;
	}

	AccumulatedTime += Delta;

	UE_LOG(LogTemp, Error, TEXT("UpdateCoolTime: %f") , AccumulatedTime);

	if (AccumulatedTime >= CoolTime)
	{
		CooldownDone();
		return;
	}

	float Progress = AccumulatedTime / CoolTime;
	AbilityMID->SetScalarParameterValue(TEXT("CooldownProgress"), Progress);
}

bool UAbilityIconUI::IsCooldowning() const
{
	if (!IsUnLock()) return false;

	return bCooldowning;
}

void UAbilityIconUI::CooldownDone()
{

	UE_LOG(LogTemp, Error, TEXT("CooldownDone"));

	bCooldowning = false;
	AccumulatedTime = 0.f;
	CoolTime = 0.f;
	AbilityMID = nullptr;
	if (AbilityIcon)
	{
		AbilityIcon->SetBrush(DefaultIconBrush);
	}
}

void UAbilityIconUI::AbilityUnLock()
{
	bAbilityUnlocked = true;
	if (AbilityIcon)
	{
		AbilityIcon->SetBrush(DefaultIconBrush);
		AbilityIcon->SetVisibility(ESlateVisibility::Visible);
	}
	RefreshAbilityEnabledVisual();
}

bool UAbilityIconUI::IsUnLock() const
{
	return bAbilityUnlocked;
}

bool UAbilityIconUI::IsAbilityEnabled() const
{
	return bAbilityEnabled;
}

void UAbilityIconUI::SetAbilityEnabled(bool bInAbilityEnabled)
{
	if (bAbilityEnabled == bInAbilityEnabled)
	{
		return;
	}

	bAbilityEnabled = bInAbilityEnabled;
	RefreshAbilityEnabledVisual();
}

void UAbilityIconUI::RefreshAbilityEnabledVisual()
{
	if (DisabledImage)
	{
		DisabledImage->SetVisibility(bAbilityEnabled ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}

	if (DisabledMID)
	{
		DisabledMID->SetScalarParameterValue(DisabledParameterName, bAbilityEnabled ? 1.0f : 0.0f);
	}
}
