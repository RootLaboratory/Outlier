#pragma once

#include "Modules/ModuleManager.h"

class FSpawnTabArgs;
class SDockTab;

class FAudioTagHelperModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterMenus();
	void OpenToolWindow();
	TSharedRef<SDockTab> SpawnToolTab(const FSpawnTabArgs& SpawnTabArgs);
};
