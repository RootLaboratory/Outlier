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
	// Shooter 가 슈트를 입기 전에는 Partner HUD 전체(모듈 + 능력 아이콘)를 숨긴다.
	// Partner 클라는 Shooter 를 로컬 컨트롤하지 않아 Shooter 쪽 UI 갱신 경로가 막히므로,
	// PlayerState 의 획득 플래그가 복제돼 올 때 UI 서브시스템이 이걸 호출한다.
	void SetSuitGatedModulesEnabled(bool bEnabled);

	// 최초 HUD 초기화에서 Image_Hud만 표시한다.
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void InitHudImageOnly();

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

	bool bHudImageInitialized = false;

	// 최초 이미지 표시 때문에 숨긴 위젯의 상태는 일반 HUD가 켜질 때 복원한다.
	UPROPERTY(Transient)
	TMap<TObjectPtr<UWidget>, ESlateVisibility> InitialHudWidgetVisibilities;

	// Main은 컨테이너까지만 직접 바인딩하고, 컨테이너가 내부 모듈을 소유한다.
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPartnerLeftHudWidget> PartnerLeftHUD;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPartnerRightHudWidget> PartnerRightHUD;

};
