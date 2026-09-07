// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "OutlierArenaInstance.h"
#include "OutlierArenaPoolSubsystem.generated.h"

class ULevelStreamingDynamic;
class ULevel;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnArenaShown, int32 /*ArenaId*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnArenaReleased, int32 /*ArenaId*/);

/**
 * 
 */
UCLASS()
class OUTLIER_API UOutlierArenaPoolSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
	
public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	FOutlierArenaInstance* AcquireArena();
	void ReleaseArena(int32 ArenaId);
	void ReloadArena(int32 ArenaId); // 디버그: 페어링(bInUse/PairId) 보존한 채 제자리 재스트리밍
	ULevel* GetArenaLoadedLevel(int32 ArenaId) const;
	bool IsPersistentArenaWorld() const;
	void EnsureArenaLoaded(int32 ArenaId, bool bForceReload = false); // Client는 id 에 해당하는 arena만 load
	bool IsArenaReady(int32 ArenaId) const;
	bool IsStreamingArenaReady(const ULevelStreamingDynamic* StreamingLevel);
	FOnArenaShown OnArenaShown;
	FOnArenaReleased OnArenaReleased;

private:
	void PreloadArenas();
	ULevelStreamingDynamic* LoadArenaLevelInstance(int32, const FTransform& InstanceTransform);
	void RefreshArenaReadyStates(const TCHAR* Reason);

	// 지연 재로드: 같은 인스턴스/이름 유지한 채 언로드 완료를 기다렸다 다시 로드 (리플리케이션 매칭 유지)
	void BeginDeferredReload(ULevelStreamingDynamic* StreamingLevel);
	void TickPendingReloads();

	TArray<TWeakObjectPtr<ULevelStreamingDynamic>> PendingReloadLevels;
	FTimerHandle ReloadPollTimer;

	UFUNCTION()
	void HandleArenaLevelLoaded();

	UFUNCTION()
	void HandleArenaLevelShown();


	UPROPERTY()
	TArray<FOutlierArenaInstance> Arenas;

	UPROPERTY(EditDefaultsOnly, Category = "Arena")
	TSoftObjectPtr<UWorld> ArenaLevel;

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arena")
	int32 MaxArenaCount = 1;
};
