// Fill out your copyright notice in the Description page of Project Settings.
#include "LocalPlayerUISubSystem.h"
#include "GameFramework/PlayerController.h"
#include "EventDrivenUI.h"


void ULocalPlayerUISubSystem::EventCall(const FGameplayTag& EventTag)
{
	if (EventModuleUIMap.Contains(EventTag))
	{
		//EventModuleUIMap[EventTag]->EventCall();
	}
}

void ULocalPlayerUISubSystem::HUDActivate(bool bShouldActivate)
{
	APlayerController* PC = GetLocalPlayer()->GetPlayerController(GetWorld());
	if (PC)
	{
		//참조중인 UMainUIBase->ModuleActivate(bShouldActivate);
	
	}
}
