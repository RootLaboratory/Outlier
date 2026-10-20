// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PartnerRightHudWidget.generated.h"

class UAbilitySlotUI;

/** 모듈화된 Partner 오른쪽 HUD가 네 공용 능력 슬롯의 바인딩을 소유한다. */
UCLASS(Blueprintable)
class TAGDRIVENUI_API UPartnerRightHudWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UAbilitySlotUI* GetShieldAbilitySlot() const { return ShieldAbilitySlot; }
	UAbilitySlotUI* GetHackingAbilitySlot() const { return HackingAbilitySlot; }
	UAbilitySlotUI* GetScanAbilitySlot() const { return ScanAbilitySlot; }
	UAbilitySlotUI* GetEMPAbilitySlot() const { return EMPAbilitySlot; }

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UAbilitySlotUI> ShieldAbilitySlot;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UAbilitySlotUI> HackingAbilitySlot;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UAbilitySlotUI> ScanAbilitySlot;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UAbilitySlotUI> EMPAbilitySlot;
};
