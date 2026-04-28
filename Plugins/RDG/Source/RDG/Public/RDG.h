// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FRDGBuilder;
class FRDGTexture;
class SWindow;
class ULocalPlayerPostProcessSubsystem;
class FRDGDebugWindowManager;

class FRDGModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void HandleBackBufferReadyRDG(FRDGBuilder& GraphBuilder, SWindow& Window, FRDGTexture* BackBuffer);
	ULocalPlayerPostProcessSubsystem* ResolvePostProcessSubsystem();
	void RegisterSlateHook();
	bool IsTargetGameWindow(const SWindow& Window) const;
	void RequestOpenDebugWindow();

	FDelegateHandle BackBufferReadyHandle;
	TWeakObjectPtr<ULocalPlayerPostProcessSubsystem> CachedPostProcessSubsystem;
	TUniquePtr<FRDGDebugWindowManager> DebugWindowManager;
	TAtomic<bool> bDebugWindowOpenRequested = false;
};
