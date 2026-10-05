// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EventDrivenUI.h"
#include "AmmoUI.generated.h"

/**
 * 
 */
UCLASS(Blueprintable)
class TAGDRIVENUI_API UAmmoUI : public UEventDrivenUI
{
	GENERATED_BODY()
	
public:
	// 기존 WBP가 사용 중일 수 있으므로 유지한다.

	UFUNCTION(BlueprintNativeEvent, Category = "UI")
	void AmmoCountChanged(int32 InAmmoCount);

	// 현재/최대 탄약을 함께 사용하는 새 UI 갱신 이벤트.
	UFUNCTION(BlueprintNativeEvent, Category = "UI")
	void AmmoStateChanged(int32 InCurrentAmmo, int32 InMaxAmmo);

	// 동기화 경로에서 상태와 BP 이벤트를 함께 갱신한다.
	void SetAmmoState(int32 InCurrentAmmo, int32 InMaxAmmo);

	// 과충전 중에는 AmmoCanvas 아래 표시 위젯을 접고 OverLoad_Infinity만 켠다. 해제 시 반대.
	void SetWeaponOverchargeActive(bool bActive);
	void SetInfiniteAmmoDisplayActive(bool bActive);

public:
	UPROPERTY(BlueprintReadOnly, Category = "Data")
	int32 CurrentAmmo = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Data")
	int32 MaxAmmo = 0;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Ammo")
	TObjectPtr<class UTextBlock> CurrentAmmoText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Ammo")
	TObjectPtr<class UTextBlock> MaxAmmoText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UCanvasPanel> AmmoCanvas;

	// AmmoCanvas 아래 어디에 있어도 된다(감싼 SizeBox 같은 패널은 건드리지 않는다).
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UImage> OverLoad_Infinity;

	// Legacy alias. 기존 BP 로직 호환을 위해 유지한다.
	UPROPERTY(BlueprintReadOnly, Category = "Data")
	int Temp_AmmoCount = 40;

protected:
	virtual void NativeConstruct() override;

private:
	void RefreshAmmoTexts();
	void ApplyWeaponOverchargeVisibility();

	bool bWeaponOverchargeActive = false;
	bool bInfiniteAmmoDisplayActive = false;
};
