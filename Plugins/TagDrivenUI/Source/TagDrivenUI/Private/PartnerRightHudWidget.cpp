// Fill out your copyright notice in the Description page of Project Settings.

#include "PartnerRightHudWidget.h"

#include "AbilityIconUI.h"

void UPartnerRightHudWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UE_LOG(LogTemp, Log,
		TEXT("[PartnerHUD][Right] Construct Widget=%s Shield=%s Hacking=%s Scan=%s EMP=%s"),
		*GetNameSafe(this),
		*GetNameSafe(AbilityShieldIcon),
		*GetNameSafe(AbilityHackingIcon),
		*GetNameSafe(AbilityScanIcon),
		*GetNameSafe(AbilityEMPIcon));
}
