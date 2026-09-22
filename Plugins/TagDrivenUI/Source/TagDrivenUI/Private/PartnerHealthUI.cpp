// Fill out your copyright notice in the Description page of Project Settings.

#include "PartnerHealthUI.h"

#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"

void UPartnerHealthUI::NativeConstruct()
{
	Super::NativeConstruct();

	SetProgressBarRatio(CurrentHPRatio);
}

void UPartnerHealthUI::HealthChanged_Implementation(float InHealthRatio)
{
	CurrentHPRatio = FMath::Clamp(InHealthRatio, 0.0f, 1.0f);
	SetProgressBarRatio(CurrentHPRatio);
}

void UPartnerHealthUI::SetHealthState(float InHealth, float InMaxHealth)
{
	CurrentHPValue = FMath::Max(InHealth, 0.0f);
	CurrentHPRatio = InMaxHealth > 0.0f
		? FMath::Clamp(InHealth / InMaxHealth, 0.0f, 1.0f)
		: 0.0f;

	HealthChanged(CurrentHPRatio);
	SetProgressBarRatio(CurrentHPRatio);
	SetValueText(CurrentHPValue);
}

void UPartnerHealthUI::SetProgressBarRatio(float InRatio)
{
	if (!HPBar)
	{
		if (bDebugHPBarUI)
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[PartnerHealthUI] SetRatio skipped: HPBar is null. Ratio=%.3f"),
				InRatio);
		}
		return;
	}

	HPBar->SetPercent(FMath::Clamp(InRatio, 0.0f, 1.0f));
}

void UPartnerHealthUI::SetValueText(float InValue)
{
	if (HPText)
	{
		HPText->SetText(FText::AsNumber(FMath::RoundToInt(FMath::Max(InValue, 0.0f))));
	}
}
