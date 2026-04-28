// Copyright Epic Games, Inc. All Rights Reserved.

#include "RDG.h"

#include "Async/Async.h"
#include "Debug/RDGDebugWindowManager.h"
#include "FRDGUIChromaticAberrationPass.h"
#include "Framework/Application/SlateApplication.h"
#include "LocalPlayerPostProcessSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "RenderGraphUtils.h"
#include "Rendering/SlateRenderer.h"
#include "ScreenPass.h"
#include "ShaderCore.h"

#define LOCTEXT_NAMESPACE "FRDGModule"

void FRDGModule::StartupModule()
{
	DebugWindowManager = MakeUnique<FRDGDebugWindowManager>();
	bDebugWindowOpenRequested = false;

	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("RDG"));
	if (!Plugin.IsValid())
	{
		return;
	}

	const FString ShaderDirectory = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shader"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/RDG"), ShaderDirectory);

	if (FSlateApplication::IsInitialized())
	{
		RegisterSlateHook();
	}

	FCoreDelegates::OnPostEngineInit.AddRaw(this, &FRDGModule::RegisterSlateHook);
}

bool FRDGModule::IsTargetGameWindow(const SWindow& Window) const
{
	if (!GEngine || !GEngine->GameViewport)
	{
		return false;
	}

	TSharedPtr<SWindow> GameWindow = GEngine->GameViewport->GetWindow();
	if (!GameWindow.IsValid())
	{
		return false;
	}

	return GameWindow.Get() == &Window;
}

void FRDGModule::RegisterSlateHook()
{
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}

	if (!BackBufferReadyHandle.IsValid())
	{
		if (FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer())
		{
			BackBufferReadyHandle =
				Renderer->OnAddBackBufferReadyToPresentPass().AddRaw(
					this,
					&FRDGModule::HandleBackBufferReadyRDG);
		}
	}
}

void FRDGModule::RequestOpenDebugWindow()
{
	if (bDebugWindowOpenRequested.Exchange(true))
	{
		return;
	}

	AsyncTask(ENamedThreads::GameThread, [this]()
	{
		if (DebugWindowManager.IsValid())
		{
			DebugWindowManager->OpenWindow();
		}
	});
}

ULocalPlayerPostProcessSubsystem* FRDGModule::ResolvePostProcessSubsystem()
{
	if (CachedPostProcessSubsystem.IsValid())
	{
		return CachedPostProcessSubsystem.Get();
	}

	if (!GEngine)
	{
		return nullptr;
	}

	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		UWorld* World = WorldContext.World();
		if (!World || !(World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::GamePreview))
		{
			continue;
		}

		UGameInstance* GameInstance = World->GetGameInstance();
		if (!GameInstance)
		{
			continue;
		}

		ULocalPlayer* LocalPlayer = GameInstance->GetFirstGamePlayer();
		if (!LocalPlayer)
		{
			continue;
		}

		if (ULocalPlayerPostProcessSubsystem* Subsystem = LocalPlayer->GetSubsystem<ULocalPlayerPostProcessSubsystem>())
		{
			UE_LOG(LogTemp, Warning, TEXT("RDG ResolvePostProcessSubsystem | World=%s Type=%d LocalPlayer=%p Subsystem=%p"),
				*World->GetName(),
				(int32)World->WorldType,
				LocalPlayer,
				Subsystem);

			CachedPostProcessSubsystem = Subsystem;
			return Subsystem;
		}
	}

	return nullptr;
}

void FRDGModule::ShutdownModule()
{
	if (DebugWindowManager.IsValid())
	{
		DebugWindowManager->CloseWindow();
	}

	if (FSlateApplication::IsInitialized())
	{
		if (FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer())
		{
			Renderer->OnAddBackBufferReadyToPresentPass().Remove(BackBufferReadyHandle);
		}
	}

	BackBufferReadyHandle.Reset();
	CachedPostProcessSubsystem.Reset();
	DebugWindowManager.Reset();
	bDebugWindowOpenRequested = false;
}

void FRDGModule::HandleBackBufferReadyRDG(FRDGBuilder& GraphBuilder, SWindow& Window, FRDGTexture* BackBuffer)
{
	if (!BackBuffer)
	{
		return;
	}

	if (!IsTargetGameWindow(Window))
	{
		return;
	}

	ULocalPlayerPostProcessSubsystem* Subsystem = ResolvePostProcessSubsystem();
	if (!Subsystem)
	{
		return;
	}

	RequestOpenDebugWindow();

	Subsystem->TickFrame();

	const FUIChromaticAberrationParameters& Params =
		Subsystem->GetUIPostProcessStrcture().ChromaticAberration;

	if (!Params.bEnabled)
	{
		return;
	}

	FScreenPassTexture Input(
		BackBuffer,
		FIntRect(FIntPoint::ZeroValue, BackBuffer->Desc.Extent));

	FScreenPassTexture Output =
		FRDGUIChromaticAberrationPass::AddPass(GraphBuilder, Input, Params);

	if (Output.IsValid() && Output.Texture != BackBuffer)
	{
		AddCopyTexturePass(GraphBuilder, Output.Texture, BackBuffer);
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FRDGModule, RDG)
