// Fill out your copyright notice in the Description page of Project Settings.


#include "HPBarUI.h"

#include "Components/ProgressBar.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

void UHPBarUI::NativeConstruct()
{
	Super::NativeConstruct();

	if (bDebugHPBarUI)
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[HPBarUI] NativeConstruct Widget=%s HPBar=%s ShieldBar=%s PartnerShieldBar=%s Material=%s ColorParam=%s InitialRatios=(HP=%.3f Shield=%.3f PartnerShield=%.3f)"),
			*GetNameSafe(this),
			*GetNameSafe(HPBar),
			*GetNameSafe(ShieldBar),
			*GetNameSafe(PartnerShieldBar),
			*GetNameSafe(ProgressBarFillMaterial),
			*FillColorParameterName.ToString(),
			CurrentHPRatio,
			CurrentShieldRatio,
			CurrentPartnerShieldRatio
		);
	}

	InitializeProgressBarMaterial(TEXT("HP"), HPBar, HPColor, ProgressBarMID);
	InitializeProgressBarMaterial(TEXT("Shield"), ShieldBar, ShieldColor, ProgressBarMID);
	InitializeProgressBarMaterial(TEXT("PartnerShield"), PartnerShieldBar, PartnerShieldColor, ProgressBarMID);

	SetProgressBarRatio(HPBar, CurrentHPRatio);
	SetProgressBarRatio(ShieldBar, CurrentShieldRatio);
	SetProgressBarRatio(PartnerShieldBar, CurrentPartnerShieldRatio);

	if (bDebugHPBarUI)
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[HPBarUI] NativeConstruct complete Widget=%s StoredProgressBarMID=%s"),
			*GetNameSafe(this),
			*GetNameSafe(ProgressBarMID)
		);
	}
}

void UHPBarUI::ShieldChanged_Implementation(float InShieldRatio)
{
	CurrentShieldRatio = FMath::Clamp(InShieldRatio, 0.0f, 1.0f);
	if (bDebugHPBarUI)
	{
		UE_LOG(LogTemp, Log, TEXT("[HPBarUI] ShieldChanged Raw=%.3f Clamped=%.3f Bar=%s"), InShieldRatio, CurrentShieldRatio, *GetNameSafe(ShieldBar));
	}
	SetProgressBarRatio(ShieldBar, CurrentShieldRatio);
}

void UHPBarUI::HealthChanged_Implementation(float InHealthRatio)
{
	CurrentHPRatio = FMath::Clamp(InHealthRatio, 0.0f, 1.0f);
	if (bDebugHPBarUI)
	{
		UE_LOG(LogTemp, Log, TEXT("[HPBarUI] HealthChanged Raw=%.3f Clamped=%.3f Bar=%s"), InHealthRatio, CurrentHPRatio, *GetNameSafe(HPBar));
	}
	SetProgressBarRatio(HPBar, CurrentHPRatio);
}

void UHPBarUI::PartnerShieldChanged_Implementation(float InPartnerShieldRatio)
{
	CurrentPartnerShieldRatio = FMath::Clamp(InPartnerShieldRatio, 0.0f, 1.0f);
	if (bDebugHPBarUI)
	{
		UE_LOG(LogTemp, Log, TEXT("[HPBarUI] PartnerShieldChanged Raw=%.3f Clamped=%.3f Bar=%s"), InPartnerShieldRatio, CurrentPartnerShieldRatio, *GetNameSafe(PartnerShieldBar));
	}
	SetProgressBarRatio(PartnerShieldBar, CurrentPartnerShieldRatio);
}

void UHPBarUI::InitializeProgressBarMaterial(
	const TCHAR* DebugName,
	UProgressBar* ProgressBar,
	const FColor& BarColor,
	TObjectPtr<UMaterialInstanceDynamic>& OutMID
)
{
	if (!ProgressBar)
	{
		if (bDebugHPBarUI)
		{
			UE_LOG(LogTemp, Warning, TEXT("[HPBarUI] InitMaterial skipped: %s ProgressBar is null"), DebugName);
		}
		return;
	}

	const FProgressBarStyle BeforeStyle = ProgressBar->GetWidgetStyle();
	const UObject* BeforeFillResource = BeforeStyle.FillImage.GetResourceObject();

	if (!ProgressBarFillMaterial)
	{
		const FLinearColor LinearBarColor = FLinearColor::FromSRGBColor(BarColor);
		if (bDebugHPBarUI)
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[HPBarUI] InitMaterial fallback: %s Material is null. Bar=%s SourceColor=%s LinearColor=%s BeforeFillResource=%s"),
				DebugName,
				*GetNameSafe(ProgressBar),
				*BarColor.ToString(),
				*LinearBarColor.ToString(),
				*GetNameSafe(BeforeFillResource)
			);
		}
		ProgressBar->SetFillColorAndOpacity(LinearBarColor);
		return;
	}

	OutMID = UMaterialInstanceDynamic::Create(ProgressBarFillMaterial, this);
	if (!OutMID)
	{
		if (bDebugHPBarUI)
		{
			UE_LOG(LogTemp, Error, TEXT("[HPBarUI] InitMaterial failed: %s MID create failed. Material=%s"), DebugName, *GetNameSafe(ProgressBarFillMaterial));
		}
		return;
	}

	const FLinearColor LinearBarColor = FLinearColor::FromSRGBColor(BarColor);
	OutMID->SetVectorParameterValue(FillColorParameterName, LinearBarColor);

	FProgressBarStyle WidgetStyle = ProgressBar->GetWidgetStyle();
	FSlateBrush FillBrush = WidgetStyle.FillImage;
	FillBrush.SetResourceObject(OutMID);
	WidgetStyle.SetFillImage(FillBrush);
	ProgressBar->SetWidgetStyle(WidgetStyle);
	ProgressBar->SetFillColorAndOpacity(FLinearColor::White);

	if (bDebugHPBarUI)
	{
		const FProgressBarStyle AfterStyle = ProgressBar->GetWidgetStyle();
		const UObject* AfterFillResource = AfterStyle.FillImage.GetResourceObject();
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[HPBarUI] InitMaterial success: %s Bar=%s Material=%s MID=%s SourceColor=%s LinearColor=%s BeforeFillResource=%s AfterFillResource=%s FillColorAndOpacity=%s"),
			DebugName,
			*GetNameSafe(ProgressBar),
			*GetNameSafe(ProgressBarFillMaterial),
			*GetNameSafe(OutMID),
			*BarColor.ToString(),
			*LinearBarColor.ToString(),
			*GetNameSafe(BeforeFillResource),
			*GetNameSafe(AfterFillResource),
			*ProgressBar->GetFillColorAndOpacity().ToString()
		);
	}
}

void UHPBarUI::SetProgressBarRatio(UProgressBar* ProgressBar, float InRatio)
{
	if (!ProgressBar)
	{
		if (bDebugHPBarUI)
		{
			UE_LOG(LogTemp, Warning, TEXT("[HPBarUI] SetRatio skipped: ProgressBar is null. Ratio=%.3f"), InRatio);
		}
		return;
	}

	const float ClampedRatio = FMath::Clamp(InRatio, 0.0f, 1.0f);
	ProgressBar->SetPercent(ClampedRatio);

	if (bDebugHPBarUI)
	{
		UE_LOG(LogTemp, Log, TEXT("[HPBarUI] SetRatio Bar=%s Raw=%.3f Clamped=%.3f ActualPercent=%.3f"), *GetNameSafe(ProgressBar), InRatio, ClampedRatio, ProgressBar->GetPercent());
	}
}
