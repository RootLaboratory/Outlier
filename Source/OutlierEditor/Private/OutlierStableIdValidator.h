#pragma once

#include "CoreMinimal.h"

class AActor;
class UWorld;

namespace OutlierEditor
{
struct FStableIdEntry
{
	TWeakObjectPtr<AActor> Actor;
	FString ActorName;
	FString LevelName;
	FString Property;
	FName Id;
};

struct FStableIdIssue
{
	FStableIdEntry Entry;
	bool bDuplicate = false;
};

struct FStableIdReport
{
	int32 ActorCount = 0;
	int32 IdCount = 0;
	TArray<FStableIdIssue> Issues;
};

// Matches SaveSubsystem's shared, case-insensitive FName namespace across all ID kinds.
TArray<FStableIdIssue> FindStableIdIssues(const TArray<FStableIdEntry>& Entries);
// Only inspects actors in loaded levels of this world. Does not load or modify map assets.
FStableIdReport ScanStableIds(UWorld* World);
}
