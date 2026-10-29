#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "LightmapMeshDiagnostics.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightmapMeshDiagnosticsTests, "Outlier.LightmapUVHelper.MeshSectionDiagnostics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLightmapMeshDiagnosticsTests::RunTest(const FString& Parameters)
{
	FLightmapMeshDiagnostic E;
	E.Sections.RenderSections = E.Sections.ProxySections = E.Sections.RHISegments = E.Sections.InitializerSegments = 2;
	E.Sections.BatchSectionIndices = {0, 1};
	LightmapMeshDiagnostics::Evaluate(E);
	TestFalse(TEXT("Matching sections do not produce a candidate"), E.HasRisk());
	E.Sections.BatchSectionIndices = {0};
	E.Sections.MissingMaterialSections = {1};
	LightmapMeshDiagnostics::Evaluate(E);
	TestTrue(TEXT("Missing material triggers exact segment count mismatch"), E.bCountMismatch);
	TestFalse(TEXT("Trailing missing material does not invent an index gap"), E.bIndexMismatch);
	E.Sections.BatchSectionIndices = {0, 2};
	E.Sections.MissingMaterialSections = {1};
	LightmapMeshDiagnostics::Evaluate(E);
	TestFalse(TEXT("Equal counts can still have an index mismatch"), E.bCountMismatch);
	TestTrue(TEXT("Skipped middle section is detected"), E.bIndexMismatch);
	E.Sections.RHISegments = INDEX_NONE;
	E.Sections.InitializerSegments = 3;
	LightmapMeshDiagnostics::Evaluate(E);
	TestFalse(TEXT("Initializer mismatch is not claimed as confirmed RHI mismatch"), E.bCountMismatch);
	TestTrue(TEXT("Initializer still identifies a candidate"), E.HasRisk());
	E.Sections.bAllUnlit = true;
	LightmapMeshDiagnostics::Evaluate(E);
	TestFalse(TEXT("All-Unlit path is skipped like GPULM"), E.HasRisk());
	E.Sections.bAllUnlit = false;
	E.Sections.BatchSectionIndices = {0, 1};
	E.Sections.MissingMaterialSections.Reset();
	E.Sections.EmptySections = {2};
	LightmapMeshDiagnostics::Evaluate(E);
	TestTrue(TEXT("Empty trailing sections are reported"), E.HasRisk());
	FLightmapMeshDiagnosticReport Report;
	Report.Map = TEXT("/Game/Tests/BakeRoom");
	E.MeshPath = TEXT("/Game/Test/TestMesh.TestMesh");
	Report.Entries.Add(E);
	TSharedPtr<FJsonObject> Json;
	TestTrue(TEXT("Diagnostic report is valid JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Report.ToJson()), Json));
	if (Json)
	{
		TestFalse(TEXT("Scanner never reports a bake execution"), Json->GetBoolField(TEXT("bake_executed")));
		TestEqual(TEXT("Report retains the mesh asset path"), Json->GetArrayField(TEXT("entries"))[0]->AsObject()->GetStringField(TEXT("mesh")), E.MeshPath);
	}
	return true;
}
#endif
