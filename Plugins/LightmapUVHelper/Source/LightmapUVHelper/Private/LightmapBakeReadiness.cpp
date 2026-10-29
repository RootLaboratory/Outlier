#include "LightmapBakeReadiness.h"

#include "Dom/JsonObject.h"
#include "DynamicRHI.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Framework/Docking/TabManager.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IPluginManager.h"
#include "LevelEditor.h"
#include "LevelEditorViewport.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderUtils.h"
#include "SceneInterface.h"
#include "SLevelViewport.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	const FName NativePanelName(TEXT("GPULightmassSettings"));
	int32 CVar(const TCHAR* Name)
	{
		const IConsoleVariable* Value = IConsoleManager::Get().FindConsoleVariable(Name);
		return Value ? Value->GetInt() : -1;
	}

	FString StateName(ELightmapBakeCheck State)
	{
		switch (State)
		{
		case ELightmapBakeCheck::Pass: return TEXT("pass");
		case ELightmapBakeCheck::Warning: return TEXT("warning");
		default: return TEXT("blocked");
		}
	}
}

FLightmapBakeEnvironment LightmapBake::CaptureEnvironment()
{
	FLightmapBakeEnvironment E;
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("GPULightmass"));
	E.bPluginInstalled = Plugin.IsValid();
	E.bPluginEnabled = Plugin && Plugin->IsEnabled();
	E.bGPUModuleLoaded = FModuleManager::Get().IsModuleLoaded(TEXT("GPULightmass"));
	E.AllowStaticLighting = CVar(TEXT("r.AllowStaticLighting"));
	E.VirtualTextures = CVar(TEXT("r.VirtualTextures"));
	E.VirtualTextureLightmaps = CVar(TEXT("r.VirtualTexturedLightmaps"));
	E.RayTracing = CVar(TEXT("r.RayTracing"));
	E.GIMethod = CVar(TEXT("r.DynamicGlobalIlluminationMethod"));
	E.bDX12 = GDynamicRHI && RHIGetInterfaceType() == ERHIInterfaceType::D3D12;
	E.RHI = GDynamicRHI ? GDynamicRHI->GetName() : TEXT("Unavailable");
	E.GPU = GRHIAdapterName;
	E.bRayTracingHardware = GRHISupportsRayTracing && GRHISupportsRayTracingShaders;
	E.bRayTracingActive = IsRayTracingEnabled();
	E.bPIE = GEditor && GEditor->PlayWorld;
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	E.bHasEditorWorld = World && World->WorldType == EWorldType::Editor;
	if (E.bHasEditorWorld)
	{
		E.Map = World->GetOutermost()->GetName();
		E.bForceNoPrecomputedLighting = World->GetWorldSettings()->bForceNoPrecomputedLighting;
		if (World->Scene) E.bRayTracingActive = IsRayTracingEnabled(World->Scene->GetShaderPlatform());
	}
	// Do not use GEditor->GetActiveViewport(): it can be this tool's asset preview viewport.
	if (FLevelEditorModule* LevelEditor = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor")))
	{
		if (TSharedPtr<SLevelViewport> Viewport = LevelEditor->GetFirstActiveLevelViewport())
		{
			const FLevelEditorViewportClient& Client = Viewport->GetLevelViewportClient();
			E.bHasPerspectiveCamera = Client.IsPerspective() && Client.GetWorld() == World;
			E.bCameraRealtime = Client.IsRealtime();
			E.CameraLocation = Client.GetViewLocation();
			E.CameraRotation = Client.GetViewRotation();
			E.CameraFOV = Client.ViewFOV;
		}
	}
	// Report file/runtime differences without applying CVars or saving renderer settings.
	const TCHAR* Names[] = {TEXT("r.AllowStaticLighting"), TEXT("r.VirtualTextures"), TEXT("r.VirtualTexturedLightmaps"), TEXT("r.RayTracing")};
	for (const TCHAR* Name : Names)
	{
		bool Stored = false;
		const int32 Active = CVar(Name);
		if (GConfig && Active >= 0 && GConfig->GetBool(TEXT("/Script/Engine.RendererSettings"), Name, Stored, GEngineIni)
			&& Stored != (Active != 0))
		{
			E.PendingSettings.Add(FString::Printf(TEXT("%s: 설정 파일 %d / 현재 실행 %d"), Name, Stored ? 1 : 0, Active));
		}
	}
	return E;
}

FLightmapBakeReadiness LightmapBake::Evaluate(const FLightmapBakeEnvironment& E)
{
	FLightmapBakeReadiness R;
	R.Environment = E;
	const auto Add = [&R](const TCHAR* Name, bool bPass, const FString& Detail)
	{
		R.Checks.Add({Name, bPass ? ELightmapBakeCheck::Pass : ELightmapBakeCheck::Blocked, Detail});
	};
	const auto ValueText = [](int32 Value) { return Value < 0 ? FString(TEXT("확인 불가")) : FString::FromInt(Value); };
	Add(TEXT("GPU Lightmass 플러그인"), E.bPluginInstalled && E.bPluginEnabled && E.bGPUModuleLoaded,
		!E.bPluginInstalled ? TEXT("엔진에서 찾을 수 없습니다.") : !E.bPluginEnabled ? TEXT("비활성화 상태입니다. Plugins에서 활성화 후 재시작이 필요합니다.")
		: !E.bGPUModuleLoaded ? TEXT("활성화됐지만 실행 모듈이 로드되지 않았습니다. 재시작/빌드 상태를 확인하세요.") : TEXT("활성화·로드됨"));
	Add(TEXT("정적 조명"), E.AllowStaticLighting == 1,
		TEXT("Allow Static Lighting = ") + ValueText(E.AllowStaticLighting) + TEXT(". 변경 후 에디터 재시작이 필요합니다."));
	Add(TEXT("Virtual Texture"), E.VirtualTextures == 1,
		TEXT("Virtual Texture Support = ") + ValueText(E.VirtualTextures) + TEXT(". BWYS의 부분 타일 검수에 필요합니다."));
	Add(TEXT("Virtual Texture Lightmaps"), E.VirtualTextureLightmaps == 1,
		TEXT("Virtual Texture Lightmaps = ") + ValueText(E.VirtualTextureLightmaps));
	Add(TEXT("DirectX 12"), E.bDX12, TEXT("현재 RHI: ") + E.RHI);
	Add(TEXT("하드웨어 Ray Tracing"), E.RayTracing == 1 && E.bRayTracingHardware && E.bRayTracingActive,
		FString::Printf(TEXT("설정 %s / RHI 지원 %s / 실행 활성 %s | GPU: %s"), *ValueText(E.RayTracing),
			E.bRayTracingHardware ? TEXT("YES") : TEXT("NO"), E.bRayTracingActive ? TEXT("YES") : TEXT("NO"), *E.GPU));
	Add(TEXT("에디터 월드"), E.bHasEditorWorld && !E.bPIE,
		E.bPIE ? TEXT("PIE를 종료하세요.") : E.bHasEditorWorld ? E.Map : TEXT("편집 중인 레벨을 찾을 수 없습니다."));
	Add(TEXT("레벨의 정적 조명 허용"), E.bHasEditorWorld && !E.bForceNoPrecomputedLighting,
		E.bForceNoPrecomputedLighting ? TEXT("Force No Precomputed Lighting ON — 베이크가 차단됩니다.")
		: E.bHasEditorWorld ? TEXT("Force No Precomputed Lighting OFF") : TEXT("현재 레벨 확인 불가"));
	Add(TEXT("정적 라이트맵 결과 표시"), E.GIMethod >= 0 && E.GIMethod != 1,
		E.GIMethod == 1 ? TEXT("기본 GI가 Lumen입니다. 정적 라이트맵 결과 검수에 적합하지 않습니다.")
		: E.GIMethod < 0 ? TEXT("GI 방식을 확인할 수 없습니다.") : TEXT("기본 GI가 Lumen 이외의 방식입니다. PP Volume/카메라 재정의는 별도 확인하세요."));
	Add(TEXT("실제 레벨 카메라"), E.bHasPerspectiveCamera,
		E.bHasPerspectiveCamera ? FString::Printf(TEXT("위치 %s | 회전 %s | FOV %.1f"), *E.CameraLocation.ToCompactString(), *E.CameraRotation.ToCompactString(), E.CameraFOV)
		: TEXT("레벨의 Perspective 뷰포트를 한 번 클릭한 뒤 다시 검사하세요. 도구의 메시 미리보기는 대상이 아닙니다."));
	if (E.bHasPerspectiveCamera)
	{
		R.Checks.Add({TEXT("레벨 뷰포트 실시간 갱신"), E.bCameraRealtime ? ELightmapBakeCheck::Pass : ELightmapBakeCheck::Warning,
			E.bCameraRealtime ? TEXT("ON — 카메라 이동에 따른 BWYS 검수 가능") : TEXT("OFF — BWYS로 이동하며 검수할 때는 엔진 패널에서 실시간 갱신을 켜세요.")});
	}
	if (!E.PendingSettings.IsEmpty())
	{
		R.Checks.Add({TEXT("실행 설정과 파일 차이"), ELightmapBakeCheck::Warning,
			FString::Join(E.PendingSettings, TEXT(" / ")) + TEXT(". 재시작 대기 또는 런타임 재정의일 수 있습니다.")});
	}
	return R;
}

bool FLightmapBakeReadiness::IsEnvironmentReady() const
{
	return !Checks.IsEmpty() && !Checks.ContainsByPredicate([](const FLightmapBakeCheck& C) { return C.State == ELightmapBakeCheck::Blocked; });
}

FString FLightmapBakeReadiness::ToJson() const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schema_version"), 1);
	Root->SetStringField(TEXT("report_type"), TEXT("BWYS_environment_readiness_only"));
	Root->SetStringField(TEXT("captured_utc"), FDateTime::UtcNow().ToIso8601());
	Root->SetBoolField(TEXT("environment_ready"), IsEnvironmentReady());
	Root->SetBoolField(TEXT("bake_executed"), false);
	Root->SetStringField(TEXT("map"), Environment.Map);
	Root->SetStringField(TEXT("rhi"), Environment.RHI);
	Root->SetStringField(TEXT("gpu"), Environment.GPU);
	Root->SetStringField(TEXT("camera_source"), TEXT("last_active_level_perspective_viewport"));
	Root->SetBoolField(TEXT("camera_available"), Environment.bHasPerspectiveCamera);
	TSharedRef<FJsonObject> Camera = MakeShared<FJsonObject>();
	Camera->SetNumberField(TEXT("x"), Environment.CameraLocation.X);
	Camera->SetNumberField(TEXT("y"), Environment.CameraLocation.Y);
	Camera->SetNumberField(TEXT("z"), Environment.CameraLocation.Z);
	Camera->SetNumberField(TEXT("pitch"), Environment.CameraRotation.Pitch);
	Camera->SetNumberField(TEXT("yaw"), Environment.CameraRotation.Yaw);
	Camera->SetNumberField(TEXT("roll"), Environment.CameraRotation.Roll);
	Camera->SetNumberField(TEXT("fov"), Environment.CameraFOV);
	Camera->SetBoolField(TEXT("realtime"), Environment.bCameraRealtime);
	Root->SetObjectField(TEXT("camera"), Camera);
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FLightmapBakeCheck& C : Checks)
	{
		TSharedRef<FJsonObject> Check = MakeShared<FJsonObject>();
		Check->SetStringField(TEXT("name"), C.Name);
		Check->SetStringField(TEXT("state"), StateName(C.State));
		Check->SetStringField(TEXT("detail"), C.Detail);
		Values.Add(MakeShared<FJsonValueObject>(Check));
	}
	Root->SetArrayField(TEXT("checks"), Values);
	FString Output;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Output));
	return Output;
}

bool LightmapBake::CanOpenNativePanel()
{
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("GPULightmass")) || !FModuleManager::Get().IsModuleLoaded(TEXT("GPULightmassEditor"))) return false;
	FLevelEditorModule* Module = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor"));
	const TSharedPtr<FTabManager> Manager = Module ? Module->GetLevelEditorTabManager() : nullptr;
	return Manager && Manager->HasTabSpawner(NativePanelName);
}

bool LightmapBake::OpenNativePanel()
{
	if (!CanOpenNativePanel() || !GEditor || GEditor->PlayWorld) return false;
	FLevelEditorModule& Module = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
	return Module.GetLevelEditorTabManager()->TryInvokeTab(NativePanelName).IsValid();
}

bool LightmapBake::SaveReport(const FLightmapBakeReadiness& Report, FString& OutPath)
{
	const FString Folder = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("LightmapUVHelper/Readiness"));
	OutPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(Folder,
		TEXT("BWYS_") + FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ_")) + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".json")));
	return IFileManager::Get().MakeDirectory(*Folder, true)
		&& FFileHelper::SaveStringToFile(Report.ToJson(), *OutPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
