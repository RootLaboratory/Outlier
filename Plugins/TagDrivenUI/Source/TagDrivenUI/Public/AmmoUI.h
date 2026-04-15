// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EventDrivenUI.h"
#include "AmmoUI.generated.h"

/**
 * 
 */
UCLASS(Blueprintable)
class TAGDRIVENUI_API UAmmoUI : public UEventDrivenUI
{
	GENERATED_BODY()
	
public:

	UFUNCTION(BlueprintNativeEvent, Category = "UI")
	void AmmoCountChanged(int32 InAmmoCount);

public:
	UPROPERTY(BlueprintReadOnly, Category = "Data")
	int Temp_AmmoCount = 40;

};
