// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "CrossHairBase.h"
#include "StaticCrossHair.generated.h"

/**
 * 
 */
UCLASS()
class TAGDRIVENUI_API UStaticCrossHair : public UCrossHairBase
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintNativeEvent, Category = "UI")
	void SpawnReloadingTimer();


};
