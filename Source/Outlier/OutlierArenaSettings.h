// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "OutlierArenaSettings.generated.h"

class UWorld;

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

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Network")
	bool bUseStaticArenaHandoff = false;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Network")
	FString StaticArenaAddress = TEXT("127.0.0.1:7780");

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Network")
	FString LobbyAddress = TEXT("127.0.0.1:7777");

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Network")
	bool bReturnToLobbyOnMatchEnd = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Network", meta = (ClampMin = "0.1"))
	float ArenaWorkerExitTimeoutSeconds = 5.0f;

	FString GetArenaPackageName() const;
	bool MatchesArenaPackageName(const FString& WorldPackageName) const;
	bool IsArenaWorld(const UWorld* World) const;
};
