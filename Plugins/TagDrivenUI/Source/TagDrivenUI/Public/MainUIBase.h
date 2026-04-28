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

// UI 전용 Player State
// Controller에 BroadCast 받아놓아서, Sub에 처리하도록 함. 
UENUM(BlueprintType)
enum class EUIPlayerState : uint8
{
	Idle,
	Move,
	Jump,
	Slide
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
