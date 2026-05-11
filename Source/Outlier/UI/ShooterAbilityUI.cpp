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
	AbilitySections[static_cast<int32>(EShooterAbility::Teleport)]->SetAbility(EShooterAbility::Teleport);

	AbilitySections[static_cast<int32>(EShooterAbility::Teleport)]->AbilityUnLock();

	AbilitySections[static_cast<int32>(EShooterAbility::Shield)] = IconShield;
	AbilitySections[static_cast<int32>(EShooterAbility::Shield)]->SetAbility(EShooterAbility::Shield);


	AbilitySections[static_cast<int32>(EShooterAbility::Stealth)] = IconStealth;
	AbilitySections[static_cast<int32>(EShooterAbility::Stealth)]->SetAbility(EShooterAbility::Stealth);

	AbilitySections[static_cast<int32>(EShooterAbility::Stimpack)] = IconStimpack;
	AbilitySections[static_cast<int32>(EShooterAbility::Stimpack)]->SetAbility(EShooterAbility::Stimpack);

	AbilitySections[static_cast<int32>(EShooterAbility::None)] = nullptr;

	if (BigCircle && M_ShooterAbilityUI)
	{
		BigCircle->SetBrushFromMaterial(M_ShooterAbilityUI);
		ShooterAbilityMID = BigCircle->GetDynamicMaterial();


		const FVector2D ASize = BigCircle->GetCachedGeometry().GetLocalSize();
		const FVector2D BSize = CenterCircle->GetCachedGeometry().GetLocalSize();

		const float RadiusUV = (BSize.X * 0.5f) / ASize.X; //큰원의 영역에서, 작은 원의 반지름 영역.

		ShooterAbilityMID->SetScalarParameterValue(TEXT("CutRadius"), RadiusUV);
		ShooterAbilityMID->SetScalarParameterValue(TEXT("CutFeather"), 2.f / ASize.X);
	}
	//UE_LOG(LogTemp, Warning, TEXT("ViewportScale: %f"), UWidgetLayoutLibrary::GetViewportScale(this));

}

void UShooterAbilityUI::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	for (UShooterAbilitySectionUI* Section : AbilitySections)
	{
		if (!Section || !Section->IsCooldowning())
		{
			continue;
		}

		Section->UpdateCoolTime(InDeltaTime);

	}
}

bool UShooterAbilityUI::TryGetHoveredAbility(EShooterAbility& OutAbility)
{
	const float AngleDeg = CalculateCoordinate();

	const TCHAR* DirectionText = TEXT("None");

	if (AngleDeg >= -45.f && AngleDeg < 45.f)
	{
		if (AbilitySections[static_cast<int32>(EShooterAbility::Teleport)] || AbilitySections[static_cast<int32>(EShooterAbility::Teleport)]->IsUnLock())
		{
			AbilitySections[static_cast<int32>(EShooterAbility::Teleport)]->SetCoolTime(5.0f);
			OutAbility = EShooterAbility::Teleport;
			DirectionText = TEXT("RIGHT");
		}
	}
	else if (AngleDeg >= 45.f && AngleDeg < 135.f)
	{
		if (AbilitySections[static_cast<int32>(EShooterAbility::Shield)] || AbilitySections[static_cast<int32>(EShooterAbility::Shield)]->IsUnLock())
		{
			OutAbility = EShooterAbility::Shield;
			DirectionText = TEXT("BOTTOM");
		}
	}
	else if (AngleDeg >= 135.f || AngleDeg < -135.f)
	{
		if (AbilitySections[static_cast<int32>(EShooterAbility::Stealth)] || AbilitySections[static_cast<int32>(EShooterAbility::Stealth)]->IsUnLock())
		{
			OutAbility = EShooterAbility::Stealth;
			DirectionText = TEXT("LEFT");
		}
	}
	else
	{
		if (AbilitySections[static_cast<int32>(EShooterAbility::Stimpack)] || AbilitySections[static_cast<int32>(EShooterAbility::Stimpack)]->IsUnLock())
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

	ShooterAbilityMID->SetScalarParameterValue(TEXT("Direction"), -1);
	
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
