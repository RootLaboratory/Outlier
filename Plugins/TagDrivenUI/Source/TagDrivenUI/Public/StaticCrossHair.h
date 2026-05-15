// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "CrossHairBase.h"
#include "StaticCrossHair.generated.h"

class UImage;

/**
 * 
 */
UCLASS()
class TAGDRIVENUI_API UStaticCrossHair : public UCrossHairBase
{
	GENERATED_BODY()
public:

	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float Indelta) override;

public:

	UFUNCTION(BlueprintNativeEvent, Category = "UI")
	void SpawnReloadingTimer();

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> CrossHairImage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability CoolTime UI|Material")
	TObjectPtr<UMaterialInterface> M_ReloadingTimeUI;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic>ReloadingTimeMID;

	FSlateBrush DefaultIconBrush;


public:
	void SetCoolTime(float InCoolTime);
	void UpdateCoolTime(float delta); //delta 누적 및 Material Update
	bool IsCooldowning();
	void CooldownDone();

private:
	float CoolTime = 0; // 후에 Material 연동
	float AccumulatedTime = 0; // 후에 Material 연동
	uint8 bCooldowning : 1 = false;
};
