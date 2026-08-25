// Fill out your copyright notice in the Description page of Project Settings.


#include "OutlierArenaSettings.h"
#include "Engine/World.h"

FString UOutlierArenaSettings::GetArenaPackageName() const
{
	return ArenaLevel.ToSoftObjectPath().GetLongPackageName();
}

bool UOutlierArenaSettings::MatchesArenaPackageName(const FString& WorldPackageName) const
{
	const FString ArenaPackageName = GetArenaPackageName();
	// PIE에서는 로드된 패키지 이름에 접두사가 붙으므로 제거한 뒤 설정된 Arena 맵과 비교.
	return !ArenaPackageName.IsEmpty()
		&& UWorld::RemovePIEPrefix(WorldPackageName) == ArenaPackageName;
}

bool UOutlierArenaSettings::IsArenaWorld(const UWorld* World) const
{
	if (!World || !World->PersistentLevel)
	{
		return false;
	}

	return MatchesArenaPackageName(
		World->PersistentLevel->GetOutermost()->GetName());
}
