// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "EventDrivenUI.generated.h"


UCLASS()
class TAGDRIVENUI_API UEventDrivenUI : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void Activate();
	virtual void Deactivate();

protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "UI")
	void HandleActivatedVisual();

	UFUNCTION(BlueprintImplementableEvent, Category = "UI")
	void HandleDeactivatedVisual();

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI", meta = (Categories = "UI.Player.Shooter,UI.Player.Partner"))
	FGameplayTag ModuleTag;
};
