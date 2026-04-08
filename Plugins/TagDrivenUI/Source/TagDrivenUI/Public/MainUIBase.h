// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MainUIBase.generated.h"

/**
 * 
 */
UCLASS()
class TAGDRIVENUI_API UMainUIBase : public UUserWidget
{
	GENERATED_BODY()
protected:
	virtual void ModuleInit() {}
	virtual void ModuleDestruct() {}
	virtual void ModuleActivate(bool bFlag) {}
};
