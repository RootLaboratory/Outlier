// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "PlayerUIProvider.generated.h"

class UMainUIBase;


UINTERFACE(Blueprintable)
class UPlayerUIProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */


class TAGDRIVENUI_API IPlayerUIProvider
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "UI")
	TSubclassOf<UMainUIBase> GetMainUIClass() const;
};
