#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "LightmapUVOperations.h"
#include "LightmapUVSettings.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshCompiler.h"
#include "UObject/StrongObjectPtr.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "SLightmapMeshPreview.h"
#include "SLightmapUVPanel.h"

namespace
{
	UStaticMesh* MakeTestMesh(int32 UVChannels = 1)
	{
		UStaticMesh* Mesh = NewObject<UStaticMesh>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional);
		Mesh->SetNumSourceModels(1);
		FMeshDescription Description;
		FStaticMeshAttributes Attributes(Description);
		Attributes.Register();
		Attributes.GetVertexInstanceUVs().SetNumChannels(UVChannels);
		const FVector3f Positions[] = { {0, 0, 0}, {100, 0, 0}, {0, 100, 0} };
		const FVector2f UVs[] = { {0, 0}, {1, 0}, {0, 1} };
		TArray<FVertexInstanceID> Corners;
		for (int32 I = 0; I < 3; ++I)
		{
			const FVertexID Vertex = Description.CreateVertex();
			Attributes.GetVertexPositions()[Vertex] = Positions[I];
			const FVertexInstanceID Instance = Description.CreateVertexInstance(Vertex);
			Attributes.GetVertexInstanceNormals()[Instance] = FVector3f(0, 0, 1);
			Attributes.GetVertexInstanceTangents()[Instance] = FVector3f(1, 0, 0);
			Attributes.GetVertexInstanceBinormalSigns()[Instance] = 1;
			for (int32 Channel = 0; Channel < UVChannels; ++Channel) Attributes.GetVertexInstanceUVs().Set(Instance, Channel, UVs[I]);
			Corners.Add(Instance);
		}
		Description.CreateTriangle(Description.CreatePolygonGroup(), Corners);
		Mesh->GetStaticMaterials().Add(FStaticMaterial());
		Mesh->CreateMeshDescription(0, MoveTemp(Description));
		Mesh->CommitMeshDescription(0);
		Mesh->GetSourceModel(0).BuildSettings.bGenerateLightmapUVs = false;
		Mesh->GetSourceModel(0).BuildSettings.bUseFullPrecisionUVs = true;
		Mesh->SetLightMapCoordinateIndex(0);
		Mesh->SetLightMapResolution(128);
		Mesh->Build(false);
		FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
		return Mesh;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightmapUVPlanTests, "Outlier.LightmapUVHelper.PlanningAndValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLightmapUVPlanTests::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UStaticMesh> Mesh(MakeTestMesh());
	TStrongObjectPtr<ULightmapUVSettings> S(NewObject<ULightmapUVSettings>());
	S->bApplyCoordinate = true;
	S->CoordinateIndex = 1;
	FLightmapUVPlan P = LightmapUV::MakePlan(Mesh.Get(), *S);
	TestTrue(TEXT("Generate into a new channel is valid"), P.Errors.IsEmpty());
	TestTrue(TEXT("Change is detected"), P.bChanged);
	TestTrue(TEXT("Unselected precision option is preserved"), P.After[0].bUseFullPrecisionUVs);
	TestEqual(TEXT("Unselected resolution is preserved"), P.AfterResolution, 128);
	TestFalse(TEXT("Planning never mutates the asset"), Mesh->GetSourceModel(0).BuildSettings.bGenerateLightmapUVs);
	TestTrue(TEXT("Snapshot initially matches"), LightmapUV::MatchesSnapshot(Mesh.Get(), P));
	Mesh->SetLightMapResolution(256);
	TestFalse(TEXT("External changes invalidate snapshot"), LightmapUV::MatchesSnapshot(Mesh.Get(), P));
	Mesh->SetLightMapResolution(128);

	S->bApplySource = true;
	S->SourceIndex = 5;
	TestFalse(TEXT("Missing source channel is rejected"), LightmapUV::MakePlan(Mesh.Get(), *S).Errors.IsEmpty());
	S->SourceIndex = 0;
	S->bApplyDestination = true;
	S->DestinationIndex = 4;
	TestFalse(TEXT("Gaps in destination channels are rejected"), LightmapUV::MakePlan(Mesh.Get(), *S).Errors.IsEmpty());
	S->DestinationIndex = 1;
	S->bApplyMinResolution = true;
	S->MinResolution = 63;
	TestFalse(TEXT("Invalid packing resolution is rejected"), LightmapUV::MakePlan(Mesh.Get(), *S).Errors.IsEmpty());
	S->MinResolution = 64;
	S->LODScope = ELightmapUVLOD::Specified;
	S->LODIndex = 5;
	TestFalse(TEXT("Missing LOD is rejected"), LightmapUV::MakePlan(Mesh.Get(), *S).Errors.IsEmpty());
	S->LODScope = ELightmapUVLOD::LOD0;

	TStrongObjectPtr<UStaticMesh> ExistingUV(MakeTestMesh(2));
	TestFalse(TEXT("Existing UV is protected by default"), LightmapUV::MakePlan(ExistingUV.Get(), *S).Errors.IsEmpty());
	S->bAllowOverwrite = true;
	TestTrue(TEXT("Explicit overwrite is accepted"), LightmapUV::MakePlan(ExistingUV.Get(), *S).Errors.IsEmpty());

	// Exercise the actual rebuild on a transient triangle; no project content is touched.
	P = LightmapUV::MakePlan(Mesh.Get(), *S);
	FString Result;
	{
		FScopedTransaction Transaction(NSLOCTEXT("LightmapUVTests", "Undo", "Test lightmap UV edit"));
		TestTrue(TEXT("Apply and rebuild succeed"), LightmapUV::Apply(Mesh.Get(), P, Result));
	}
	TestEqual(TEXT("Destination UV channel exists after build"), Mesh->GetNumTexCoords(0), 2);
	TestEqual(TEXT("Lighting channel points to generated UV"), Mesh->GetLightMapCoordinateIndex(), 1);
	TestTrue(TEXT("Unrelated setting survives apply"), Mesh->GetSourceModel(0).BuildSettings.bUseFullPrecisionUVs);
	TestFalse(TEXT("Repeated apply with same settings is a no-op"), LightmapUV::MakePlan(Mesh.Get(), *S).bChanged);
	GEditor->UndoTransaction();
	FStaticMeshCompilingManager::Get().FinishCompilation({Mesh.Get()});
	TestFalse(TEXT("Undo restores generation setting"), Mesh->GetSourceModel(0).BuildSettings.bGenerateLightmapUVs);
	TestEqual(TEXT("Undo restores lighting channel"), Mesh->GetLightMapCoordinateIndex(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightmapUVSlateTests, "Outlier.LightmapUVHelper.PreviewLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLightmapUVSlateTests::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UStaticMesh> Mesh(MakeTestMesh());
	{
		TSharedRef<SLightmapUVPanel> Panel = SNew(SLightmapUVPanel);
		TestTrue(TEXT("Panel constructs with editor services"), Panel->GetChildren()->Num() > 0);
		TSharedRef<SLightmapMeshPreview> Preview = SNew(SLightmapMeshPreview);
		TestTrue(TEXT("Preview renders continuous frames for camera interaction"), Preview->IsRealtime());
		Preview->SetMesh(Mesh.Get());
		Preview->FocusMesh();
		const FVector PannedLocation(123, 456, 789);
		Preview->GetViewportClient()->SetViewLocation(PannedLocation);
		Preview->SetMesh(Mesh.Get());
		TestTrue(TEXT("Reselecting the same mesh preserves the camera"), Preview->GetViewportClient()->GetViewLocation().Equals(PannedLocation));
		Preview->SetMesh(nullptr);
	}
	// Detect dangling scene/client references during widget destruction and garbage collection.
	CollectGarbage(RF_NoFlags);
	TestTrue(TEXT("Preview does not own or modify the mesh asset"), Mesh.IsValid());
	TestFalse(TEXT("Preview leaves mesh build settings unchanged"), Mesh->GetSourceModel(0).BuildSettings.bGenerateLightmapUVs);
	return true;
}
#endif
