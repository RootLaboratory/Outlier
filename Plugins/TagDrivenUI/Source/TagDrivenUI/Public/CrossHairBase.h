// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EventDrivenUI.h"
#include "CrossHairBase.generated.h"

/**
 * 
 */

UCLASS(Blueprintable)
class TAGDRIVENUI_API UCrossHairBase : public UEventDrivenUI
{
	GENERATED_BODY()


protected:
	float Duration =0;


};
