// Fill out your copyright notice in the Description page of Project Settings.

#include "PartnerHealthUI.h"
#include "GhostProgressBar.h"

#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"

void UPartnerHealthUI::NativeConstruct()
{
	Super::NativeConstruct();

	SetProgressBarRatio(CurrentHPRatio, true);
	if (bDebugHPBarUI)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[PartnerHP][Construct] World=%s Widget=%s Class=%s HPBar=%s BarClass=%s HPText=%s"),
			*GetNameSafe(GetWorld()), *GetNameSafe(this), *GetNameSafe(GetClass()),
			*GetNameSafe(HPBar.Get()), *GetNameSafe(HPBar ? HPBar->GetClass() : nullptr), *GetNameSafe(HPText.Get()));
	}
}

void UPartnerHealthUI::Activate()
{
	SetProgressBarRatio(CurrentHPRatio, true);
	Super::Activate();
}

void UPartnerHealthUI::Deactivate()
{
	SetProgressBarRatio(CurrentHPRatio, true);
	Super::Deactivate();
}

void UPartnerHealthUI::HealthChanged_Implementation(float InHealthRatio)
{
	CurrentHPRatio = FMath::Clamp(InHealthRatio, 0.0f, 1.0f);
	SetProgressBarRatio(CurrentHPRatio, bHealthUpdateImmediate);
}

void UPartnerHealthUI::SetHealthState(float InHealth, float InMaxHealth, bool bImmediate)
{
	const bool bValidState = FMath::IsFinite(InHealth) && FMath::IsFinite(InMaxHealth) && InMaxHealth > 0.0f;
	TGuardValue<bool> ImmediateGuard(bHealthUpdateImmediate, bImmediate || !bHasHealthState
		|| CachedMaxHealth != InMaxHealth || !bValidState || !IsVisible());
	CurrentHPValue = bValidState ? FMath::Max(InHealth, 0.0f) : 0.0f;
	CurrentHPRatio = bValidState
		? FMath::Clamp(InHealth / InMaxHealth, 0.0f, 1.0f)
		: 0.0f;

	HealthChanged(CurrentHPRatio);
	SetProgressBarRatio(CurrentHPRatio, bHealthUpdateImmediate);
	SetValueText(CurrentHPValue);
	if (bDebugHPBarUI)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[PartnerHP][WidgetApply] World=%s Widget=%s Input=%.2f/%.2f Ratio=%.3f Immediate=%d Visible=%d HPBar=%s BarPercent=%.3f HPText=%s Text=%s"),
			*GetNameSafe(GetWorld()), *GetNameSafe(this), InHealth, InMaxHealth, CurrentHPRatio,
			bHealthUpdateImmediate, IsVisible(), *GetNameSafe(HPBar.Get()), HPBar ? HPBar->GetPercent() : -1.0f,
			*GetNameSafe(HPText.Get()), HPText ? *HPText->GetText().ToString() : TEXT("<null>"));
	}
	CachedMaxHealth = InMaxHealth;
	bHasHealthState = bValidState;
}

void UPartnerHealthUI::SetProgressBarRatio(float InRatio, bool bImmediate)
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

	const float Ratio = FMath::Clamp(InRatio, 0.0f, 1.0f);
	if (UGhostProgressBar* GhostBar = Cast<UGhostProgressBar>(HPBar))
	{
		if (bImmediate) GhostBar->SetPercentImmediate(Ratio);
		else GhostBar->SetTargetPercent(Ratio);
	}
	else
	{
		HPBar->SetPercent(Ratio);
	}
}

void UPartnerHealthUI::SetValueText(float InValue)
{
	if (HPText)
	{
		HPText->SetText(FText::AsNumber(FMath::RoundToInt(FMath::Max(InValue, 0.0f))));
	}
}

void UPartnerHealthUI::NativeDestruct()
{
	SetProgressBarRatio(CurrentHPRatio, true);
	Super::NativeDestruct();
}
