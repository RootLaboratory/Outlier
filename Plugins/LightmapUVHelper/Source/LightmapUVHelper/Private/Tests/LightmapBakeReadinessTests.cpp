#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "LightmapBakeReadiness.h"
#include "SLightmapBakeReadiness.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Editor.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightmapBakeReadinessTests, "Outlier.LightmapUVHelper.BWYSReadiness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLightmapBakeReadinessTests::RunTest(const FString& Parameters)
{
	FLightmapBakeEnvironment E;
	E.bPluginInstalled = E.bPluginEnabled = E.bGPUModuleLoaded = true;
	E.bHasEditorWorld = E.bDX12 = E.bRayTracingHardware = E.bRayTracingActive = true;
	E.AllowStaticLighting = E.VirtualTextures = E.VirtualTextureLightmaps = E.RayTracing = 1;
	E.GIMethod = 0;
	E.bHasPerspectiveCamera = E.bCameraRealtime = true;
	E.Map = TEXT("/Game/Tests/BakeRoom");
	E.RHI = TEXT("D3D12");
	E.CameraLocation = FVector(123, 456, 789);
	E.CameraFOV = 70;
	TestTrue(TEXT("Configured environment passes"), LightmapBake::Evaluate(E).IsEnvironmentReady());
	E.bPluginEnabled = false;
	TestFalse(TEXT("Disabled plugin blocks BWYS even with RT enabled"), LightmapBake::Evaluate(E).IsEnvironmentReady());
	E.bPluginEnabled = true;
	E.VirtualTextureLightmaps = 0;
	TestFalse(TEXT("VT lightmaps are required for BWYS readiness"), LightmapBake::Evaluate(E).IsEnvironmentReady());
	E.VirtualTextureLightmaps = 1;
	E.AllowStaticLighting = 0;
	TestFalse(TEXT("Static lighting off blocks readiness"), LightmapBake::Evaluate(E).IsEnvironmentReady());
	E.AllowStaticLighting = 1;
	E.GIMethod = 1;
	TestFalse(TEXT("Lumen is flagged for static result inspection"), LightmapBake::Evaluate(E).IsEnvironmentReady());
	E.GIMethod = 0;
	E.bForceNoPrecomputedLighting = true;
	TestFalse(TEXT("Level force-no-precomputed blocks readiness"), LightmapBake::Evaluate(E).IsEnvironmentReady());
	E.bForceNoPrecomputedLighting = false;
	E.bHasPerspectiveCamera = false;
	TestFalse(TEXT("Asset preview cannot substitute for a level camera"), LightmapBake::Evaluate(E).IsEnvironmentReady());
	E.bHasPerspectiveCamera = true;
	E.bRayTracingActive = false;
	TestFalse(TEXT("Ray tracing setting alone does not prove runtime support"), LightmapBake::Evaluate(E).IsEnvironmentReady());
	E.bRayTracingActive = true;
	E.bPIE = true;
	TestFalse(TEXT("PIE is excluded"), LightmapBake::Evaluate(E).IsEnvironmentReady());
	E.bPIE = false;
	E.bCameraRealtime = false;
	E.PendingSettings.Add(TEXT("r.VirtualTextures: file 0 / runtime 1"));
	const FLightmapBakeReadiness Report = LightmapBake::Evaluate(E);
	TestTrue(TEXT("Realtime off is an advisory, not a missing backend"), Report.IsEnvironmentReady());
	TSharedPtr<FJsonObject> Json;
	TestTrue(TEXT("Report is valid JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Report.ToJson()), Json));
	if (Json)
	{
		TestFalse(TEXT("Readiness never claims a bake was executed"), Json->GetBoolField(TEXT("bake_executed")));
		TestEqual(TEXT("Camera snapshot is preserved"), Json->GetObjectField(TEXT("camera"))->GetNumberField(TEXT("x")), 123.0);
		TestEqual(TEXT("Report refers to actual level"), Json->GetStringField(TEXT("map")), E.Map);
	}
	TSharedRef<SLightmapBakeReadiness> Panel = SNew(SLightmapBakeReadiness);
	TestTrue(TEXT("Readiness panel constructs without enabling GPU Lightmass"), Panel->GetChildren()->Num() > 0);
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	const int32 ActorsBefore = World ? World->GetActorCount() : 0;
	IConsoleVariable* StaticLighting = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AllowStaticLighting"));
	const int32 StaticLightingBefore = StaticLighting ? StaticLighting->GetInt() : -1;
	const FLightmapBakeEnvironment Captured = LightmapBake::CaptureEnvironment();
	TestEqual(TEXT("Readiness does not spawn a GPU Lightmass settings actor"), World ? World->GetActorCount() : 0, ActorsBefore);
	TestEqual(TEXT("Readiness never changes renderer CVars"), StaticLighting ? StaticLighting->GetInt() : -1, StaticLightingBefore);
	TestEqual(TEXT("Readiness reports effective setting"), Captured.AllowStaticLighting, StaticLightingBefore);
	return true;
}
#endif
