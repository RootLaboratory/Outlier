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

	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float Indelta) override;

public:

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> CrossHairImage;

	// 디자이너에서 지정한 텍스처가 M_ReloadingTimeUI 의 IconTexture 로 들어간다.
	// 머티리얼은 초기화 때 한 번만 입히고, 이후엔 쿨타임 동안만 보이게 켜고 끈다.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UImage> PistolCoolTime;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability CoolTime UI|Material")
	TObjectPtr<UMaterialInterface> M_ReloadingTimeUI;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic>ReloadingTimeMID;


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
