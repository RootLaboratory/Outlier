// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "OutlierArenaSettings.generated.h"

class UWorld;
class UDataLayerAsset;
class UDataTable;

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

	// Arena LevelInstance는 유지하고, 이 Runtime Data Layer만 내려서 gameplay 액터를 재생성한다.
	// 비어 있으면 기존 전체 Arena reload 경로를 사용한다.
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Arena|World Partition")
	TSoftObjectPtr<UDataLayerAsset> GameplayDataLayer;

	// 프리셋 스테이지별 업그레이드 노드 지급량(FPresetNodeProvideRow). RowName은
	// OutlierPresetStageIds.h의 상수와 같아야 한다.
	//
	// 원래 GameMode의 EditDefaultsOnly 프로퍼티였는데, AOutlierGameMode 파생 BP가 3개라
	// (BP_OutlierGM / BP_LobbyGM / BP_BTestGM) 어느 BP가 뜨느냐에 따라 값이 있기도 없기도 했다.
	// 특히 Listen은 Title 맵을 열고 아레나를 스트리밍으로 얹는 구조라 GameMode가 BP_LobbyGM이고,
	// 아레나 맵(WP_Test)의 World Settings에 꽂아둔 값은 dedi 워커에서만 쓰인다.
	// 실행 형태에 따라 조용히 0이 되는 걸 막으려고 프로젝트 설정으로 올렸다.
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Arena|Respawn")
	TSoftObjectPtr<UDataTable> PresetNodeProvideTable;

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

	// Worker가 Lobby로 Heartbeat를 보내는 주기.
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Network|Process", meta = (ClampMin = "0.5"))
	float ArenaWorkerHeartbeatIntervalSeconds = 2.0f;

	// 이 시간 동안 Lobby 응답이 없으면 Worker는 고아로 판단하고 스스로 종료한다.
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Network|Process", meta = (ClampMin = "1.0"))
	float ArenaWorkerHeartbeatTimeoutSeconds = 15.0f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Network")
	FString LobbyAddress = TEXT("127.0.0.1:7777");

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Network")
	bool bReturnToLobbyOnMatchEnd = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Network", meta = (ClampMin = "0.1"))
	float ArenaWorkerExitTimeoutSeconds = 5.0f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Network", meta = (ClampMin = "0.0"))
	float ArenaMatchStartDelaySeconds = 1.0f;

	FString GetArenaPackageName() const;
	bool MatchesArenaPackageName(const FString& WorldPackageName) const;
	bool IsArenaWorld(const UWorld* World) const;
	bool ShouldUseExternalArenaHandoff(ENetMode NetMode) const;
};
