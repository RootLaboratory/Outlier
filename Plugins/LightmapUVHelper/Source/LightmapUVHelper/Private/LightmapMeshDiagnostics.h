#pragma once

#include "CoreMinimal.h"

class UStaticMeshComponent;

struct FLightmapSectionSnapshot
{
	TArray<int32> BatchSectionIndices;
	TArray<int32> EmptySections;
	TArray<int32> MissingMaterialSections;
	int32 RenderSections = 0;
	int32 ProxySections = INDEX_NONE;
	int32 RHISegments = INDEX_NONE;
	int32 InitializerSegments = INDEX_NONE;
	bool bAllUnlit = false;
};

struct FLightmapMeshDiagnostic
{
	TWeakObjectPtr<UStaticMeshComponent> Component;
	FString Actor;
	FString ComponentPath;
	FString MeshPath;
	int32 LOD = 0;
	bool bNanite = false;
	bool bStaticLightingEligible = false;
	bool bInstanced = false;
	FLightmapSectionSnapshot Sections;
	TArray<FString> Findings;
	bool bCountMismatch = false;
	bool bIndexMismatch = false;
	bool HasRisk() const { return !Findings.IsEmpty(); }
	FString Description() const;
};

struct FLightmapMeshDiagnosticReport
{
	FString Map;
	FString Error;
	int32 Components = 0;
	int32 UniqueMeshes = 0;
	bool bCancelled = false;
	TArray<FLightmapMeshDiagnostic> Entries;
	int32 RiskCount() const;
	FString ToJson() const;
};

namespace LightmapMeshDiagnostics
{
	/** Same section inclusion rule as UE 5.7 GPULM's mesh batch builder. No geometry is modified. */
	void Evaluate(FLightmapMeshDiagnostic& Entry);
	FLightmapMeshDiagnosticReport ScanEditorWorld();
	bool Save(const FLightmapMeshDiagnosticReport& Report, FString& OutPath);
}
