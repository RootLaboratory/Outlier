// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Save/OutlierCheckpointData.h"
#include "Save/OutlierCheckpointSnapshot.h"
#include "OutlierSaveSubSystem.generated.h"

class AOutlierPlayerState;

/**
 * 
 */
UCLASS()
class OUTLIER_API UOutlierSaveSubSystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
	
public:
	bool SavePlayerCheckpoint(const FString& PlayerId, const FOutlierCheckpointData& Data);
	bool LoadPlayerCheckpoint(const FString& PlayerId, FOutlierCheckpointData& OutData) const;

	void ResetRuntimeCheckpointState();
	bool ConfigureNewSave(const FGuid& OwnerId, const FGuid& SaveId, const FString& KeyVerifier);
	bool LoadLatestSave(const FGuid& OwnerId, const FGuid& SaveId, const FString& KeyVerifier);
	bool ValidateResumeKey(const FGuid& OwnerId, const FGuid& SaveId,
		const FGuid& ResumeKey, FString& OutKeyVerifier) const;
	static FString MakeKeyVerifier(const FGuid& ResumeKey);
	FGuid GetActiveSaveId() const { return ActiveSaveId; }
	bool CaptureInitialSnapshot(const FOutlierCheckpointSnapshot& Snapshot);
	bool CommitDurableCheckpointSnapshot(const FOutlierCheckpointSnapshot& Snapshot);
	bool GetRestoreSnapshot(FOutlierCheckpointSnapshot& OutSnapshot) const;
	bool HasInitialSnapshot() const { return bHasInitialSnapshot; }
	bool HasLatestCheckpointSnapshot() const { return bHasLatestCheckpointSnapshot; }
	bool HasCommittedCheckpoint(FName CheckpointId) const;
	// 프리셋/디버그 재로드처럼 새 진행을 시작할 때 비운다. 체크포인트 Actor는 BeginPlay에서 이 목록을 읽는다.
	void ResetCommittedCheckpointIds();

	// 저장 스냅샷과 별개인 현재 매치 상태. 서버 Shooter PS가 확정된 뒤, Gameplay 로드 전에 주입한다.
	void SyncCurrentSuitState(const AOutlierPlayerState* ShooterPlayerState);
	bool IsCurrentSuitAcquired() const { return bCurrentSuitAcquired; }

	bool SetWorldProgressState(EOutlierWorldProgressType Type, FName ProgressId, bool bCompleted);
	bool HasWorldProgress(EOutlierWorldProgressType Type, FName ProgressId) const;
	bool RecordCompletedEncounter(FName EncounterId);
	void RestoreCurrentWorldProgress(const FOutlierWorldProgressSnapshot& Snapshot);
	const FOutlierWorldProgressSnapshot& GetCurrentWorldProgress() const { return CurrentWorldProgress; }
	void SetCurrentRoomPhaseProgress(FGameplayTag RoomTag, const FOutlierRoomPhaseProgress& Progress);
	void ClearCurrentRoomPhaseProgress(FGameplayTag RoomTag);
	void RestoreCurrentRoomPhaseProgress(const TMap<FGameplayTag, FOutlierRoomPhaseProgress>& Progress);
	const TMap<FGameplayTag, FOutlierRoomPhaseProgress>& GetCurrentRoomPhaseProgress() const
	{
		return CurrentRoomPhaseProgress;
	}

	// 배치 터렛 Actor는 사망 후에도 남으므로 월드 진행과 별도로 Stable ID별 사망 자세를 추적한다.
	bool SetDestroyedTurretState(FName TurretId, bool bDestroyed);
	bool IsTurretDestroyed(FName TurretId) const;
	void RestoreCurrentDestroyedTurretIds(const TSet<FName>& DestroyedTurretIds);
	const TSet<FName>& GetCurrentDestroyedTurretIds() const { return CurrentDestroyedTurretIds; }

	bool RegisterWorldProgressId(EOutlierWorldProgressType Type, FName ProgressId, UObject* Owner);
	void UnregisterWorldProgressId(EOutlierWorldProgressType Type, FName ProgressId, const UObject* Owner);
	bool RegisterCheckpointId(FName CheckpointId, UObject* Owner);
	void UnregisterCheckpointId(FName CheckpointId, const UObject* Owner);
	// 사망 상태 저장과 분리해, 배치 시점부터 다른 진행 오브젝트와 Stable ID 중복을 검사한다.
	bool RegisterPersistentTurretId(FName TurretId, UObject* Owner);
	void UnregisterPersistentTurretId(FName TurretId, const UObject* Owner);
	bool HasValidStableIds() const { return bStableIdsValid; }

#if WITH_DEV_AUTOMATION_TESTS
	void SetAutoSaveDirectoryForTesting(const FString& Directory) { AutoSaveDirectoryOverride = Directory; }
	bool CommitCheckpointSnapshotForTesting(const FOutlierCheckpointSnapshot& Snapshot)
	{
		return CommitCheckpointSnapshot(Snapshot);
	}
#endif

private:
	bool CommitCheckpointSnapshot(const FOutlierCheckpointSnapshot& Snapshot);
	bool RegisterStableId(FName StableId, UObject* Owner, const TCHAR* IdKind);
	void UnregisterStableId(FName StableId, const UObject* Owner);

	TMap<FString, FOutlierCheckpointData> RuntimeCheckpointData;

	UPROPERTY(Transient)
	bool bHasInitialSnapshot = false;

	UPROPERTY(Transient)
	FOutlierCheckpointSnapshot InitialSnapshot;

	UPROPERTY(Transient)
	bool bHasLatestCheckpointSnapshot = false;

	UPROPERTY(Transient)
	FOutlierCheckpointSnapshot LatestCheckpointSnapshot;

	UPROPERTY(Transient)
	bool bCurrentSuitAcquired = false;

	UPROPERTY(Transient)
	FOutlierWorldProgressSnapshot CurrentWorldProgress;

	UPROPERTY(Transient)
	TMap<FGameplayTag, FOutlierRoomPhaseProgress> CurrentRoomPhaseProgress;

	UPROPERTY(Transient)
	TSet<FName> CurrentDestroyedTurretIds;

	UPROPERTY(Transient)
	TSet<FName> CommittedCheckpointIds;

	TMap<FName, TWeakObjectPtr<UObject>> RegisteredStableIds;
	bool bStableIdsValid = true;
	FString AutoSaveDirectoryOverride;
	FGuid ActiveOwnerId;
	FGuid ActiveSaveId;
	FString ActiveKeyVerifier;
	FString GetSaveDirectory(const FGuid& SaveId) const;
};
