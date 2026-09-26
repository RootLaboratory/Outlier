// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MainUIBase.h"
#include "GangTongMainUI.generated.h"


class UPartnerCamUI;
class UStaticCrossHair;
class UAbilityIconUI;
class UDamageFeedBackWidget;
class UPartnerLeftHudWidget;
class UPartnerRightHudWidget;
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

	virtual void On_RepAbilityDisabledByDistance() override;
	virtual void On_RepAbilityabledByDistance() override;

public:
	// Shooter 가 슈트를 입기 전에는 Partner HUD 전체(모듈 + 능력 아이콘)를 숨긴다.
	// Partner 클라는 Shooter 를 로컬 컨트롤하지 않아 Shooter 쪽 UI 갱신 경로가 막히므로,
	// PlayerState 의 획득 플래그가 복제돼 올 때 UI 서브시스템이 이걸 호출한다.
	void SetSuitGatedModulesEnabled(bool bEnabled);

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

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UDamageFeedBackWidget> DamageFeedbackUI;

private:
	void CacheNestedHudWidgets();

	// Main은 컨테이너까지만 직접 바인딩하고, 컨테이너가 내부 모듈을 소유한다.
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPartnerLeftHudWidget> PartnerLeftHUD;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPartnerRightHudWidget> PartnerRightHUD;

};
