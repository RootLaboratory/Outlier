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

	if (UTexture* IconTexture = Cast<UTexture>(DefaultIconBrush.GetResourceObject()))
	{
		AbilityMID->SetTextureParameterValue(TEXT("IconTexture"), IconTexture);
	}

	SyncMaterialState();
}

void UAbilityIconUI::SyncMaterialState()
{
	if (!AbilityMID)
	{
		return;
	}

	const float CooldownProgress = bCooldowning && CoolTime > 0.0f
		? FMath::Clamp(AccumulatedTime / CoolTime, 0.0f, 1.0f)
		: 0.0f;

	AbilityMID->SetScalarParameterValue(TEXT("CooldownProgress"), CooldownProgress);
	AbilityMID->SetScalarParameterValue(TEXT("IsCoolDown"), bCooldowning ? 1.0f : 0.0f);
	AbilityMID->SetScalarParameterValue(TEXT("LockFactor"), bAbilityEnabled ? 0.0f : 1.0f);
}

void UAbilityIconUI::TryRestoreDefaultBrush()
{
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
		if (!bCooldowning)
		{
			AbilityIcon->SetBrush(DefaultIconBrush);
		}
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
		SyncMaterialState();
		return;
	}

	SyncMaterialState();
	TryRestoreDefaultBrush();
}

void UAbilityIconUI::SetCoolTime(float InCoolTime)
{
	const UWorld* World = GetWorld();
	SetCoolTimeAt(InCoolTime, World ? World->GetTimeSeconds() : 0.0f);
}

void UAbilityIconUI::SetCoolTimeAt(float InCoolTime, float InStartTime)
{
	if (!AbilityIcon || !M_AbilityIconMaster || !DefaultIconBrush.GetResourceObject())
	{
		return;
	}

	CoolTime = InCoolTime;
	CooldownStartTime = InStartTime;

	const UWorld* World = GetWorld();
	AccumulatedTime = World
		? FMath::Max(0.0f, World->GetTimeSeconds() - CooldownStartTime)
		: 0.0f;
	bCooldowning = true;

	EnsureMasterMaterial();
	SyncMaterialState();
}

void UAbilityIconUI::UpdateCoolTime(float Delta)
{
	if (!bCooldowning || !AbilityMID || CoolTime <= 0.0f)
	{
		return;
	}

	if (const UWorld* World = GetWorld())
	{
		AccumulatedTime = FMath::Max(0.0f, World->GetTimeSeconds() - CooldownStartTime);
	}
	else
	{
		AccumulatedTime += Delta;
	}

	if (AccumulatedTime >= CoolTime)
	{
		CooldownDone();
		return;
	}

	SyncMaterialState();
}

bool UAbilityIconUI::IsCooldowning() const
{
	if (!IsUnLock())
	{
		return false;
	}

	return bCooldowning;
}

void UAbilityIconUI::CooldownDone()
{
	bCooldowning = false;
	AccumulatedTime = 0.0f;
	CooldownStartTime = 0.0f;
	CoolTime = 0.0f;

	SyncMaterialState();
	TryRestoreDefaultBrush();
}
