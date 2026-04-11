// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EventDrivenUI.h"
#include "HPBarUI.generated.h"


/**
 * 
 */
UCLASS(Blueprintable)
class TAGDRIVENUI_API UHPBarUI : public UEventDrivenUI
{
	GENERATED_BODY()
	
public:


	UFUNCTION(BlueprintNativeEvent, Category = "UI")
	void ShieldChanged(float InShieldRatio);
	UFUNCTION(BlueprintNativeEvent, Category = "UI")
	void HealthChanged(float InHealthRatio);


public:
	UPROPERTY(BlueprintReadWrite, Category = "Data")
	float CurrentHPRatio =1;
	UPROPERTY(BlueprintReadWrite, Category = "Data")
	float CurrentShieldRatio=1 ;

	
};
