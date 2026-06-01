// Fill out your copyright notice in the Description page of Project Settings.

#include "AbilityIconUI.h"
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
}


void UAbilityIconUI::EnsureMasterMaterial()
{
	if (AbilityMID)
	{
		return; 
	}

	if (!AbilityIcon || !M_AbilityIconMaster || !DefaultIconBrush.GetResourceObject())
	{
		return;
	}

	AbilityIcon->SetBrushFromMaterial(M_AbilityIconMaster);
	AbilityMID = AbilityIcon->GetDynamicMaterial();

	if (!AbilityMID)
	{
		return;
	}

	// 아이콘 텍스처를 마스터 머티리얼에 전달
	if (UTexture* IconTexture = Cast<UTexture>(DefaultIconBrush.GetResourceObject()))
	{
		AbilityMID->SetTextureParameterValue(TEXT("IconTexture"), IconTexture);
	}

	// 현재 상태 파라미터 동기화
	AbilityMID->SetScalarParameterValue(TEXT("CooldownProgress"), 0.0f);
	AbilityMID->SetScalarParameterValue(TEXT("LockFactor"), bAbilityEnabled ? 0.0f : 1.0f);
}

void UAbilityIconUI::TryRestoreDefaultBrush()
{
	// 쿨타임도 끝나고 잠금도 없을 때만 기본 텍스처로 복귀
	if (bCooldowning || !bAbilityEnabled)
	{
		return;
	}

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

	if (!bAbilityEnabled)
	{
		EnsureMasterMaterial();
		if (AbilityMID)
		{
			AbilityMID->SetScalarParameterValue(TEXT("LockFactor"), 1.0f);
		}
	}
	else
	{
		if (AbilityMID)
		{
			AbilityMID->SetScalarParameterValue(TEXT("LockFactor"), 0.0f);
		}
		TryRestoreDefaultBrush();
	}
}

void UAbilityIconUI::SetCoolTime(float InCoolTime)
{
	if (!AbilityIcon || !M_AbilityIconMaster || !DefaultIconBrush.GetResourceObject())
	{
		return;
	}

	CoolTime        = InCoolTime;
	AccumulatedTime = 0.0f;

	EnsureMasterMaterial();

	bCooldowning = true;
}

void UAbilityIconUI::UpdateCoolTime(float Delta)
{
	if (!bCooldowning || !AbilityMID || CoolTime <= 0.0f)
	{
		return;
	}

	AccumulatedTime += Delta;

	if (AccumulatedTime >= CoolTime)
	{
		CooldownDone();
		return;
	}

	const float Progress = AccumulatedTime / CoolTime;
	AbilityMID->SetScalarParameterValue(TEXT("CooldownProgress"), Progress);
}

bool UAbilityIconUI::IsCooldowning() const
{
	if (!IsUnLock()) return false;
	return bCooldowning;
}

void UAbilityIconUI::CooldownDone()
{
	bCooldowning    = false;
	AccumulatedTime = 0.f;
	CoolTime        = 0.f;

	if (AbilityMID)
	{
		AbilityMID->SetScalarParameterValue(TEXT("CooldownProgress"), 0.0f);
	}

	// 잠금도 없으면 기본 텍스처로 복귀
	TryRestoreDefaultBrush();
}
