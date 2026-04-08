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
	virtual void EventCall() override;
	virtual void EventBind(const FGameplayTag& InTag) override;

	UFUNCTION(BlueprintImplementableEvent)
	void OnDamaged();
};
