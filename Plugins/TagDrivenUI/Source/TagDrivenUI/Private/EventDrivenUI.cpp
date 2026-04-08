// Fill out your copyright notice in the Description page of Project Settings.


#include "EventDrivenUI.h"



void UEventDrivenUI::EventCall()
{
}

void UEventDrivenUI::EventBind(const FGameplayTag& InTag)
{
	EventTag = InTag;

}

const FGameplayTag& UEventDrivenUI::GetPlayTag()
{
	return EventTag;
}

void UEventDrivenUI::SetPlayTag(const FGameplayTag& InTag)
{
	EventTag = InTag;
}
