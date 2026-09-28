// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "DamageFeedbackIndicator.h"
#include "RedDamageFeedbackWidget.generated.h"

class UImage;

/** Red hit direction indicator. Pooled by UDamageFeedbackLayer. */
UCLASS(Abstract, Blueprintable, meta = (DisableNativeTick))
class TAGDRIVENUI_API URedDamageFeedbackWidget : public UDamageFeedbackIndicator
{
	GENERATED_BODY()

protected:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> RedFeedback;
};
