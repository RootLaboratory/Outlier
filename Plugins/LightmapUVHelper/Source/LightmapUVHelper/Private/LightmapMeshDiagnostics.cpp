#include "LightmapMeshDiagnostics.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "RenderingThread.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "StaticMeshCompiler.h"
#include "StaticMeshResources.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogLightmapMeshDiagnostics, Log, All);

namespace
{
	FString Indices(const TArray<int32>& Values)
	{
		TArray<FString> Text;
		for (int32 V : Values) Text.Add(FString::FromInt(V));
		return TEXT("[") + FString::Join(Text, TEXT(",")) + TEXT("]");
	}

	struct FProxyQuery
	{
		FRayTracingGeometry* Geometry = nullptr;
		int32 ProxySections = INDEX_NONE;
		int32 RHISegments = INDEX_NONE;
		int32 InitializerSegments = INDEX_NONE;
	};
}

void LightmapMeshDiagnostics::Evaluate(FLightmapMeshDiagnostic& E)
{
	E.Findings.Reset();
	E.bCountMismatch = E.bIndexMismatch = false;
	const FLightmapSectionSnapshot& S = E.Sections;
	// GPULM skips the ray tracing command path when all included materials are Unlit.
	if (S.bAllUnlit) return;
	if (S.RHISegments != INDEX_NONE && S.RHISegments != S.BatchSectionIndices.Num())
	{
		E.bCountMismatch = true;
		E.Findings.Add(FString::Printf(TEXT("현재 RHI 섹션 %d ≠ GPULM 예상 배치 %d — 크래시 직전 ensure와 같은 불일치"), S.RHISegments, S.BatchSectionIndices.Num()));
	}
	else if (S.RHISegments == INDEX_NONE && S.InitializerSegments != INDEX_NONE && S.InitializerSegments != S.BatchSectionIndices.Num())
	{
		E.Findings.Add(FString::Printf(TEXT("Proxy 초기화 섹션 %d ≠ 예상 배치 %d (RHI 미상주: 런타임 불일치 확정 불가)"), S.InitializerSegments, S.BatchSectionIndices.Num()));
	}
	for (int32 Batch = 0; Batch < S.BatchSectionIndices.Num(); ++Batch)
	{
		if (S.BatchSectionIndices[Batch] != Batch)
		{
			E.bIndexMismatch = true;
			E.Findings.Add(TEXT("배치 순번과 원본 섹션 번호가 다릅니다: ") + Indices(S.BatchSectionIndices));
			break;
		}
	}
	if (!S.MissingMaterialSections.IsEmpty()) E.Findings.Add(TEXT("실제 적용 머티리얼 None인 섹션: ") + Indices(S.MissingMaterialSections));
	if (!S.EmptySections.IsEmpty()) E.Findings.Add(TEXT("삼각형 0개인 섹션: ") + Indices(S.EmptySections));
	if (S.ProxySections == INDEX_NONE) E.Findings.Add(TEXT("해당 LOD의 Ray Tracing Proxy가 없습니다. 지원 설정/빌드 상태 확인 필요"));
}

FString FLightmapMeshDiagnostic::Description() const
{
	return FString::Printf(TEXT("%s | LOD%d | Nanite %s | %s\n%s\n렌더 섹션 %d / 예상 배치 %d / Proxy 섹션 %d / RHI %d / 초기화 %d\n%s"),
		*Actor, LOD, bNanite ? TEXT("ON") : TEXT("OFF"), bStaticLightingEligible ? TEXT("정적 조명 설정 유효") : TEXT("정적 조명 설정 비유효: 참고 후보"),
		*MeshPath, Sections.RenderSections, Sections.BatchSectionIndices.Num(), Sections.ProxySections, Sections.RHISegments, Sections.InitializerSegments,
		*FString::Join(Findings, TEXT("\n")));
}

FLightmapMeshDiagnosticReport LightmapMeshDiagnostics::ScanEditorWorld()
{
	FLightmapMeshDiagnosticReport Report;
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World || World->WorldType != EWorldType::Editor || GEditor->PlayWorld)
	{
		Report.Error = TEXT("PIE 종료 후 편집 중인 레벨에서 실행하세요.");
		return Report;
	}
	Report.Map = World->GetPathName();
	TArray<TStrongObjectPtr<UStaticMeshComponent>> Components;
	TArray<TStrongObjectPtr<UStaticMesh>> Meshes;
	TSet<UStaticMesh*> UniqueMeshes;
	for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
	{
		UStaticMeshComponent* Component = *It;
		if (Component->IsTemplate() || Component->GetWorld() != World || !Component->IsRegistered() || !Component->GetStaticMesh()) continue;
		Components.Emplace(Component);
		if (!UniqueMeshes.Contains(Component->GetStaticMesh()))
		{
			UniqueMeshes.Add(Component->GetStaticMesh());
			Meshes.Emplace(Component->GetStaticMesh());
		}
	}
	Report.UniqueMeshes = Meshes.Num();
	TArray<UStaticMesh*> MeshPointers = UniqueMeshes.Array();
	FStaticMeshCompilingManager::Get().FinishCompilation(MeshPointers);
	FScopedSlowTask Progress(Components.Num(), NSLOCTEXT("LightmapMeshDiagnostics", "Scan", "현재 레벨의 GPULightmass 섹션 호환성 검사 중…"));
	Progress.MakeDialog(true);

	TArray<FProxyQuery> Queries;
	TMap<TPair<UStaticMesh*, int32>, int32> QueryIndices;
	TArray<int32> EntryQueryIndices;
	for (const auto& StrongComponent : Components)
	{
		if (Progress.ShouldCancel()) { Report.bCancelled = true; break; }
		UStaticMeshComponent* Component = StrongComponent.Get();
		Progress.EnterProgressFrame(1, FText::FromString(Component->GetName()));
		++Report.Components;
		UStaticMesh* Mesh = Component->GetStaticMesh();
		const FStaticMeshRenderData* Data = Mesh->GetRenderData();
		if (!Data) continue;
		for (int32 LOD = 0; LOD < Data->LODResources.Num(); ++LOD)
		{
			const FStaticMeshLODResources& Resource = Data->LODResources[LOD];
			if (Resource.GetNumVertices() == 0) continue;
			FLightmapMeshDiagnostic E;
			E.Component = Component;
			E.Actor = Component->GetOwner() ? Component->GetOwner()->GetActorLabel() : Component->GetName();
			E.ComponentPath = Component->GetPathName();
			E.MeshPath = Mesh->GetPathName();
			E.LOD = LOD;
			E.bNanite = Mesh->HasValidNaniteData();
			E.bStaticLightingEligible = Component->HasValidSettingsForStaticLighting(false);
			E.bInstanced = Component->IsA<UInstancedStaticMeshComponent>();
			E.Sections.RenderSections = Resource.Sections.Num();
			bool bAllUnlit = true;
			for (int32 Section = 0; Section < Resource.Sections.Num(); ++Section)
			{
				const FStaticMeshSection& S = Resource.Sections[Section];
				if (S.NumTriangles == 0) { E.Sections.EmptySections.Add(Section); continue; }
				UMaterialInterface* Material = Component->GetMaterial(S.MaterialIndex);
				if (!Material) { E.Sections.MissingMaterialSections.Add(Section); continue; }
				E.Sections.BatchSectionIndices.Add(Section);
				bAllUnlit &= Material->GetShadingModels().HasOnlyShadingModel(MSM_Unlit);
			}
			E.Sections.bAllUnlit = bAllUnlit && !E.Sections.BatchSectionIndices.IsEmpty();
			const TPair<UStaticMesh*, int32> Key(Mesh, LOD);
			int32* ExistingQuery = QueryIndices.Find(Key);
			int32 QueryIndex;
			if (ExistingQuery) QueryIndex = *ExistingQuery;
			else
			{
				FProxyQuery Q;
				if (Data->RayTracingProxy && Data->RayTracingProxy->LODs.IsValidIndex(LOD))
				{
					const FStaticMeshRayTracingProxyLOD& Proxy = Data->RayTracingProxy->LODs[LOD];
					Q.Geometry = Proxy.RayTracingGeometry;
					Q.ProxySections = Proxy.Sections ? Proxy.Sections->Num() : INDEX_NONE;
				}
				QueryIndex = Queries.Add(Q);
				QueryIndices.Add(Key, QueryIndex);
			}
			EntryQueryIndices.Add(QueryIndex);
			Report.Entries.Add(MoveTemp(E));
		}
	}
	// Inspect GPU resources on the render thread. Strong asset references above keep their owners
	// alive until the command completes. No ray tracing scene or light bake is constructed here.
	ENQUEUE_RENDER_COMMAND(ReadLightmapProxySegments)([&Queries](FRHICommandListImmediate&)
	{
#if RHI_RAYTRACING
		for (FProxyQuery& Q : Queries)
		{
			if (!Q.Geometry) continue;
			Q.InitializerSegments = Q.Geometry->Initializer.Segments.Num();
			if (const FRHIRayTracingGeometry* GeometryRHI = Q.Geometry->GetRHI()) Q.RHISegments = GeometryRHI->GetNumSegments();
		}
#endif
	});
	FlushRenderingCommands();
	for (int32 I = 0; I < Report.Entries.Num(); ++I)
	{
		FLightmapMeshDiagnostic& E = Report.Entries[I];
		const FProxyQuery& Q = Queries[EntryQueryIndices[I]];
		E.Sections.ProxySections = Q.ProxySections;
		E.Sections.RHISegments = Q.RHISegments;
		E.Sections.InitializerSegments = Q.InitializerSegments;
		Evaluate(E);
		if (E.HasRisk()) UE_LOG(LogLightmapMeshDiagnostics, Warning, TEXT("%s\nComponent: %s"), *E.Description(), *E.ComponentPath);
	}
	Report.Entries.StableSort([](const FLightmapMeshDiagnostic& A, const FLightmapMeshDiagnostic& B)
	{
		const int32 AS = (A.bCountMismatch ? 4 : 0) + (A.bIndexMismatch ? 2 : 0) + (A.bStaticLightingEligible ? 1 : 0);
		const int32 BS = (B.bCountMismatch ? 4 : 0) + (B.bIndexMismatch ? 2 : 0) + (B.bStaticLightingEligible ? 1 : 0);
		return AS > BS;
	});
	return Report;
}

int32 FLightmapMeshDiagnosticReport::RiskCount() const
{
	int32 Count = 0;
	for (const FLightmapMeshDiagnostic& E : Entries) if (E.HasRisk()) ++Count;
	return Count;
}

FString FLightmapMeshDiagnosticReport::ToJson() const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("report_type"), TEXT("GPULM_mesh_section_diagnostics"));
	Root->SetStringField(TEXT("captured_utc"), FDateTime::UtcNow().ToIso8601());
	Root->SetStringField(TEXT("map"), Map);
	Root->SetStringField(TEXT("error"), Error);
	Root->SetBoolField(TEXT("cancelled"), bCancelled);
	Root->SetBoolField(TEXT("bake_executed"), false);
	Root->SetStringField(TEXT("scope"), TEXT("loaded_registered_static_mesh_components_all_render_LODs; landscape_and_unloaded_actors_not_scanned; candidates_not_crash_proof"));
	Root->SetNumberField(TEXT("components"), Components);
	Root->SetNumberField(TEXT("unique_meshes"), UniqueMeshes);
	Root->SetNumberField(TEXT("risk_entries"), RiskCount());
	TArray<TSharedPtr<FJsonValue>> Items;
	for (const FLightmapMeshDiagnostic& E : Entries)
	{
		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("actor"), E.Actor);
		Item->SetStringField(TEXT("component"), E.ComponentPath);
		Item->SetStringField(TEXT("mesh"), E.MeshPath);
		Item->SetNumberField(TEXT("lod"), E.LOD);
		Item->SetBoolField(TEXT("nanite"), E.bNanite);
		Item->SetBoolField(TEXT("static_lighting_eligible"), E.bStaticLightingEligible);
		Item->SetBoolField(TEXT("instanced"), E.bInstanced);
		Item->SetNumberField(TEXT("render_sections"), E.Sections.RenderSections);
		Item->SetNumberField(TEXT("batch_count"), E.Sections.BatchSectionIndices.Num());
		Item->SetStringField(TEXT("batch_section_indices"), Indices(E.Sections.BatchSectionIndices));
		Item->SetNumberField(TEXT("proxy_sections"), E.Sections.ProxySections);
		Item->SetNumberField(TEXT("rhi_segments"), E.Sections.RHISegments);
		Item->SetNumberField(TEXT("initializer_segments"), E.Sections.InitializerSegments);
		Item->SetBoolField(TEXT("count_mismatch"), E.bCountMismatch);
		Item->SetBoolField(TEXT("index_mismatch"), E.bIndexMismatch);
		Item->SetStringField(TEXT("findings"), FString::Join(E.Findings, TEXT("\n")));
		Items.Add(MakeShared<FJsonValueObject>(Item));
	}
	Root->SetArrayField(TEXT("entries"), Items);
	FString Output;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Output));
	return Output;
}

bool LightmapMeshDiagnostics::Save(const FLightmapMeshDiagnosticReport& Report, FString& OutPath)
{
	const FString Folder = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("LightmapUVHelper/Diagnostics"));
	OutPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(Folder,
		TEXT("GPULM_Meshes_") + FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ_")) + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".json")));
	return IFileManager::Get().MakeDirectory(*Folder, true)
		&& FFileHelper::SaveStringToFile(Report.ToJson(), *OutPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
