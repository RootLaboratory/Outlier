#include "ShooterCrosshairWidget.h"

#include "CrossHairBase.h"

void UShooterCrosshairWidget::NativeConstruct()
{
	Super::NativeConstruct();
	bPresentationInitialized = false;
	RefreshPresentation();
}

void UShooterCrosshairWidget::SetWeaponType(EWidgetWeaponType Type)
{
	WeaponType = Type;
	RefreshPresentation();
}

void UShooterCrosshairWidget::SetSuitAcquired(bool bAcquired)
{
	if (bAcquired && !bSuitAcquired) ResetRifleCrosshair();
	bSuitAcquired = bAcquired;
	RefreshPresentation();
}

UCrossHairBase* UShooterCrosshairWidget::GetCurrentCrosshair() const
{
	if (WeaponType == EWidgetWeaponType::Pistol)
	{
		return PistolCrossHairUI;
	}
	if (WeaponType == EWidgetWeaponType::Rifle && bSuitAcquired)
	{
		return RifleCrossHairUI;
	}
	return nullptr;
}

void UShooterCrosshairWidget::RefreshPresentation()
{
	UCrossHairBase* Selected = GetCurrentCrosshair();
	if (bPresentationInitialized && ActiveCrosshair == Selected) return;
	for (UCrossHairBase* Crosshair : { RifleCrossHairUI.Get(), PistolCrossHairUI.Get() })
	{
		if (!Crosshair) continue;
		// 슈트 상태를 반복 갱신해도 Blueprint 활성화 효과가 다시 시작되지 않게 한다.
		const bool bShouldShow = Crosshair == Selected;
		if (bShouldShow)
		{
			Crosshair->Activate();
		}
		else
		{
			Crosshair->Deactivate();
		}
	}
	ActiveCrosshair = Selected;
	bPresentationInitialized = true;
}

void UShooterCrosshairWidget::ResetRifleCrosshair()
{
	if (RifleCrossHairUI)
	{
		RifleCrossHairUI->ResetCrossHairState();
		// Blueprint 시각 효과를 초기화할 때 미장착 라이플 조준점이 나타나지 않게 한다.
		if (ActiveCrosshair != RifleCrossHairUI) RifleCrossHairUI->Deactivate();
	}
}
