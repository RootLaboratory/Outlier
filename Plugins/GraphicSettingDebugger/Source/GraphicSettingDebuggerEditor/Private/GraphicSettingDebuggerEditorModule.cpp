#include "Modules/ModuleManager.h"

#include "GraphicSettingDebuggerWidget.h"
#include "GraphicSettingDebuggerLogWidget.h"
#include "Framework/Docking/TabManager.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/Application/SlateApplication.h"

namespace GraphicSettingDebuggerEditor
{
	const FName TabName(TEXT("GraphicSettingDebugger"));
	const FName LogTabName(TEXT("GraphicSettingDebuggerLog"));
}

class FGraphicSettingDebuggerEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
			GraphicSettingDebuggerEditor::TabName,
			FOnSpawnTab::CreateRaw(this, &FGraphicSettingDebuggerEditorModule::SpawnTab))
			.SetDisplayName(FText::FromString(TEXT("Graphic Setting Debugger")))
			.SetTooltipText(FText::FromString(TEXT("Apply, compare, and snapshot graphics settings.")))
			.SetMenuType(ETabSpawnerMenuType::Hidden);
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
			GraphicSettingDebuggerEditor::LogTabName,
			FOnSpawnTab::CreateRaw(this, &FGraphicSettingDebuggerEditorModule::SpawnLogTab))
			.SetDisplayName(FText::FromString(TEXT("Graphic Setting Debugger Logs")))
			.SetTooltipText(FText::FromString(TEXT("Browse graphic setting snapshot slots and performance values.")))
			.SetMenuType(ETabSpawnerMenuType::Hidden);

		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FGraphicSettingDebuggerEditorModule::RegisterMenus));
	}

	virtual void ShutdownModule() override
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
		if (FSlateApplication::IsInitialized())
		{
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(GraphicSettingDebuggerEditor::TabName);
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(GraphicSettingDebuggerEditor::LogTabName);
		}
	}

private:
	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);
		UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu(TEXT("MainFrame.MainMenu.Tools"));
		FToolMenuSection& Section = ToolsMenu->FindOrAddSection(TEXT("Rendering"));
		Section.AddMenuEntry(
			TEXT("OpenGraphicSettingDebugger"),
			FText::FromString(TEXT("Graphic Setting Debugger")),
			FText::FromString(TEXT("Open the graphics setting apply/capture/compare debugger.")),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FGraphicSettingDebuggerEditorModule::OpenTab)));
		Section.AddMenuEntry(
			TEXT("OpenGraphicSettingDebuggerLogs"),
			FText::FromString(TEXT("Graphic Setting Debugger Logs")),
			FText::FromString(TEXT("Browse saved graphic setting snapshot slots and performance values.")),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FGraphicSettingDebuggerEditorModule::OpenLogTab)));
	}

	void OpenTab()
	{
		FGlobalTabmanager::Get()->TryInvokeTab(GraphicSettingDebuggerEditor::TabName);
		FGlobalTabmanager::Get()->TryInvokeTab(GraphicSettingDebuggerEditor::LogTabName);
	}
	void OpenLogTab() { FGlobalTabmanager::Get()->TryInvokeTab(GraphicSettingDebuggerEditor::LogTabName); }

	TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args)
	{
		return SNew(SDockTab).TabRole(ETabRole::NomadTab)[SNew(SGraphicSettingDebuggerWidget)];
	}

	TSharedRef<SDockTab> SpawnLogTab(const FSpawnTabArgs& Args)
	{
		return SNew(SDockTab).TabRole(ETabRole::NomadTab)[SNew(SGraphicSettingDebuggerLogWidget)];
	}
};

IMPLEMENT_MODULE(FGraphicSettingDebuggerEditorModule, GraphicSettingDebuggerEditor)
