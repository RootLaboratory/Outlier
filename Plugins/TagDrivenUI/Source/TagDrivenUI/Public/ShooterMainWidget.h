// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "MainUIBase.h"

#include "ShooterMainWidget.generated.h"

class UPartnerCamUI;
class UHPBarUI;
class UAmmoUI;
class UCrossHairBase;
class UShooterCurrentAbilityIcon;
class UShooterCurrentWeaponIcon;

UENUM(BlueprintType)
enum class EWidgetWeaponType : uint8
{
	Unarmed,
	Pistol,
	Rifle,
	Melee
};

UCLASS(Blueprintable)
class TAGDRIVENUI_API UShooterMainWidget : public UMainUIBase
{
	GENERATED_BODY()
public:

	virtual void NativeConstruct() override;

	//그 Construct 키워드 함수 사용 
	virtual void ModuleInit() override;
	virtual void ModuleDestruct() override;

	virtual void ModuleActivate() override;
	virtual void ModuleDeActivate() override;

public:

	void DefaultModuleInit(); //초기에는 아무것도 갖고 있지 않음.Wrapping
	void SuitOnModuleInit(); // Suit 착용 시, 활성화 되어야 하는 모듈 상태 초기화

	void OnChangeWeapon(EWidgetWeaponType Type);

public:

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPartnerCamUI> PartnerCamUI;
	
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHPBarUI> HPBarUI;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UAmmoUI> AmmoUI;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCrossHairBase> RifleCrossHairUI;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCrossHairBase> PistolCrossHairUI;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UShooterCurrentAbilityIcon> CurrentAbilityUI;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UShooterCurrentWeaponIcon> CurrentWeaponUI;

private:
	TObjectPtr<UCrossHairBase> CurrentCrossHairUI;
};
