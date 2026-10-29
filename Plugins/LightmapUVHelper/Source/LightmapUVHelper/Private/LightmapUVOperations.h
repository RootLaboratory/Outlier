#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

class UStaticMesh;
class ULightmapUVSettings;

struct FLightmapUVPlan
{
	int32 SourceModelCount = 0;
	TArray<int32> LODs;
	TArray<FMeshBuildSettings> Before;
	TArray<FMeshBuildSettings> After;
	int32 BeforeCoordinate = 0;
	int32 AfterCoordinate = 0;
	int32 BeforeResolution = 0;
	int32 AfterResolution = 0;
	bool bChanged = false;
	TArray<FString> Errors;
	TArray<FString> Warnings;
	FString Summary;
};

namespace LightmapUV
{
	FLightmapUVPlan MakePlan(UStaticMesh* Mesh, const ULightmapUVSettings& Settings);
	bool MatchesSnapshot(UStaticMesh* Mesh, const FLightmapUVPlan& Plan);
	FString Describe(UStaticMesh* Mesh);
	FString LightingStatus();
	/** Modifies an already validated mesh. Caller owns the undo transaction. No packages are saved. */
	bool Apply(UStaticMesh* Mesh, const FLightmapUVPlan& Plan, FString& OutResult);
}
