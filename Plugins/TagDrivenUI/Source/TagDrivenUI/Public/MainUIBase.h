// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MainUIBase.generated.h"

/**
 * 
 */
UENUM()
enum class EUIModule : uint8
{
	HP,
	PartnerCam,
	Ammo,
	Suit,
	FirstSkill,
	SecondSkill,
	ThirdSkill,
	CrossHair,

	None
};

class UEventDrivenUI;



UCLASS()
class TAGDRIVENUI_API UMainUIBase : public UUserWidget
{
	GENERATED_BODY()

public:

	UEventDrivenUI* GetModule(EUIModule Key);

	virtual void ModuleInit() {}
	virtual void ModuleDestruct() {}
	virtual void ModuleActivate() {}
	virtual void ModuleDeActivate() {}


protected:
	UPROPERTY()
	TMap< EUIModule, UEventDrivenUI*> Module;
};
