// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MainUIBase.h"
#include "GangTongMainUI.generated.h"


class UPartnerCamUI;
class UStaticCrossHair;
class UAbilityIconUI;
class UImage;
class UPartnerLeftHudWidget;
class UPartnerRightHudWidget;
class UPartnerHPUI;
class UPartnerHealthUI;
//HP Delegate;

UENUM(BlueprintType)
enum class EPlayerHPCondition : uint8
{
	NonShield,
	NormalShield,
	SkillShield,
};

UCLASS()
class TAGDRIVENUI_API UGangTongMainUI : public UMainUIBase
{
	GENERATED_BODY()
	
private:
	virtual void NativeConstruct() override;

	virtual void ModuleInit() override;
	virtual void ModuleDestruct() override;

public:
	// 자기 PlayerState의 슈트 상태: true는 모든 Layer, false는 예외 Layer만 표시한다.
	void SetSuitGatedModulesEnabled(bool bEnabled);

	virtual void ApplyExceptionVisibility() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_Hud;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UStaticCrossHair> CrossHairUI;

	UPROPERTY(Transient)
	TObjectPtr<UAbilityIconUI> AbilityShieldIcon;

	UPROPERTY(Transient)
	TObjectPtr<UAbilityIconUI> AbilityHackingIcon;

	UPROPERTY(Transient)
	TObjectPtr<UAbilityIconUI> AbilityScanIcon;

	UPROPERTY(Transient)
	TObjectPtr<UAbilityIconUI> AbilityEMPIcon;

private:
	void CacheNestedHudWidgets();

	// Main은 컨테이너까지만 직접 바인딩하고, 컨테이너가 내부 모듈을 소유한다.
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPartnerLeftHudWidget> PartnerLeftHUD;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPartnerRightHudWidget> PartnerRightHUD;

};
