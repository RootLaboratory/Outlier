// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"


#include "EventDrivenUI.generated.h"

/**
 * 
 */

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnEventTriggered);

UCLASS()
class TAGDRIVENUI_API UEventDrivenUI : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void EventCall();
	virtual void EventBind(const FGameplayTag& InTag);

	const FGameplayTag& GetPlayTag();
	void SetPlayTag(const FGameplayTag& InTag);

protected:
	FGameplayTag EventTag;

	UPROPERTY()
	FOnEventTriggered OnEventTriggered;
};
