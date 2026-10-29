// Fill out your copyright notice in the Description page of Project Settings.

#include "PartnerRightHudWidget.h"

#include "AbilitySlotUI.h"
#include "InputAction.h"

void UPartnerRightHudWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (ShieldAbilitySlot) ShieldAbilitySlot->SetInputAction(ShieldInputAction);
	if (HackingAbilitySlot) HackingAbilitySlot->SetInputAction(HackingInputAction);
	if (ScanAbilitySlot) ScanAbilitySlot->SetInputAction(ScanInputAction);
	if (EMPAbilitySlot) EMPAbilitySlot->SetInputAction(EMPInputAction);

	UE_LOG(LogTemp, Log,
		TEXT("[PartnerHUD][Right] Construct Widget=%s Shield=%s Hacking=%s Scan=%s EMP=%s"),
		*GetNameSafe(this),
		*GetNameSafe(ShieldAbilitySlot),
		*GetNameSafe(HackingAbilitySlot),
		*GetNameSafe(ScanAbilitySlot),
		*GetNameSafe(EMPAbilitySlot));
}
