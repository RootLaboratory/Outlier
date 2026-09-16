// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MainUIBase.h"
#include "GangTongMainUI.generated.h"


class UPartnerCamUI;
class UStaticCrossHair;
class UAbilityIconUI;
class UDistanceSlideUI;
class UPartnerHPUI;
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

	virtual void ModuleActivate() override;
	virtual void ModuleDeActivate() override;


	virtual void On_RepAbilityDisabledByDistance() override;
	virtual void On_RepAbilityabledByDistance() override;

public:
	// Shooter 가 슈트를 입기 전에는 Partner HUD 전체(모듈 + 능력 아이콘)를 숨긴다.
	// Partner 클라는 Shooter 를 로컬 컨트롤하지 않아 Shooter 쪽 UI 갱신 경로가 막히므로,
	// PlayerState 의 획득 플래그가 복제돼 올 때 UI 서브시스템이 이걸 호출한다.
	void SetSuitGatedModulesEnabled(bool bEnabled);


	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UPartnerCamUI> PartnerCamUI;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UStaticCrossHair> CrossHairUI;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPartnerHPUI> PartnerHPUI;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UAbilityIconUI> AbilityShieldIcon;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UAbilityIconUI> AbilityHackingIcon;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UAbilityIconUI> AbilityScanIcon;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UAbilityIconUI> AbilityEMPIcon;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UDistanceSlideUI> DistanceSlide;



};
