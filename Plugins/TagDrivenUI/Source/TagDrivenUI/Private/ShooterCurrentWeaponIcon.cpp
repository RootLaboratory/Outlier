// Fill out your copyright notice in the Description page of Project Settings.


#include "ShooterCurrentWeaponIcon.h"

#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "TagDrivenUIGameplayTags.h"

void UShooterCurrentWeaponIcon::NativeConstruct()
{
	Super::NativeConstruct();

	ModuleTag = TagDrivenUITags::Shooter::CurrentWeapon();

	SetCurrentWeapon(CurrentWeaponType);
}

void UShooterCurrentWeaponIcon::SetCurrentWeapon(EWidgetWeaponType WeaponType)
{
	CurrentWeaponType = WeaponType;

	if (!CurrentWeaponImage)
	{
		return;
	}

	if (UTexture2D* WeaponTexture = GetTextureForWeapon(WeaponType))
	{
		CurrentWeaponImage->SetBrushFromTexture(WeaponTexture, true);
		CurrentWeaponImage->SetVisibility(ESlateVisibility::Visible);
		return;
	}

	CurrentWeaponImage->SetVisibility(ESlateVisibility::Collapsed);
}

UTexture2D* UShooterCurrentWeaponIcon::GetTextureForWeapon(EWidgetWeaponType WeaponType) const
{
	switch (WeaponType)
	{
	case EWidgetWeaponType::Pistol:
		return PistolTexture;
	case EWidgetWeaponType::Rifle:
		return RifleTexture;
	case EWidgetWeaponType::Melee:
		return MeleeTexture;
	case EWidgetWeaponType::Unarmed:
	default:
		return UnarmedTexture;
	}
}
