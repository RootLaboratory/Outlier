// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "ProjectionMarkDefinition.h"
#include "TrailEffectDefinition.h"
#include "SoundDefinition.h"
#include "VisualEventType.generated.h"

class UProjectionMarkDefinition;
class UTrailEffectDefinition;
class USoundDefinition;


USTRUCT(BlueprintType)
struct VISUALEVENT_API FVisualEventSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "VisualEvent")
	TObjectPtr< UProjectionMarkDefinition> DecalDef = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VisualEvent")
	TObjectPtr< UTrailEffectDefinition> TrailEffectDef = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VisualEvent")
	TObjectPtr< USoundDefinition> SoundDef = nullptr;

};
