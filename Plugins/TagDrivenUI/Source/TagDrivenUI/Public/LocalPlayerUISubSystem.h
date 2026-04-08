// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "GameplayTagContainer.h"
#include "LocalPlayerUISubSystem.generated.h"


class UEventDrivenUI;

UCLASS()
class TAGDRIVENUI_API ULocalPlayerUISubSystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()
	
public:

	void EventCall(const FGameplayTag& EventTag);
	void HUDActivate(bool bShouldActivate); 

private:
	UPROPERTY()
	TMap<FGameplayTag, TObjectPtr<UEventDrivenUI>> EventModuleUIMap;	
};

