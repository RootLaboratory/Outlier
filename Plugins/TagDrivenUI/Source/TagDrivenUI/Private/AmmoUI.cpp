// Fill out your copyright notice in the Description page of Project Settings.


#include "AmmoUI.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"

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

	// Blueprint 이벤트가 텍스트를 건드려도 BindWidget 값이 최종 상태가 되도록 보장한다.
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

void UAmmoUI::RefreshAmmoTexts()
{
	if (CurrentAmmoText)
	{
		CurrentAmmoText->SetText(FText::AsNumber(CurrentAmmo));
	}

	if (MaxAmmoText)
	{
		MaxAmmoText->SetText(FText::AsNumber(MaxAmmo));
	}
}
