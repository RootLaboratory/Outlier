// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Drone/Partner/HackType.h"
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
	virtual UHackableComponent* GetHackableComponent() const = 0;
	virtual void HandleHackStarted(const FHackQueryContext& Context) {}
	virtual void HandleHackCompleted(const FHackResultContext& Context) {}
	virtual void HandleHackEffect(FGameplayTag EffectTag, const FHackResultContext& Context) = 0;
};
