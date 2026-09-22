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

public:
	UPROPERTY(BlueprintReadOnly, Category = "Data")
	int32 CurrentAmmo = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Data")
	int32 MaxAmmo = 0;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Ammo")
	TObjectPtr<class UTextBlock> CurrentAmmoText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Ammo")
	TObjectPtr<class UTextBlock> MaxAmmoText;

	// Legacy alias. 기존 BP 로직 호환을 위해 유지한다.
	UPROPERTY(BlueprintReadOnly, Category = "Data")
	int Temp_AmmoCount = 40;

protected:
	virtual void NativeConstruct() override;

private:
	void RefreshAmmoTexts();

};
