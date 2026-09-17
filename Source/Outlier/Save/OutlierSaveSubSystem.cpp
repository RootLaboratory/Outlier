// Fill out your copyright notice in the Description page of Project Settings.


#include "Save/OutlierSaveSubSystem.h"

bool UOutlierSaveSubSystem::SavePlayerCheckpoint(const FString& PlayerId, const FOutlierCheckpointData& Data)
{
	if (PlayerId.IsEmpty() || !Data.IsValid())
	{
		return false;
	}

	RuntimeCheckpointData.Add(PlayerId, Data);

	// 나중에 서버 SaveGame 파일 저장
	// 싱글이면 로컬 SaveGame + HMAC + Backup 처리

	return true;
}

bool UOutlierSaveSubSystem::LoadPlayerCheckpoint(const FString& PlayerId, FOutlierCheckpointData& OutData) const
{
	if (const FOutlierCheckpointData* FoundData = RuntimeCheckpointData.Find(PlayerId))
	{
		OutData = *FoundData;
		return FoundData->IsValid();
	}

	return false;
}

void UOutlierSaveSubSystem::ResetRuntimeCheckpointState()
{
	// 월드 Actor는 페어 시작 전에 BeginPlay에서 ID를 등록할 수 있으므로
	// 매치 스냅샷만 비우고 ID 레지스트리와 검증 결과는 유지한다.
	RuntimeCheckpointData.Reset();
	bHasInitialSnapshot = false;
	InitialSnapshot = FOutlierCheckpointSnapshot();
	bHasLatestCheckpointSnapshot = false;
	LatestCheckpointSnapshot = FOutlierCheckpointSnapshot();
	CurrentWorldProgress.Reset();
	CommittedCheckpointIds.Reset();
}

bool UOutlierSaveSubSystem::CaptureInitialSnapshot(const FOutlierCheckpointSnapshot& Snapshot)
{
	if (bHasInitialSnapshot || !Snapshot.IsValid() || !Snapshot.bInitialSnapshot)
	{
		return false;
	}

	InitialSnapshot = Snapshot;
	bHasInitialSnapshot = true;
	return true;
}

bool UOutlierSaveSubSystem::CommitCheckpointSnapshot(const FOutlierCheckpointSnapshot& Snapshot)
{
	if (!Snapshot.IsValid() || Snapshot.bInitialSnapshot || Snapshot.CheckpointId.IsNone()
		|| CommittedCheckpointIds.Contains(Snapshot.CheckpointId)
		|| !bStableIdsValid)
	{
		return false;
	}

	LatestCheckpointSnapshot = Snapshot;
	bHasLatestCheckpointSnapshot = true;
	CommittedCheckpointIds.Add(Snapshot.CheckpointId);
	return true;
}

bool UOutlierSaveSubSystem::GetRestoreSnapshot(FOutlierCheckpointSnapshot& OutSnapshot) const
{
	if (bHasLatestCheckpointSnapshot)
	{
		OutSnapshot = LatestCheckpointSnapshot;
		return true;
	}
	if (bHasInitialSnapshot)
	{
		OutSnapshot = InitialSnapshot;
		return true;
	}
	return false;
}

bool UOutlierSaveSubSystem::HasCommittedCheckpoint(FName CheckpointId) const
{
	return !CheckpointId.IsNone() && CommittedCheckpointIds.Contains(CheckpointId);
}

bool UOutlierSaveSubSystem::SetWorldProgressState(
	EOutlierWorldProgressType Type,
	FName ProgressId,
	bool bCompleted)
{
	if (ProgressId.IsNone())
	{
		return false;
	}

	TSet<FName>& Ids = CurrentWorldProgress.GetIds(Type);
	if (bCompleted)
	{
		Ids.Add(ProgressId);
	}
	else
	{
		Ids.Remove(ProgressId);
	}
	return true;
}

bool UOutlierSaveSubSystem::HasWorldProgress(
	EOutlierWorldProgressType Type,
	FName ProgressId) const
{
	return !ProgressId.IsNone() && CurrentWorldProgress.GetIds(Type).Contains(ProgressId);
}

bool UOutlierSaveSubSystem::RecordCompletedEncounter(FName EncounterId)
{
	return SetWorldProgressState(
		EOutlierWorldProgressType::CompletedEncounter,
		EncounterId,
		true);
}

void UOutlierSaveSubSystem::RestoreCurrentWorldProgress(
	const FOutlierWorldProgressSnapshot& Snapshot)
{
	CurrentWorldProgress = Snapshot;
}

bool UOutlierSaveSubSystem::RegisterWorldProgressId(
	EOutlierWorldProgressType Type,
	FName ProgressId,
	UObject* Owner)
{
	const FString IdKind = FString::Printf(TEXT("WorldProgress.%d"), static_cast<int32>(Type));
	return RegisterStableId(ProgressId, Owner, *IdKind);
}

void UOutlierSaveSubSystem::UnregisterWorldProgressId(
	EOutlierWorldProgressType Type,
	FName ProgressId,
	const UObject* Owner)
{
	if (ProgressId.IsNone() || !Owner)
	{
		return;
	}

	(void)Type;
	UnregisterStableId(ProgressId, Owner);
}

bool UOutlierSaveSubSystem::RegisterCheckpointId(FName CheckpointId, UObject* Owner)
{
	return RegisterStableId(CheckpointId, Owner, TEXT("Checkpoint"));
}

void UOutlierSaveSubSystem::UnregisterCheckpointId(FName CheckpointId, const UObject* Owner)
{
	UnregisterStableId(CheckpointId, Owner);
}

bool UOutlierSaveSubSystem::RegisterStableId(
	FName StableId,
	UObject* Owner,
	const TCHAR* IdKind)
{
	if (StableId.IsNone() || !IsValid(Owner))
	{
		bStableIdsValid = false;
		UE_LOG(LogTemp, Error,
			TEXT("[Checkpoint] Invalid stable Id Kind=%s Id=%s Owner=%s"),
			IdKind,
			*StableId.ToString(),
			*GetNameSafe(Owner));
		return false;
	}

	if (const TWeakObjectPtr<UObject>* ExistingOwner = RegisteredStableIds.Find(StableId))
	{
		if (ExistingOwner->IsValid() && ExistingOwner->Get() != Owner)
		{
			bStableIdsValid = false;
			UE_LOG(LogTemp, Error,
				TEXT("[Checkpoint] Duplicate stable Id Kind=%s Id=%s Existing=%s New=%s"),
				IdKind,
				*StableId.ToString(),
				*GetNameSafe(ExistingOwner->Get()),
				*GetNameSafe(Owner));
			return false;
		}
	}

	RegisteredStableIds.Add(StableId, Owner);
	return true;
}

void UOutlierSaveSubSystem::UnregisterStableId(FName StableId, const UObject* Owner)
{
	if (StableId.IsNone() || !Owner)
	{
		return;
	}
	if (const TWeakObjectPtr<UObject>* ExistingOwner = RegisteredStableIds.Find(StableId);
		ExistingOwner && ExistingOwner->Get() == Owner)
	{
		RegisteredStableIds.Remove(StableId);
	}
}
