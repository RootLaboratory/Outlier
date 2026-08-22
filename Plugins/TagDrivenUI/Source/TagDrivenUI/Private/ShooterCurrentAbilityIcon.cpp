// Fill out your copyright notice in the Description page of Project Settings.

#include "ShooterCurrentAbilityIcon.h"

#include "AbilityIconUI.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "TagDrivenUIGameplayTags.h"

void UShooterCurrentAbilityIcon::NativeConstruct()
{
	Super::NativeConstruct();

	ModuleTag = TagDrivenUITags::Shooter::CurrentAbility();

	if (!CurrentAbilityTag.IsValid())
	{
		CurrentAbilityTag = TagDrivenUITags::Ability::Shooter::Stealth();
	}

	SetCurrentAbility(CurrentAbilityTag);
}

void UShooterCurrentAbilityIcon::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (CurrentAbilityIcon && CurrentAbilityIcon->IsCooldowning())
	{
		CurrentAbilityIcon->UpdateCoolTime(InDeltaTime);
	}
}

void UShooterCurrentAbilityIcon::SetCurrentAbility(const FGameplayTag& AbilityTag)
{
	if (!AbilityTag.IsValid())
	{
		return;
	}

	if (!CurrentAbilityIcon)
	{
		CurrentAbilityTag = AbilityTag;
		return;
	}

	if (AbilityTag == CurrentAbilityTag && CurrentAbilityIcon->IsCooldowning())
	{
		CurrentAbilityIcon->SetAbility(AbilityTag);
		CurrentAbilityIcon->SetVisibility(ESlateVisibility::Visible);
		return;
	}

	if (AbilityTag != CurrentAbilityTag && CurrentAbilityIcon->IsCooldowning())
	{
		CurrentAbilityIcon->CooldownDone();
	}

	CurrentAbilityTag = AbilityTag;
	CurrentAbilityIcon->SetAbility(AbilityTag);
	ApplyTextureForAbility(AbilityTag);
	CurrentAbilityIcon->AbilityUnLock();
}

bool UShooterCurrentAbilityIcon::ApplyCooldownIfMatches(const FGameplayTag& AbilityTag, float CoolTime)
{
	if (!CurrentAbilityIcon || AbilityTag != CurrentAbilityTag || CoolTime <= 0.0f)
	{
		return false;
	}

	CurrentAbilityIcon->SetCoolTime(CoolTime);
	return true;
}

void UShooterCurrentAbilityIcon::ResetCooldown()
{
	if (CurrentAbilityIcon)
	{
		CurrentAbilityIcon->CooldownDone();
	}
}

UTexture2D* UShooterCurrentAbilityIcon::GetTextureForAbility(const FGameplayTag& AbilityTag) const
{
	if (AbilityTag == TagDrivenUITags::Ability::Shooter::QuantumLeap())
	{
		return TeleportTexture;
	}

	if (AbilityTag == TagDrivenUITags::Ability::Shooter::BulletReflection())
	{
		return ShieldTexture;
	}

	if (AbilityTag == TagDrivenUITags::Ability::Shooter::Stealth())
	{
		return StealthTexture;
	}

	if (AbilityTag == TagDrivenUITags::Ability::Shooter::WeaponOvercharge())
	{
		return StimPackTexture;
	}

	return nullptr;
}

void UShooterCurrentAbilityIcon::ApplyTextureForAbility(const FGameplayTag& AbilityTag)
{
	if (!CurrentAbilityIcon || !CurrentAbilityIcon->AbilityIcon)
	{
		return;
	}

	if (UTexture2D* AbilityTexture = GetTextureForAbility(AbilityTag))
	{
		CurrentAbilityIcon->AbilityIcon->SetBrushFromTexture(AbilityTexture, true);
		CurrentAbilityIcon->DefaultIconBrush = CurrentAbilityIcon->AbilityIcon->GetBrush();
		CurrentAbilityIcon->SetVisibility(ESlateVisibility::Visible);
		return;
	}

	CurrentAbilityIcon->SetVisibility(ESlateVisibility::Collapsed);
}
