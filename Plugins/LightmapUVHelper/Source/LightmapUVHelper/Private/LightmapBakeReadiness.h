#pragma once

#include "CoreMinimal.h"

enum class ELightmapBakeCheck : uint8 { Pass, Warning, Blocked };

struct FLightmapBakeCheck
{
	FString Name;
	ELightmapBakeCheck State = ELightmapBakeCheck::Blocked;
	FString Detail;
};

/** Runtime values, not proposed renderer settings. Capturing never loads/enables GPU Lightmass. */
struct FLightmapBakeEnvironment
{
	bool bPluginInstalled = false;
	bool bPluginEnabled = false;
	bool bGPUModuleLoaded = false;
	bool bHasEditorWorld = false;
	bool bPIE = false;
	bool bForceNoPrecomputedLighting = false;
	bool bDX12 = false;
	bool bRayTracingHardware = false;
	bool bRayTracingActive = false;
	int32 AllowStaticLighting = -1;
	int32 VirtualTextures = -1;
	int32 VirtualTextureLightmaps = -1;
	int32 RayTracing = -1;
	int32 GIMethod = -1;
	bool bHasPerspectiveCamera = false;
	bool bCameraRealtime = false;
	FVector CameraLocation = FVector::ZeroVector;
	FRotator CameraRotation = FRotator::ZeroRotator;
	float CameraFOV = 0;
	FString Map;
	FString RHI;
	FString GPU;
	TArray<FString> PendingSettings;
};

struct FLightmapBakeReadiness
{
	FLightmapBakeEnvironment Environment;
	TArray<FLightmapBakeCheck> Checks;
	bool IsEnvironmentReady() const;
	FString ToJson() const;
};

namespace LightmapBake
{
	FLightmapBakeEnvironment CaptureEnvironment();
	FLightmapBakeReadiness Evaluate(const FLightmapBakeEnvironment& Environment);
	bool CanOpenNativePanel();
	bool OpenNativePanel();
	bool SaveReport(const FLightmapBakeReadiness& Report, FString& OutPath);
}
