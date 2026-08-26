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
	bool bUseStaticArenaHandoff = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Network")
	FString StaticArenaAddress = TEXT("127.0.0.1:7780");

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Network|Process")
	bool bUseProcessManager = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Network|Process")
	bool bUseArenaControlChannel = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Network|Process")
	FString ArenaWorkerHost = TEXT("127.0.0.1");

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Network|Process", meta = (ClampMin = "1", ClampMax = "16"))
	int32 StaticArenaSlots = 4;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Network|Process", meta = (ClampMin = "1", ClampMax = "65535"))
	int32 ArenaBasePort = 7780;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Network|Process", meta = (ClampMin = "1", ClampMax = "65535"))
	int32 ArenaControlPort = 7790;

	// 비어 있으면 현재 Lobby 실행 파일을 사용한다. Editor에서는 프로젝트 경로를 인자로 함께 전달한다.
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Network|Process")
	FString ArenaWorkerExecutablePath;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Network|Process", meta = (ClampMin = "1.0"))
	float ArenaWorkerReadyTimeoutSeconds = 60.0f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Network|Process", meta = (ClampMin = "0.1"))
	float ArenaWorkerRestartDelaySeconds = 2.0f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Network|Process", meta = (ClampMin = "0", ClampMax = "10"))
	int32 MaxArenaWorkerRestartAttempts = 3;

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
