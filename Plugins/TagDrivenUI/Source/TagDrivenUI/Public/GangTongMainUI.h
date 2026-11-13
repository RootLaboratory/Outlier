// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MainUIBase.h"
#include "GangTongMainUI.generated.h"


class UPartnerCamUI;
class UStaticCrossHair;
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
	virtual void ModulesControl(bool Flag) override;
	// 다운 상태를 Left / Right HUD 의 Grayout 과 Partner Image 연출로 전달하고, 크로스헤어를 숨긴다.
	void SetKnockedDown(bool bInKnockedDown);
	// StartWorldTime 은 World->GetTimeSeconds 기준. Duration 이 0 이면 Progress 0(회색 + 최대 왜곡)으로 유지한다.
	void SetKnockDownTiming(float StartWorldTime, float Duration);

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_Hud;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UStaticCrossHair> CrossHairUI;

private:
	void RefreshKnockDownVisibility();
	bool bKnockedDown = false;

	// 다운 중 HUD 회색 정도. Left / Right Grayout Retainer 와 Partner Image 시작 상태에 같은 값이 들어간다.
	UPROPERTY(EditDefaultsOnly, Category = "KnockDown", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float KnockDownGrayoutAmount = 1.0f;

	// Main은 컨테이너까지만 직접 바인딩하고, 컨테이너가 내부 모듈을 소유한다.
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPartnerLeftHudWidget> PartnerLeftHUD;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPartnerRightHudWidget> PartnerRightHUD;

};
