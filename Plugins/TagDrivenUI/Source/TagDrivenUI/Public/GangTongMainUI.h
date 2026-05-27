// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MainUIBase.h"
#include "GangTongMainUI.generated.h"


class UPartnerCamUI;
class UStaticCrossHair;
class UUserWidget;
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



public:

	// HP; Material; enum으로 상태도 받아야 하고->Client 쪽에서 상태 처리
	//
	// Skill Cool Time - 이미 있고 Key bind
	//
	// Distance; 클래스 만들고, Set; slide로 비율도 처리. 최대 거리.

private:
	virtual void NativeConstruct() override;

	virtual void ModuleInit() override;
	virtual void ModuleDestruct() override;

	virtual void ModuleActivate() override;
	virtual void ModuleDeActivate() override;

public:

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPartnerCamUI> PartnerCamUI;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UStaticCrossHair> CrossHairUI;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPartnerHPUI> PartnerHPUI;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UUserWidget> AbilityShieldIcon;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UUserWidget> AbilityHackingIcon;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UUserWidget> AbilityEMPIcon;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UDistanceSlideUI> DistanceSlide;

};
