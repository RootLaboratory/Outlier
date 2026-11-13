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
	virtual FVector GetHackTargetLocation() const;
	static FVector ResolveHackTargetLocation(const AActor* Actor);
	virtual void HandleHackStarted(const FHackQueryContext& Context) {}
	virtual void HandleHackCompleted(const FHackResultContext& Context) {}
	// 상태 진입 이후 효과를 확정하는 대상은 완료 통지까지 직접 소유한다.
	virtual bool DeferHackCompletion(const FHackResultContext& Context,
		TFunction<void(const FHackResultContext&)> Completion) { return false; }
	virtual void HandleHackEffect(FGameplayTag EffectTag, const FHackResultContext& Context) = 0;
};
