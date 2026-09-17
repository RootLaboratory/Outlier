// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Save/OutlierCheckpointData.h"
#include "Save/OutlierCheckpointSnapshot.h"
#include "OutlierSaveSubSystem.generated.h"

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
	bool CaptureInitialSnapshot(const FOutlierCheckpointSnapshot& Snapshot);
	bool CommitCheckpointSnapshot(const FOutlierCheckpointSnapshot& Snapshot);
	bool GetRestoreSnapshot(FOutlierCheckpointSnapshot& OutSnapshot) const;
	bool HasInitialSnapshot() const { return bHasInitialSnapshot; }
	bool HasLatestCheckpointSnapshot() const { return bHasLatestCheckpointSnapshot; }
	bool HasCommittedCheckpoint(FName CheckpointId) const;

	bool SetWorldProgressState(EOutlierWorldProgressType Type, FName ProgressId, bool bCompleted);
	bool HasWorldProgress(EOutlierWorldProgressType Type, FName ProgressId) const;
	bool RecordCompletedEncounter(FName EncounterId);
	void RestoreCurrentWorldProgress(const FOutlierWorldProgressSnapshot& Snapshot);
	const FOutlierWorldProgressSnapshot& GetCurrentWorldProgress() const { return CurrentWorldProgress; }

	bool RegisterWorldProgressId(EOutlierWorldProgressType Type, FName ProgressId, UObject* Owner);
	void UnregisterWorldProgressId(EOutlierWorldProgressType Type, FName ProgressId, const UObject* Owner);
	bool RegisterCheckpointId(FName CheckpointId, UObject* Owner);
	void UnregisterCheckpointId(FName CheckpointId, const UObject* Owner);
	bool HasValidStableIds() const { return bStableIdsValid; }

private:
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
	FOutlierWorldProgressSnapshot CurrentWorldProgress;

	UPROPERTY(Transient)
	TSet<FName> CommittedCheckpointIds;

	TMap<FName, TWeakObjectPtr<UObject>> RegisteredStableIds;
	bool bStableIdsValid = true;
};
