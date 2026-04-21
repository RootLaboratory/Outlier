// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "VisualEventType.h"
#include "VisualEffectProvider.generated.h"

// This class does not need to be modified.
UINTERFACE(MinimalAPI)
class UVisualEffectProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */

//이펙트나 에셋 호출이 정적인 지형을 한정해서 일단 인터페이스 구조를 만듦.
class VISUALEVENT_API IVisualEffectProvider
{
	GENERATED_BODY()

public:
	FVisualEventSet VisualEventSet;

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	FVisualEventSet GetVisualEventSet() const;
};
