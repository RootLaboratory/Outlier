// Fill out your copyright notice in the Description page of Project Settings.


#include "Save/OutlierSaveSubSystem.h"
#include "OutlierPlayerState.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Crc.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

namespace
{
	constexpr uint32 AutoSaveMagic = 0x4F55544C;
	constexpr int32 AutoSaveVersion = 2;
	constexpr int64 MaxAutoSaveBytes = 16 * 1024 * 1024;

	struct FAutoSaveFile
	{
		FGuid OwnerId;
		FGuid SaveId;
		FString KeyVerifier;
		FString MapName;
		FOutlierCheckpointSnapshot Snapshot;
	};

	bool ReadAutoSaveFile(const FString& Path, FAutoSaveFile& OutFile)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path) || Bytes.IsEmpty()
			|| Bytes.Num() > MaxAutoSaveBytes)
		{
			return false;
		}

		FMemoryReader Reader(Bytes, true);
		uint32 Magic = 0;
		int32 Version = 0;
		Reader << Magic;
		Reader << Version;
		if (Reader.IsError() || Magic != AutoSaveMagic || Version != AutoSaveVersion)
		{
			return false;
		}
		Reader << OutFile.OwnerId;
		Reader << OutFile.SaveId;
		Reader << OutFile.KeyVerifier;
		Reader << OutFile.MapName;
		int32 PayloadSize = 0;
		uint32 PayloadCrc = 0;
		Reader << PayloadSize;
		Reader << PayloadCrc;
		if (Reader.IsError() || !OutFile.OwnerId.IsValid() || !OutFile.SaveId.IsValid()
			|| OutFile.KeyVerifier.Len() != 40 || OutFile.MapName.IsEmpty()
			|| PayloadSize <= 0 || PayloadSize != Reader.TotalSize() - Reader.Tell())
		{
			return false;
		}

		TArray<uint8> Payload;
		Payload.SetNumUninitialized(PayloadSize);
		Reader.Serialize(Payload.GetData(), PayloadSize);
		if (Reader.IsError() || FCrc::MemCrc32(Payload.GetData(), Payload.Num()) != PayloadCrc)
		{
			return false;
		}
		FMemoryReader PayloadReader(Payload, true);
		FObjectAndNameAsStringProxyArchive ObjectReader(PayloadReader, true);
		FOutlierCheckpointSnapshot::StaticStruct()->SerializeItem(
			ObjectReader, &OutFile.Snapshot, nullptr);
		return !PayloadReader.IsError() && PayloadReader.AtEnd()
			&& OutFile.Snapshot.IsValid() && !OutFile.Snapshot.bInitialSnapshot;
	}
}

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
	bCurrentSuitAcquired = false;
	CurrentWorldProgress.Reset();
	CurrentRoomPhaseProgress.Reset();
	CurrentDestroyedTurretIds.Reset();
	CommittedCheckpointIds.Reset();
	ActiveOwnerId.Invalidate();
	ActiveSaveId.Invalidate();
	ActiveKeyVerifier.Reset();
}

FString UOutlierSaveSubSystem::MakeKeyVerifier(const FGuid& ResumeKey)
{
	if (!ResumeKey.IsValid())
	{
		return FString();
	}
	const FString KeyText = ResumeKey.ToString(EGuidFormats::Digits);
	FTCHARToUTF8 KeyUtf8(*KeyText);
	uint8 Hash[FSHA1::DigestSize];
	FSHA1::HashBuffer(KeyUtf8.Get(), KeyUtf8.Length(), Hash);
	return BytesToHex(Hash, UE_ARRAY_COUNT(Hash));
}

FString UOutlierSaveSubSystem::GetSaveDirectory(const FGuid& SaveId) const
{
	const FString Root = AutoSaveDirectoryOverride.IsEmpty()
		? FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Checkpoint"))
		: AutoSaveDirectoryOverride;
	return FPaths::Combine(Root, SaveId.ToString(EGuidFormats::Digits));
}

bool UOutlierSaveSubSystem::ConfigureNewSave(
	const FGuid& OwnerId, const FGuid& SaveId, const FString& KeyVerifier)
{
	if (!OwnerId.IsValid() || !SaveId.IsValid() || KeyVerifier.Len() != 40)
	{
		return false;
	}
	ActiveOwnerId = OwnerId;
	ActiveSaveId = SaveId;
	ActiveKeyVerifier = KeyVerifier;
	return true;
}

bool UOutlierSaveSubSystem::ValidateResumeKey(
	const FGuid& OwnerId, const FGuid& SaveId,
	const FGuid& ResumeKey, FString& OutKeyVerifier) const
{
	OutKeyVerifier.Reset();
	const FString Verifier = MakeKeyVerifier(ResumeKey);
	if (!OwnerId.IsValid() || !SaveId.IsValid() || Verifier.IsEmpty())
	{
		return false;
	}
	FAutoSaveFile File;
	const FString Directory = GetSaveDirectory(SaveId);
	const FString Latest = FPaths::Combine(Directory, TEXT("LatestAutoSave.sav"));
	const FString Backup = Latest + TEXT(".bak");
	const bool bLoaded = ReadAutoSaveFile(Latest, File)
		|| ReadAutoSaveFile(Backup, File);
	if (!bLoaded || File.OwnerId != OwnerId || File.SaveId != SaveId
		|| File.KeyVerifier != Verifier)
	{
		return false;
	}
	OutKeyVerifier = Verifier;
	return true;
}

bool UOutlierSaveSubSystem::LoadLatestSave(
	const FGuid& OwnerId, const FGuid& SaveId, const FString& KeyVerifier)
{
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World || !OwnerId.IsValid() || !SaveId.IsValid() || KeyVerifier.Len() != 40)
	{
		return false;
	}
	const FString Directory = GetSaveDirectory(SaveId);
	const FString Latest = FPaths::Combine(Directory, TEXT("LatestAutoSave.sav"));
	const FString Backup = Latest + TEXT(".bak");
	FAutoSaveFile File;
	if (!ReadAutoSaveFile(Latest, File))
	{
		if (!ReadAutoSaveFile(Backup, File))
		{
			return false;
		}
	}
	if (File.OwnerId != OwnerId || File.SaveId != SaveId
		|| File.KeyVerifier != KeyVerifier || File.MapName != World->GetMapName()
		|| !bStableIdsValid)
	{
		return false;
	}

	// 파일과 소유권 검증이 모두 끝난 뒤에만 실행 중 복원 기준을 바꾼다.
	ResetRuntimeCheckpointState();
	ConfigureNewSave(OwnerId, SaveId, KeyVerifier);
	if (!CommitCheckpointSnapshot(File.Snapshot))
	{
		return false;
	}
	RestoreCurrentRoomPhaseProgress(File.Snapshot.RoomPhaseProgress);
	return true;
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

bool UOutlierSaveSubSystem::CommitDurableCheckpointSnapshot(const FOutlierCheckpointSnapshot& Snapshot)
{
	if (!Snapshot.IsValid() || Snapshot.bInitialSnapshot || Snapshot.CheckpointId.IsNone()
		|| CommittedCheckpointIds.Contains(Snapshot.CheckpointId) || !bStableIdsValid
		|| !ActiveOwnerId.IsValid() || !ActiveSaveId.IsValid()
		|| ActiveKeyVerifier.Len() != 40)
	{
		return false;
	}
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World || World->GetMapName().IsEmpty())
	{
		return false;
	}

	TArray<uint8> Payload;
	FMemoryWriter PayloadWriter(Payload);
	FObjectAndNameAsStringProxyArchive ObjectWriter(PayloadWriter, false);
	FOutlierCheckpointSnapshot Copy = Snapshot;
	FOutlierCheckpointSnapshot::StaticStruct()->SerializeItem(ObjectWriter, &Copy, nullptr);
	if (PayloadWriter.IsError() || Payload.IsEmpty() || Payload.Num() > MaxAutoSaveBytes)
	{
		return false;
	}
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	uint32 Magic = AutoSaveMagic;
	int32 Version = AutoSaveVersion;
	FString MapName = World->GetMapName();
	int32 PayloadSize = Payload.Num();
	uint32 PayloadCrc = FCrc::MemCrc32(Payload.GetData(), Payload.Num());
	Writer << Magic;
	Writer << Version;
	Writer << ActiveOwnerId;
	Writer << ActiveSaveId;
	Writer << ActiveKeyVerifier;
	Writer << MapName;
	Writer << PayloadSize;
	Writer << PayloadCrc;
	Writer.Serialize(Payload.GetData(), Payload.Num());
	if (Writer.IsError() || Bytes.Num() > MaxAutoSaveBytes)
	{
		return false;
	}

	const FString Directory = GetSaveDirectory(ActiveSaveId);
	IFileManager& Files = IFileManager::Get();
	if (!Files.DirectoryExists(*Directory) && !Files.MakeDirectory(*Directory, true))
	{
		return false;
	}
	const FString Latest = FPaths::Combine(Directory, TEXT("LatestAutoSave.sav"));
	const FString Pending = Latest + TEXT(".tmp");
	const FString Backup = Latest + TEXT(".bak");
	TArray<uint8> VerifiedBytes;
	if (!FFileHelper::SaveArrayToFile(Bytes, *Pending)
		|| !FFileHelper::LoadFileToArray(VerifiedBytes, *Pending)
		|| Bytes != VerifiedBytes)
	{
		Files.Delete(*Pending);
		return false;
	}

	// 손상된 최신본보다 유효한 백업이 우선이다. 새 파일 교체가 실패해도 백업을 잃지 않는다.
	if (Files.FileExists(*Latest))
	{
		FAutoSaveFile Previous;
		if (ReadAutoSaveFile(Latest, Previous))
		{
			if (Files.FileExists(*Backup) && !Files.Delete(*Backup))
			{
				Files.Delete(*Pending);
				return false;
			}
			if (!Files.Move(*Backup, *Latest))
			{
				Files.Delete(*Pending);
				return false;
			}
		}
		else if (!Files.Delete(*Latest))
		{
			Files.Delete(*Pending);
			return false;
		}
	}
	if (!Files.Move(*Latest, *Pending))
	{
		if (Files.FileExists(*Backup))
		{
			Files.Move(*Latest, *Backup);
		}
		Files.Delete(*Pending);
		return false;
	}
	VerifiedBytes.Reset();
	if (!FFileHelper::LoadFileToArray(VerifiedBytes, *Latest) || Bytes != VerifiedBytes)
	{
		Files.Delete(*Latest);
		if (Files.FileExists(*Backup))
		{
			Files.Move(*Latest, *Backup);
		}
		return false;
	}
	Files.Delete(*Backup);
	return CommitCheckpointSnapshot(Snapshot);
}

bool UOutlierSaveSubSystem::GetRestoreSnapshot(FOutlierCheckpointSnapshot& OutSnapshot) const
{
	// 마지막 확정 체크포인트를 우선한다. 아직 없으면 최초 시작 스냅샷으로 돌아가 이후 진행을 버린다.
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

void UOutlierSaveSubSystem::ResetCommittedCheckpointIds()
{
	// 마지막 체크포인트 스냅샷은 유지한다. 같은 체크포인트를 새 진행에서 다시 저장할 수 있게 목록만 비운다.
	CommittedCheckpointIds.Reset();
}

void UOutlierSaveSubSystem::SyncCurrentSuitState(const AOutlierPlayerState* ShooterPlayerState)
{
	if (!ShooterPlayerState || !ShooterPlayerState->HasAuthority() || !ShooterPlayerState->IsShooterPlayer())
	{
		return;
	}

	bCurrentSuitAcquired = ShooterPlayerState->GetAcquiredSuit();
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

	// 현재 플레이의 진행 기록만 바꾼다. 확정 스냅샷은 다음 체크포인트 저장 전까지 그대로 유지한다.
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
	// 병합이 아니라 교체다. 체크포인트 이후 사용한 노드/문/해킹/전투 등의 진행은 롤백한다.
	CurrentWorldProgress = Snapshot;
}

void UOutlierSaveSubSystem::SetCurrentRoomPhaseProgress(
	FGameplayTag RoomTag, const FOutlierRoomPhaseProgress& Progress)
{
	if (RoomTag.IsValid() && Progress.NextPhaseIndex >= 0)
	{
		CurrentRoomPhaseProgress.Add(RoomTag, Progress);
	}
}

void UOutlierSaveSubSystem::ClearCurrentRoomPhaseProgress(FGameplayTag RoomTag)
{
	CurrentRoomPhaseProgress.Remove(RoomTag);
}

void UOutlierSaveSubSystem::RestoreCurrentRoomPhaseProgress(
	const TMap<FGameplayTag, FOutlierRoomPhaseProgress>& Progress)
{
	CurrentRoomPhaseProgress = Progress;
}

bool UOutlierSaveSubSystem::SetDestroyedTurretState(FName TurretId, bool bDestroyed)
{
	if (TurretId.IsNone())
	{
		return false;
	}

	if (bDestroyed)
	{
		CurrentDestroyedTurretIds.Add(TurretId);
	}
	else
	{
		CurrentDestroyedTurretIds.Remove(TurretId);
	}
	return true;
}

bool UOutlierSaveSubSystem::IsTurretDestroyed(FName TurretId) const
{
	return !TurretId.IsNone() && CurrentDestroyedTurretIds.Contains(TurretId);
}

void UOutlierSaveSubSystem::RestoreCurrentDestroyedTurretIds(
	const TSet<FName>& DestroyedTurretIds)
{
	// 병합하면 체크포인트 이후 파괴된 터렛이 남는다. Snapshot 집합으로 교체해야 롤백된다.
	CurrentDestroyedTurretIds = DestroyedTurretIds;
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

bool UOutlierSaveSubSystem::RegisterPersistentTurretId(FName TurretId, UObject* Owner)
{
	return RegisterStableId(TurretId, Owner, TEXT("WaveTurret"));
}

void UOutlierSaveSubSystem::UnregisterPersistentTurretId(FName TurretId, const UObject* Owner)
{
	UnregisterStableId(TurretId, Owner);
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
