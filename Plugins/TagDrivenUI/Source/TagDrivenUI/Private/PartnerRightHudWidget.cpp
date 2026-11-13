// Fill out your copyright notice in the Description page of Project Settings.

#include "PartnerRightHudWidget.h"

#include "AbilitySlotUI.h"
#include "InputAction.h"
#include "Components/RetainerBox.h"
#include "Materials/MaterialInstanceDynamic.h"

void UPartnerRightHudWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (ShieldAbilitySlot) ShieldAbilitySlot->SetInputAction(ShieldInputAction);
	if (HackingAbilitySlot) HackingAbilitySlot->SetInputAction(HackingInputAction);
	if (ScanAbilitySlot) ScanAbilitySlot->SetInputAction(ScanInputAction);
	if (EMPAbilitySlot) EMPAbilitySlot->SetInputAction(EMPInputAction);

	// 생성 전에 받은 상태(기본 해제)를 그대로 반영한다.
	ApplyGrayout();

	UE_LOG(LogTemp, Log,
		TEXT("[PartnerHUD][Right] Construct Widget=%s Shield=%s Hacking=%s Scan=%s EMP=%s"),
		*GetNameSafe(this),
		*GetNameSafe(ShieldAbilitySlot),
		*GetNameSafe(HackingAbilitySlot),
		*GetNameSafe(ScanAbilitySlot),
		*GetNameSafe(EMPAbilitySlot));
}

void UPartnerRightHudWidget::SetGrayout(bool bEnabled, float InGrayoutAmount)
{
	bGrayoutEnabled = bEnabled;
	GrayoutAmount = FMath::Clamp(InGrayoutAmount, 0.0f, 1.0f);
	ApplyGrayout();
}

void UPartnerRightHudWidget::ApplyGrayout()
{
	if (!GrayoutRetainer)
	{
		return;
	}
	GrayoutRetainer->SetRetainRendering(bGrayoutEnabled);
	if (UMaterialInstanceDynamic* GrayoutMID = GrayoutRetainer->GetEffectMaterial())
	{
		GrayoutMID->SetScalarParameterValue(GrayAmountParameterName, bGrayoutEnabled ? GrayoutAmount : 0.0f);
	}
}
