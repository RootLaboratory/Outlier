#include "Modules/ModuleManager.h"

#include "SLightmapUVPanel.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Application/SlateApplication.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "LightmapUVHelperModule"

class FLightmapUVHelperModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TEXT("LightmapUVHelper"),
			FOnSpawnTab::CreateRaw(this, &FLightmapUVHelperModule::SpawnTab))
			.SetDisplayName(LOCTEXT("Title", "Lightmap UV Helper"))
			.SetMenuType(ETabSpawnerMenuType::Hidden);
		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FLightmapUVHelperModule::RegisterMenus));
	}

	virtual void ShutdownModule() override
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
		if (FSlateApplication::IsInitialized())
		{
			if (TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->FindExistingLiveTab(FName(TEXT("LightmapUVHelper")))) Tab->RequestCloseTab();
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TEXT("LightmapUVHelper"));
		}
	}

private:
	void RegisterMenus()
	{
		FToolMenuOwnerScoped Owner(this);
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("MainFrame.MainMenu.Tools"));
		Menu->FindOrAddSection(TEXT("StaticMesh")).AddMenuEntry(TEXT("OpenLightmapUVHelper"),
			LOCTEXT("MenuLabel", "Lightmap UV Helper"),
			LOCTEXT("Tooltip", "폴더 안의 Static Mesh 라이트맵 UV 설정을 검사하고 일괄 적용합니다."), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([]() { FGlobalTabmanager::Get()->TryInvokeTab(FName(TEXT("LightmapUVHelper"))); })));
	}

	TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args)
	{
		return SNew(SDockTab).TabRole(ETabRole::NomadTab)[SNew(SLightmapUVPanel)];
	}
};

IMPLEMENT_MODULE(FLightmapUVHelperModule, LightmapUVHelper)
#undef LOCTEXT_NAMESPACE
