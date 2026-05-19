// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "OutlierArenaSettings.generated.h"

/**
 * 
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Outlier Arena"))
class OUTLIER_API UOutlierArenaSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly)
	TSoftObjectPtr<UWorld> ArenaLevel;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly)
	int32 MaxArenaCount = 8;
};
