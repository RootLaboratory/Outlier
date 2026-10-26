// Fill out your copyright notice in the Description page of Project Settings.


#include "HPBarUI.h"
#include "GhostProgressBar.h"

#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
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

	SetProgressBarRatio(HPBar, CurrentHPRatio, true);
	SetProgressBarRatio(ShieldBar, CurrentShieldRatio, true);
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

void UHPBarUI::Activate()
{
	// 숨겨진 동안 받은 최신 수치로 두 Fill을 맞춘 뒤 화면에 복귀한다.
	SetProgressBarRatio(HPBar, CurrentHPRatio, true);
	SetProgressBarRatio(ShieldBar, CurrentShieldRatio, true);
	Super::Activate();
}

void UHPBarUI::Deactivate()
{
	SetProgressBarRatio(HPBar, CurrentHPRatio, true);
	SetProgressBarRatio(ShieldBar, CurrentShieldRatio, true);
	Super::Deactivate();
}

void UHPBarUI::ShieldChanged_Implementation(float InShieldRatio)
{
	CurrentShieldRatio = FMath::Clamp(InShieldRatio, 0.0f, 1.0f);
	if (bDebugHPBarUI)
	{
		UE_LOG(LogTemp, Log, TEXT("[HPBarUI] ShieldChanged Raw=%.3f Clamped=%.3f Bar=%s"), InShieldRatio, CurrentShieldRatio, *GetNameSafe(ShieldBar));
	}
	SetProgressBarRatio(ShieldBar, CurrentShieldRatio, bShieldUpdateImmediate);
}

void UHPBarUI::HealthChanged_Implementation(float InHealthRatio)
{
	CurrentHPRatio = FMath::Clamp(InHealthRatio, 0.0f, 1.0f);
	if (bDebugHPBarUI)
	{
		UE_LOG(LogTemp, Log, TEXT("[HPBarUI] HealthChanged Raw=%.3f Clamped=%.3f Bar=%s"), InHealthRatio, CurrentHPRatio, *GetNameSafe(HPBar));
	}
	SetProgressBarRatio(HPBar, CurrentHPRatio, bHealthUpdateImmediate);
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

void UHPBarUI::SetHealthState(float InHealth, float InMaxHealth, bool bImmediate)
{
	const bool bValidState = FMath::IsFinite(InHealth) && FMath::IsFinite(InMaxHealth) && InMaxHealth > 0.0f;
	// BP 이벤트 안의 Parent 호출과 이벤트 이후 최종 적용이 같은 정책을 사용한다.
	TGuardValue<bool> ImmediateGuard(bHealthUpdateImmediate, bImmediate || !bHasHealthState
		|| CachedMaxHealth != InMaxHealth || !bValidState || !IsVisible());
	CurrentHPValue = bValidState ? FMath::Max(InHealth, 0.0f) : 0.0f;
	CurrentHPRatio = bValidState
		? FMath::Clamp(InHealth / InMaxHealth, 0.0f, 1.0f)
		: 0.0f;

	HealthChanged(CurrentHPRatio);
	SetProgressBarRatio(HPBar, CurrentHPRatio, bHealthUpdateImmediate);
	SetValueText(HPText, CurrentHPValue);
	CachedMaxHealth = InMaxHealth;
	bHasHealthState = bValidState;
}

void UHPBarUI::SetShieldState(float InShield, float InMaxShield, bool bImmediate)
{
	const bool bValidState = FMath::IsFinite(InShield) && FMath::IsFinite(InMaxShield) && InMaxShield > 0.0f;
	TGuardValue<bool> ImmediateGuard(bShieldUpdateImmediate, bImmediate || !bHasShieldState
		|| CachedMaxShield != InMaxShield || !bValidState || !IsVisible());
	CurrentShieldValue = bValidState ? FMath::Max(InShield, 0.0f) : 0.0f;
	CurrentShieldRatio = bValidState
		? FMath::Clamp(InShield / InMaxShield, 0.0f, 1.0f)
		: 0.0f;

	ShieldChanged(CurrentShieldRatio);
	SetProgressBarRatio(ShieldBar, CurrentShieldRatio, bShieldUpdateImmediate);
	SetValueText(ShieldText, CurrentShieldValue);
	CachedMaxShield = InMaxShield;
	bHasShieldState = bValidState;
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
	if (ProgressBar->IsA<UGhostProgressBar>())
	{
		// 새 게이지의 외형은 자체 Brush/Tint가 원본이다. 기존 HUD 공통 MID로 덮어쓰지 않는다.
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

void UHPBarUI::SetProgressBarRatio(UProgressBar* ProgressBar, float InRatio, bool bImmediate)
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
	if (UGhostProgressBar* GhostBar = Cast<UGhostProgressBar>(ProgressBar))
	{
		if (bImmediate) GhostBar->SetPercentImmediate(ClampedRatio);
		else GhostBar->SetTargetPercent(ClampedRatio);
	}
	else
	{
		// 기존 WBP도 교체 전까지 정상 표시한다. Ghost는 새 위젯에만 적용된다.
		ProgressBar->SetPercent(ClampedRatio);
	}

	if (bDebugHPBarUI)
	{
		UE_LOG(LogTemp, Log, TEXT("[HPBarUI] SetRatio Bar=%s Raw=%.3f Clamped=%.3f ActualPercent=%.3f"), *GetNameSafe(ProgressBar), InRatio, ClampedRatio, ProgressBar->GetPercent());
	}
}

void UHPBarUI::SetValueText(UTextBlock* TextBlock, float InValue)
{
	if (TextBlock)
	{
		TextBlock->SetText(FText::AsNumber(FMath::RoundToInt(FMath::Max(InValue, 0.0f))));
	}
}

void UHPBarUI::NativeDestruct()
{
	SetProgressBarRatio(HPBar, CurrentHPRatio, true);
	SetProgressBarRatio(ShieldBar, CurrentShieldRatio, true);
	Super::NativeDestruct();
}
