#include "GraphicSettingDebuggerSubsystem.h"

#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Json.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"
#include "DynamicRHI.h"

namespace GraphicSettingDebuggerPrivate
{
	FString SanitizeLabel(const FString& InLabel)
	{
		FString Result = InLabel;
		for (TCHAR& Character : Result)
		{
			if (!FChar::IsAlnum(Character) && Character != TEXT('_') && Character != TEXT('-'))
			{
				Character = TEXT('_');
			}
		}
		return Result.IsEmpty() ? TEXT("capture") : Result;
	}

	FString GetOutputDirectory()
	{
		const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("GraphicSettingDebugger"));
		IFileManager::Get().MakeDirectory(*Directory, true);
		return Directory;
	}
}

bool UGraphicSettingDebuggerSubsystem::ApplySetting(const FName Key, const FString& Value)
{
	IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*Key.ToString());
	if (!Variable)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GraphicSettingDebugger] Unknown console variable: %s"), *Key.ToString());
		return false;
	}

	Variable->Set(*Value, ECVF_SetByCode);
	return true;
}

FString UGraphicSettingDebuggerSubsystem::GetSettingValue(const FName Key) const
{
	if (const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*Key.ToString()))
	{
		return Variable->GetString();
	}
	return FString();
}

bool UGraphicSettingDebuggerSubsystem::IsSettingActive(const FName Key) const
{
	const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*Key.ToString());
	return Variable && Variable->GetFlags() != ECVF_Unregistered;
}

bool UGraphicSettingDebuggerSubsystem::SaveSnapshot(const FString& Label)
{
	const FString FilePath = FPaths::Combine(
		GraphicSettingDebuggerPrivate::GetOutputDirectory(),
		FString::Printf(TEXT("snapshot_%s_%s.json"), *GraphicSettingDebuggerPrivate::SanitizeLabel(Label), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("label"), Label);
	Root->SetStringField(TEXT("timestampUtc"), FDateTime::UtcNow().ToIso8601());
	const double DeltaTime = FApp::GetDeltaTime();
	Root->SetNumberField(TEXT("fps"), DeltaTime > SMALL_NUMBER ? 1.0 / DeltaTime : 0.0);
	Root->SetNumberField(TEXT("frameMs"), DeltaTime * 1000.0);
	Root->SetNumberField(TEXT("gpuFrameMs"), FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));

	TSharedRef<FJsonObject> Settings = MakeShared<FJsonObject>();
	for (const TCHAR* Key : {
		TEXT("r.Lumen.HardwareRayTracing"), TEXT("r.Lumen.Reflections.Allow"), TEXT("r.Lumen.Reflections.SmoothBias"),
		TEXT("r.RayTracing"), TEXT("r.RayTracing.Reflections"), TEXT("r.Translucency"), TEXT("r.Translucency.VolumeBlur"),
		TEXT("r.TranslucencyLightingVolumeDim"), TEXT("r.ScreenPercentage"), TEXT("r.DynamicRes.OperationMode"),
		TEXT("sg.ViewDistanceQuality"), TEXT("sg.AntiAliasingQuality"), TEXT("sg.ShadowQuality"),
		TEXT("sg.GlobalIlluminationQuality"), TEXT("sg.ReflectionQuality"), TEXT("sg.PostProcessQuality"),
		TEXT("sg.TextureQuality"), TEXT("sg.EffectsQuality"), TEXT("sg.FoliageQuality"), TEXT("sg.ShadingQuality") })
	{
		if (const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Key))
		{
			Settings->SetStringField(Key, Variable->GetString());
		}
	}
	Root->SetObjectField(TEXT("settings"), Settings);

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	if (!FJsonSerializer::Serialize(Root, Writer) || !FFileHelper::SaveStringToFile(Json, *FilePath))
	{
		return false;
	}

	LastSnapshotPath = FilePath;
	UE_LOG(LogTemp, Display, TEXT("[GraphicSettingDebugger] Snapshot saved: %s"), *FilePath);
	return true;
}
