// Fill out your copyright notice in the Description page of Project Settings.


#include "AmmoUI.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"

DEFINE_LOG_CATEGORY_STATIC(LogAmmoUI, Log, All);

void UAmmoUI::NativeConstruct()
{
	Super::NativeConstruct();
	RefreshAmmoTexts();

	ApplyWeaponOverchargeVisibility();
}

void UAmmoUI::AmmoCountChanged_Implementation(int32 InAmmoCount)
{
	CurrentAmmo = InAmmoCount;
	Temp_AmmoCount = InAmmoCount;
	RefreshAmmoTexts();
}

void UAmmoUI::AmmoStateChanged_Implementation(int32 InCurrentAmmo, int32 InMaxAmmo)
{
	CurrentAmmo = InCurrentAmmo;
	MaxAmmo = InMaxAmmo;
	Temp_AmmoCount = InCurrentAmmo;
	RefreshAmmoTexts();
}

void UAmmoUI::SetAmmoState(int32 InCurrentAmmo, int32 InMaxAmmo)
{
	CurrentAmmo = InCurrentAmmo;
	MaxAmmo = InMaxAmmo;
	Temp_AmmoCount = InCurrentAmmo;

	// Current/Max를 함께 받는 경로만 사용한다. 구형 단일 이벤트를 뒤이어 호출하면
	// WBP 구현에서 Max 텍스트까지 Current 값으로 다시 덮을 수 있다.
	AmmoStateChanged(InCurrentAmmo, InMaxAmmo);

	// BP 이벤트가 Parent를 호출하지 않아도 최종 숫자와 잔량 색상은 여기서 함께 적용한다.
	RefreshAmmoTexts();
}

void UAmmoUI::SetWeaponOverchargeActive(bool bActive)
{
	if (bWeaponOverchargeActive == bActive)
	{
		return;
	}

	bWeaponOverchargeActive = bActive;
	ApplyWeaponOverchargeVisibility();
}

void UAmmoUI::SetInfiniteAmmoDisplayActive(bool bActive)
{
	if (bInfiniteAmmoDisplayActive == bActive)
	{
		return;
	}

	bInfiniteAmmoDisplayActive = bActive;
	ApplyWeaponOverchargeVisibility();
}

void UAmmoUI::ApplyWeaponOverchargeVisibility()
{
	if (!AmmoCanvas)
	{
		return;
	}

	const bool bShowInfinity = bWeaponOverchargeActive || bInfiniteAmmoDisplayActive;
	if (!bShowInfinity)
	{
		// 숨겨진 동안 변경된 탄수/Max를 먼저 반영한 뒤 숫자를 다시 표시한다.
		RefreshAmmoTexts();
	}

	// 패널(SizeBox 등)은 그대로 두고 표시 위젯만 뒤집는다. 무한 이미지와 나머지는 항상 반대 상태다.
	UWidgetTree::ForWidgetAndChildren(AmmoCanvas, [this, bShowInfinity](UWidget* Widget)
	{
		if (!Widget || Widget->IsA<UPanelWidget>())
		{
			return;
		}

		const bool bShow = (Widget == OverLoad_Infinity) == bShowInfinity;
		Widget->SetVisibility(bShow ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	});
}

FLinearColor UAmmoUI::ResolveAmmoTextColor()
{
	float EffectiveLowRatio = LowAmmoRatio;
	float EffectiveCriticalRatio = CriticalAmmoRatio;
	const bool bValidRatios = FMath::IsFinite(LowAmmoRatio)
		&& FMath::IsFinite(CriticalAmmoRatio)
		&& CriticalAmmoRatio >= 0.0f
		&& CriticalAmmoRatio <= LowAmmoRatio
		&& LowAmmoRatio <= 1.0f;
	if (!bValidRatios)
	{
		// 잘못된 설정은 에셋을 수정하지 않고 이번 판정에 사용할 두 비율만 기본값으로 대체한다.
		EffectiveLowRatio = 0.30f;
		EffectiveCriticalRatio = 0.10f;
		if (!bInvalidAmmoRatiosWarningLogged)
		{
			UE_LOG(LogAmmoUI, Warning, TEXT("[%s] Invalid ammo text ratios (Critical=%g, Low=%g); using 0.10/0.30."),
				*GetName(), CriticalAmmoRatio, LowAmmoRatio);
			bInvalidAmmoRatiosWarningLogged = true;
		}
	}

	if (MaxAmmo <= 0)
	{
		return NormalAmmoTextColor;
	}

	const double Ratio = FMath::Clamp(static_cast<double>(CurrentAmmo) / static_cast<double>(MaxAmmo), 0.0, 1.0);
	// 위험 구간은 낮은 잔량 구간에도 포함되므로 먼저 판정한다.
	if (Ratio <= EffectiveCriticalRatio)
	{
		return CriticalAmmoTextColor;
	}
	if (Ratio <= EffectiveLowRatio)
	{
		return LowAmmoTextColor;
	}
	return NormalAmmoTextColor;
}

void UAmmoUI::RefreshAmmoTexts()
{
	if (CurrentAmmoText)
	{
		CurrentAmmoText->SetText(FText::AsNumber(CurrentAmmo));
		CurrentAmmoText->SetColorAndOpacity(FSlateColor(ResolveAmmoTextColor()));
	}

	if (MaxAmmoText)
	{
		MaxAmmoText->SetText(FText::AsNumber(MaxAmmo));
	}
}
