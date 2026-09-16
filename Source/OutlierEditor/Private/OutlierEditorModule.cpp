#include "Modules/ModuleManager.h"

#include "Editor.h"
#include "EnhancedInputDeveloperSettings.h"
#include "Framework/Docking/TabManager.h"
#include "Kismet/GameplayStatics.h"
#include "LevelEditor.h"
#include "Misc/Paths.h"
#include "OutlierInputMappableToolWidget.h"
#include "OutlierUpgradeEffectToolWidget.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "FOutlierEditorModule"

namespace OutlierEditor
{
	const FName InputMappableToolTabName(TEXT("OutlierInputMappableTool"));
	const FName UpgradeEffectToolTabName(TEXT("OutlierUpgradeEffectTool"));
	const FString ListenServerStartMap(TEXT("/Game/Maps/Title"));
}

class FOutlierEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
			OutlierEditor::InputMappableToolTabName,
			FOnSpawnTab::CreateRaw(this, &FOutlierEditorModule::SpawnInputMappableToolTab))
			.SetDisplayName(LOCTEXT("InputMappableToolTabTitle", "Outlier Input Mappable Tool"))
			.SetTooltipText(LOCTEXT("InputMappableToolTooltip", "Batch edit Player Mappable Key Settings on Input Mapping Context mappings."))
			.SetMenuType(ETabSpawnerMenuType::Hidden);

		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
			OutlierEditor::UpgradeEffectToolTabName,
			FOnSpawnTab::CreateRaw(this, &FOutlierEditorModule::SpawnUpgradeEffectToolTab))
			.SetDisplayName(LOCTEXT("UpgradeEffectToolTabTitle", "Outlier Upgrade Effect Tool"))
			.SetTooltipText(LOCTEXT("UpgradeEffectToolTooltip", "Author upgrade effect rows with dropdowns; writes raw CSV and reimports the DataTable."))
			.SetMenuType(ETabSpawnerMenuType::Hidden);

		UToolMenus::RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FOutlierEditorModule::RegisterMenus));

		FEditorDelegates::BeginPIE.AddRaw(
			this,
			&FOutlierEditorModule::HandleBeginPIE);
	}

	virtual void ShutdownModule() override
	{
		FEditorDelegates::BeginPIE.RemoveAll(this);

		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);

		if (FSlateApplication::IsInitialized())
		{
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(
				OutlierEditor::InputMappableToolTabName);
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(
				OutlierEditor::UpgradeEffectToolTabName);
		}
	}

private:
	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);

		UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu(TEXT("MainFrame.MainMenu.Tools"));
		FToolMenuSection& Section = ToolsMenu->FindOrAddSection(TEXT("Outlier"));
		Section.AddMenuEntry(
			TEXT("OpenOutlierInputMappableTool"),
			LOCTEXT("OpenInputMappableToolLabel", "Outlier Input Mappable Tool"),
			LOCTEXT("OpenInputMappableToolTooltip", "Scan IMCs and batch-enable Player Mappable Key Settings."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FOutlierEditorModule::OpenInputMappableTool)));

		Section.AddMenuEntry(
			TEXT("OpenOutlierUpgradeEffectTool"),
			LOCTEXT("OpenUpgradeEffectToolLabel", "Outlier Upgrade Effect Tool"),
			LOCTEXT("OpenUpgradeEffectToolTooltip", "Author upgrade effect rows with dropdowns; writes CSV and reimports."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FOutlierEditorModule::OpenUpgradeEffectTool)));

		UToolMenu* PlayToolBar = UToolMenus::Get()->ExtendMenu(
			TEXT("LevelEditor.LevelEditorToolBar.PlayToolBar"));
		FToolMenuSection& PlaySection = PlayToolBar->FindOrAddSection(TEXT("Play"));
		PlaySection.AddSeparator(TEXT("OutlierPlayModesSeparator"));

		PlaySection.AddEntry(FToolMenuEntry::InitToolBarButton(
			TEXT("OutlierPlayStandalone1P"),
			FUIAction(
				FExecuteAction::CreateRaw(this, &FOutlierEditorModule::StartStandalonePlaySession),
				FCanExecuteAction::CreateRaw(this, &FOutlierEditorModule::CanStartPlaySession)),
			LOCTEXT("PlayStandalone1PLabel", "Standalone 1P"),
			LOCTEXT("PlayStandalone1PTooltip", "Start the current map as a one-player Standalone PIE session."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("PlayWorld.PlayInNewProcess"))));

		PlaySection.AddEntry(FToolMenuEntry::InitToolBarButton(
			TEXT("OutlierPlayListen2P"),
			FUIAction(
				FExecuteAction::CreateRaw(this, &FOutlierEditorModule::StartListenServerPlaySession),
				FCanExecuteAction::CreateRaw(this, &FOutlierEditorModule::CanStartPlaySession)),
			LOCTEXT("PlayListen2PLabel", "Listen 2P"),
			LOCTEXT("PlayListen2PTooltip", "Start the Title map as a two-player Listen Server PIE session."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.GroupActors"))));
	}

	void OpenInputMappableTool()
	{
		FGlobalTabmanager::Get()->TryInvokeTab(OutlierEditor::InputMappableToolTabName);
	}

	void OpenUpgradeEffectTool()
	{
		FGlobalTabmanager::Get()->TryInvokeTab(OutlierEditor::UpgradeEffectToolTabName);
	}

	bool CanStartPlaySession() const
	{
		return GEditor && !GEditor->IsPlaySessionInProgress();
	}

	void StartStandalonePlaySession()
	{
		StartPlaySession(EPlayNetMode::PIE_Standalone, 1, FString());
	}

	void StartListenServerPlaySession()
	{
		StartPlaySession(EPlayNetMode::PIE_ListenServer, 2, OutlierEditor::ListenServerStartMap);
	}

	void StartPlaySession(EPlayNetMode NetMode, int32 PlayerCount, const FString& MapOverride)
	{
		if (!CanStartPlaySession())
		{
			return;
		}

		ULevelEditorPlaySettings* PlaySettings = DuplicateObject<ULevelEditorPlaySettings>(
			GetDefault<ULevelEditorPlaySettings>(),
			GetTransientPackage());
		PlaySettings->SetPlayNetMode(NetMode);
		PlaySettings->SetPlayNumberOfClients(PlayerCount);
		PlaySettings->SetRunUnderOneProcess(true);
		PlaySettings->bLaunchSeparateServer = false;
		PlaySettings->LastExecutedPlayModeType = PlayMode_InEditorFloating;

		FRequestPlaySessionParams Params;
		Params.EditorPlaySettings = PlaySettings;
		Params.GlobalMapOverride = MapOverride;

		FLevelEditorModule& LevelEditorModule =
			FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		if (TSharedPtr<IAssetViewport> ActiveViewport = LevelEditorModule.GetFirstActiveViewport())
		{
			Params.DestinationSlateViewport = ActiveViewport;
		}

		GEditor->RequestPlaySession(Params);
	}

	void HandleBeginPIE(bool bIsSimulating)
	{
		(void)bIsSimulating;
		FlushEnhancedInputUserSettingsSave();
	}

	void FlushEnhancedInputUserSettingsSave() const
	{
		const FString SlotName =
			GetDefault<UEnhancedInputDeveloperSettings>()->InputSettingsSaveSlotName;
		if (SlotName.IsEmpty())
		{
			return;
		}

		for (int32 UserIndex = 0; UserIndex < 4; ++UserIndex)
		{
			UGameplayStatics::DeleteGameInSlot(SlotName, UserIndex);
		}

		const FString SaveGamePath = FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("SaveGames"),
			SlotName + TEXT(".sav"));
		IFileManager::Get().Delete(*SaveGamePath, false, true);

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[OutlierEditor] Flushed Enhanced Input user settings save: %s"),
			*SaveGamePath);
	}

	TSharedRef<SDockTab> SpawnInputMappableToolTab(const FSpawnTabArgs& SpawnTabArgs)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SOutlierInputMappableToolWidget)
			];
	}

	TSharedRef<SDockTab> SpawnUpgradeEffectToolTab(const FSpawnTabArgs& SpawnTabArgs)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SOutlierUpgradeEffectToolWidget)
			];
	}
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FOutlierEditorModule, OutlierEditor)
