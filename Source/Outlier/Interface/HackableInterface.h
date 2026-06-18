// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "HackableInterface.generated.h"


class UHackableComponent;

// This class does not need to be modified.
UINTERFACE(Blueprintable)
class UHackableInterface : public UInterface
{
	GENERATED_BODY()
};


class OUTLIER_API IHackableInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Hack")
	UHackableComponent* GetHackableComponent() const;
};
