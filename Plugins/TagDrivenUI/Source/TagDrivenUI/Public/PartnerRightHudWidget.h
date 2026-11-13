// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PartnerRightHudWidget.generated.h"

class UAbilitySlotUI;
class UInputAction;
class URetainerBox;

/** 모듈화된 Partner 오른쪽 HUD가 네 공용 능력 슬롯의 바인딩을 소유한다. */
UCLASS(Blueprintable)
class TAGDRIVENUI_API UPartnerRightHudWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Set these on the Partner Right HUD Blueprint's Class Defaults.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner|Input")
	TObjectPtr<UInputAction> ShieldInputAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner|Input")
	TObjectPtr<UInputAction> HackingInputAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner|Input")
	TObjectPtr<UInputAction> ScanInputAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner|Input")
	TObjectPtr<UInputAction> EMPInputAction;

	UAbilitySlotUI* GetShieldAbilitySlot() const { return ShieldAbilitySlot; }
	UAbilitySlotUI* GetHackingAbilitySlot() const { return HackingAbilitySlot; }
	UAbilitySlotUI* GetScanAbilitySlot() const { return ScanAbilitySlot; }
	UAbilitySlotUI* GetEMPAbilitySlot() const { return EMPAbilitySlot; }

	// Partner 다운 중 Right HUD 전체를 회색 처리한다. 해제 시 Retainer 를 꺼서 비용을 없앤다.
	void SetGrayout(bool bEnabled, float InGrayoutAmount);

protected:
	virtual void NativeConstruct() override;

	// Right HUD 전체를 감싼다. Effect Material 의 GrayAmount 를 코드에서 넣는다.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<URetainerBox> GrayoutRetainer;

	UPROPERTY(EditDefaultsOnly, Category = "KnockDown")
	FName GrayAmountParameterName = TEXT("GrayAmount");

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UAbilitySlotUI> ShieldAbilitySlot;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UAbilitySlotUI> HackingAbilitySlot;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UAbilitySlotUI> ScanAbilitySlot;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UAbilitySlotUI> EMPAbilitySlot;

private:
	void ApplyGrayout();

	bool bGrayoutEnabled = false;
	float GrayoutAmount = 0.0f;
};
