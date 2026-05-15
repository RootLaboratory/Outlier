// Fill out your copyright notice in the Description page of Project Settings.

#include "ShooterAbilityUI.h"
#include "UI/AbilityIconUI.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Border.h"
#include "Framework/Application/SlateApplication.h"

void UShooterAbilityUI::NativeConstruct()
{
	Super::NativeConstruct();

	AbilitySections.Reset();

	RightAbilityTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Ability.Shooter.Teleport")), false);
	BottomAbilityTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Ability.Shooter.Shield")), false);
	LeftAbilityTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Ability.Shooter.Stealth")), false);
	TopAbilityTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Ability.Shooter.Stimpack")), false);

	if (!RightAbilityTag.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Missing GameplayTag: Ability.Shooter.Teleport"));
	}
	if (!BottomAbilityTag.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Missing GameplayTag: Ability.Shooter.Shield"));
	}
	if (!LeftAbilityTag.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Missing GameplayTag: Ability.Shooter.Stealth"));
	}
	if (!TopAbilityTag.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Missing GameplayTag: Ability.Shooter.Stimpack"));
	}

	RegisterAbilityIcon(IconTeleport, RightAbilityTag, true);
	RegisterAbilityIcon(IconShield, BottomAbilityTag, true);
	RegisterAbilityIcon(IconStealth, LeftAbilityTag, true);
	RegisterAbilityIcon(IconStimpack, TopAbilityTag, true);

	if (BigCircle && CenterCircle && M_ShooterAbilityUI)
	{
		BigCircle->SetBrushFromMaterial(M_ShooterAbilityUI);
		ShooterAbilityMID = BigCircle->GetDynamicMaterial();

		const FVector2D ASize = BigCircle->GetCachedGeometry().GetLocalSize();
		const FVector2D BSize = CenterCircle->GetCachedGeometry().GetLocalSize();
		const float RadiusUV = (BSize.X * 0.5f) / ASize.X;

		ShooterAbilityMID->SetScalarParameterValue(TEXT("CutRadius"), RadiusUV);
		ShooterAbilityMID->SetScalarParameterValue(TEXT("CutFeather"), 2.f / ASize.X);
	}
}

void UShooterAbilityUI::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	for (const TPair<FGameplayTag, TObjectPtr<UAbilityIconUI>>& AbilitySection : AbilitySections)
	{
		UAbilityIconUI* Section = AbilitySection.Value;
		if (!Section || !Section->IsCooldowning())
		{
			continue;
		}

		Section->UpdateCoolTime(InDeltaTime);
	}
}

bool UShooterAbilityUI::TryGetHoveredAbility(FGameplayTag& OutAbilityTag)
{
	OutAbilityTag = FGameplayTag();

	const float AngleDeg = CalculateCoordinate();
	const FGameplayTag HoveredAbilityTag = GetAbilityTagByAngle(AngleDeg);
	UAbilityIconUI* HoveredIcon = GetAbilityIcon(HoveredAbilityTag);
	if (!HoveredIcon || !HoveredIcon->IsUnLock())
	{
		return false;
	}

	OutAbilityTag = HoveredAbilityTag;

	//
	if (HoveredAbilityTag == RightAbilityTag)
	{
		HoveredIcon->SetCoolTime(5.0f);
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

	ShooterAbilityMID->SetScalarParameterValue(TEXT("Direction"), -1);

	if (AngleDeg >= -45.f && AngleDeg < 45.f)
	{
		if (IsAbilityUnlocked(RightAbilityTag))
		{
			ShooterAbilityMID->SetScalarParameterValue(TEXT("Direction"), 0);
		}
	}
	else if (AngleDeg >= 45.f && AngleDeg < 135.f)
	{
		if (IsAbilityUnlocked(BottomAbilityTag))
		{
			ShooterAbilityMID->SetScalarParameterValue(TEXT("Direction"), 1);
		}
	}
	else if (AngleDeg >= 135.f || AngleDeg < -135.f)
	{
		if (IsAbilityUnlocked(LeftAbilityTag))
		{
			ShooterAbilityMID->SetScalarParameterValue(TEXT("Direction"), 2);
		}
	}
	else
	{
		if (IsAbilityUnlocked(TopAbilityTag))
		{
			ShooterAbilityMID->SetScalarParameterValue(TEXT("Direction"), 3);
		}
	}
}

void UShooterAbilityUI::RegisterAbilityIcon(UAbilityIconUI* Icon, const FGameplayTag& AbilityTag, bool bUnlock)
{
	if (!Icon || !AbilityTag.IsValid())
	{
		return;
	}

	Icon->AbilityTag = AbilityTag;
	AbilitySections.Add(AbilityTag, Icon);

	if (bUnlock)
	{
		Icon->AbilityUnLock();
	}
}

UAbilityIconUI* UShooterAbilityUI::GetAbilityIcon(const FGameplayTag& AbilityTag) const
{
	const TObjectPtr<UAbilityIconUI>* Icon = AbilitySections.Find(AbilityTag);
	return Icon ? Icon->Get() : nullptr;
}

FGameplayTag UShooterAbilityUI::GetAbilityTagByAngle(float AngleDeg) const
{
	if (AngleDeg >= -45.f && AngleDeg < 45.f)
	{
		return RightAbilityTag;
	}
	if (AngleDeg >= 45.f && AngleDeg < 135.f)
	{
		return BottomAbilityTag;
	}
	if (AngleDeg >= 135.f || AngleDeg < -135.f)
	{
		return LeftAbilityTag;
	}

	return TopAbilityTag;
}

bool UShooterAbilityUI::IsAbilityUnlocked(const FGameplayTag& AbilityTag) const
{
	if (UAbilityIconUI* Icon = GetAbilityIcon(AbilityTag))
	{
		return Icon->IsUnLock();
	}

	return false;
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
