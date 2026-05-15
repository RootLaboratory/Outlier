// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MainUIBase.h"
#include "GangTongMainUI.generated.h"


class UPartnerCamUI;
class UStaticCrossHair;
class UHPBarUI;


UCLASS()
class TAGDRIVENUI_API UGangTongMainUI : public UMainUIBase
{
	GENERATED_BODY()

public:

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
	TObjectPtr<UHPBarUI> PartnerHPUI;

};
