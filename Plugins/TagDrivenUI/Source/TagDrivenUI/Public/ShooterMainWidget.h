// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "MainUIBase.h"

#include "EventDrivenUI.h"

#include "ShooterMainWidget.generated.h"

class UPartnerCamUI;
class UHPBarUI;
class UAmmoUI;


UCLASS(Blueprintable)
class TAGDRIVENUI_API UShooterMainWidget : public UMainUIBase
{
	GENERATED_BODY()
public:

	virtual void ModuleInit() override;
	virtual void ModuleDestruct() override;
	virtual void ModuleActivate(bool bFlag) override;

public:


	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPartnerCamUI> PartnerCameraUI;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHPBarUI> HPBarUI;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UAmmoUI> AmmoUI;
};

//