// Copyright Epic Games, Inc. All Rights Reserved.

#include "RDG.h"

#include "Debug/RDGDebugWindowManager.h"
#include "FRDGPixelSortingPass.h"
#include "FRDGSceneColorCopyPass.h"
#include "FRDGUIChromaticAberrationPass.h"
#include "FRDGZoomBlurPass.h"
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

#if WITH_EDITOR
#include "ToolMenus.h"
#endif

#define LOCTEXT_NAMESPACE "FRDGModule"

void FRDGModule::StartupModule()
{
	DebugWindowManager = MakeUnique<FRDGDebugWindowManager>();

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

#if WITH_EDITOR
	if (!IsRunningCommandlet())
	{
		UToolMenus::RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FRDGModule::RegisterMenus));
	}
#endif
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

#if WITH_EDITOR
void FRDGModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	if (UToolMenu* WindowMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Window")))
	{
		FToolMenuSection& Section = WindowMenu->FindOrAddSection(
			TEXT("RDG"),
			LOCTEXT("RDGMenuSection", "RDG"));

		Section.AddMenuEntry(
			TEXT("OpenRDGGraphicsDebugger"),
			LOCTEXT("OpenRDGGraphicsDebugger", "RDG Graphics Debugger"),
			LOCTEXT("OpenRDGGraphicsDebuggerTooltip", "Open the RDG post-process debugger window."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FRDGModule::OpenDebugWindowFromMenu)));
	}
}

void FRDGModule::OpenDebugWindowFromMenu()
{
	if (DebugWindowManager.IsValid())
	{
		DebugWindowManager->OpenWindow();
	}
}
#endif

void FRDGModule::ShutdownModule()
{
#if WITH_EDITOR
	if (UToolMenus::IsToolMenuUIEnabled())
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
	}
#endif

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
}

// Slate가 그린 뒤, present 직전의 backbuffer에 도는 체인:
// Pixel Sorting -> Zoom Blur -> Chromatic Aberration -> Present
//
// 줌 블러가 정렬보다 뒤인 이유: 먼저 흐리면 대비가 뭉개져 임계값을 넘는 픽셀이
// 줄어들어 정렬 결과가 약해짐. 반대 순서면 줄무늬가 살아있는 채로 부드러워지고,
// 정렬의 축소 합성 경계까지 자연스럽게 덮어줌.
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

	Subsystem->TickFrame();

	const FPixelSortingParameters& PixelSortingParams =
		Subsystem->GetPostProcessStrcture().PixelSorting;
	const FZoomBlurParameters& ZoomBlurParams =
		Subsystem->GetPostProcessStrcture().ZoomBlur;
	const FUIChromaticAberrationParameters& ChromaticParams =
		Subsystem->GetUIPostProcessStrcture().ChromaticAberration;

	if (!PixelSortingParams.bEnabled && !ZoomBlurParams.bEnabled && !ChromaticParams.bEnabled)
	{
		return;
	}

	FScreenPassTexture Current(
		BackBuffer,
		FIntRect(FIntPoint::ZeroValue, BackBuffer->Desc.Extent));

	Current = FRDGPixelSortingPass::AddPass(GraphBuilder, Current, PixelSortingParams);
	Current = FRDGZoomBlurPass::AddPass(GraphBuilder, Current, ZoomBlurParams);
	Current = FRDGUIChromaticAberrationPass::AddPass(GraphBuilder, Current, ChromaticParams);

	if (!Current.IsValid() || Current.Texture == BackBuffer)
	{
		return;
	}

	// 픽셀 소팅 출력은 UAV를 쓸 수 있는 PF_R8G8B8A8이라 backbuffer 포맷(PF_B8G8R8A8 등)과
	// 다를 수 있음. 단순 복사 대신 셰이더 blit으로 넘겨서 포맷/스위즐 변환까지 처리함.
	FScreenPassRenderTarget BackBufferTarget(
		BackBuffer,
		FIntRect(FIntPoint::ZeroValue, BackBuffer->Desc.Extent),
		ERenderTargetLoadAction::ENoAction);

	FRDGSceneColorCopyPass::AddPass(GraphBuilder, Current, BackBufferTarget);
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FRDGModule, RDG)
