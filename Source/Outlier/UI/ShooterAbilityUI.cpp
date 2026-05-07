// Fill out your copyright notice in the Description page of Project Settings.

#include "ShooterAbilityUI.h"
#include "UI/ShooterAbilitySectionUI.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Border.h"
#include "Framework/Application/SlateApplication.h"

void UShooterAbilityUI::NativeConstruct()
{
	Super::NativeConstruct();

	AbilitySections.Reset();
	AbilitySections.SetNum(static_cast<int32>(EShooterAbility::None) + 1);

	AbilitySections[static_cast<int32>(EShooterAbility::Teleport)] = IconTeleport;
	AbilitySections[static_cast<int32>(EShooterAbility::Shield)] = IconShield;
	AbilitySections[static_cast<int32>(EShooterAbility::Stealth)] = IconStealth;
	AbilitySections[static_cast<int32>(EShooterAbility::Stimpack)] = IconStimpack;
	AbilitySections[static_cast<int32>(EShooterAbility::None)] = nullptr;

	if (BigCircle && M_ShooterAbilityUI)
	{
		BigCircle->SetBrushFromMaterial(M_ShooterAbilityUI);
		ShooterAbilityMID = BigCircle->GetDynamicMaterial();
	}

}

bool UShooterAbilityUI::TryGetHoveredAbility(EShooterAbility& OutAbility)
{
	const float AngleDeg = CalculateCoordinate();

	const TCHAR* DirectionText = TEXT("None");

	if (AngleDeg >= -45.f && AngleDeg < 45.f)
	{
		if (AbilitySections[static_cast<int32>(EShooterAbility::Teleport)]->IsUnLock())
		{
			OutAbility = EShooterAbility::Teleport;
			DirectionText = TEXT("RIGHT");
		}
	}
	else if (AngleDeg >= 45.f && AngleDeg < 135.f)
	{
		if (AbilitySections[static_cast<int32>(EShooterAbility::Shield)]->IsUnLock())
		{
			OutAbility = EShooterAbility::Shield;
			DirectionText = TEXT("BOTTOM");
		}
	}
	else if (AngleDeg >= 135.f || AngleDeg < -135.f)
	{
		if (AbilitySections[static_cast<int32>(EShooterAbility::Stealth)]->IsUnLock())
		{
			OutAbility = EShooterAbility::Stealth;
			DirectionText = TEXT("LEFT");
		}
	}
	else
	{
		if (AbilitySections[static_cast<int32>(EShooterAbility::Stimpack)]->IsUnLock())
		{
			OutAbility = EShooterAbility::Stimpack;
			DirectionText = TEXT("TOP");
		}
	}

	
	return true;
}

void UShooterAbilityUI::TryHovering()
{
	if (!ShooterAbilityMID)
	{
		return;
	}

	const float AngleDeg = CalculateCoordinate();

	float OutMaterialParameter = 0;
	const TCHAR* DirectionText = TEXT("None");

	if (AngleDeg >= -45.f && AngleDeg < 45.f)
	{
		if (AbilitySections[static_cast<int32>(EShooterAbility::Teleport)]->IsUnLock())
		{
			ShooterAbilityMID->SetScalarParameterValue(TEXT("Direction"), 0);
			DirectionText = TEXT("RIGHT");
		}
	}
	else if (AngleDeg >= 45.f && AngleDeg < 135.f)
	{
		if (AbilitySections[static_cast<int32>(EShooterAbility::Shield)]->IsUnLock())
		{
			ShooterAbilityMID->SetScalarParameterValue(TEXT("Direction"), 1);
			DirectionText = TEXT("BOTTOM");
		}
	}
	else if (AngleDeg >= 135.f || AngleDeg < -135.f)
	{
		if (AbilitySections[static_cast<int32>(EShooterAbility::Stealth)]->IsUnLock())
		{
			ShooterAbilityMID->SetScalarParameterValue(TEXT("Direction"), 2);
			DirectionText = TEXT("LEFT");
		}
	}
	else 
	{
		if (AbilitySections[static_cast<int32>(EShooterAbility::Stimpack)]->IsUnLock())
		{
			ShooterAbilityMID->SetScalarParameterValue(TEXT("Direction"), 3);
			DirectionText = TEXT("TOP");
		}
	}

}

float UShooterAbilityUI::CalculateCoordinate()
{
	if (!CenterCircle || !BigCircle)
	{
		return 0;
	}

	const FGeometry& SmallGeometry = CenterCircle->GetCachedGeometry();
	const FGeometry& BigGeometry = BigCircle->GetCachedGeometry();

	const FVector2D SmallCenterScreen =
		SmallGeometry.LocalToAbsolute(SmallGeometry.GetLocalSize() * 0.5f);
	const FVector2D BigCenterScreen =
		BigGeometry.LocalToAbsolute(BigGeometry.GetLocalSize() * 0.5f);
	const FVector2D MouseScreen = FSlateApplication::Get().GetCursorPos();
	const FVector2D MouseViewport = UWidgetLayoutLibrary::GetMousePositionOnViewport(this);

	const float BigRadius = FMath::Min(BigGeometry.GetLocalSize().X, BigGeometry.GetLocalSize().Y) * 0.5f;
	const float SmallRadius = FMath::Min(SmallGeometry.GetLocalSize().X, SmallGeometry.GetLocalSize().Y) * 0.5f;
	const FVector2D ToSmallCenter = MouseScreen - SmallCenterScreen;
	const FVector2D ToBigCenter = MouseScreen - BigCenterScreen;

	const bool bInsideBigCircle = ToBigCenter.SizeSquared() <= FMath::Square(BigRadius);
	const bool bOutsideSmallCircle = ToSmallCenter.SizeSquared() >= FMath::Square(SmallRadius);
	if (!bInsideBigCircle || !bOutsideSmallCircle || ToSmallCenter.IsNearlyZero())
	{
		return 0;
	}

	float AngleDeg = FMath::RadiansToDegrees(FMath::Atan2(ToSmallCenter.Y, ToSmallCenter.X));

	return AngleDeg;
}
