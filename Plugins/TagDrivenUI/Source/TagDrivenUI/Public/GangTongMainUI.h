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

	UPROPERTY(meta = (BindWidget))
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
