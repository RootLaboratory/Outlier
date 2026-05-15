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
