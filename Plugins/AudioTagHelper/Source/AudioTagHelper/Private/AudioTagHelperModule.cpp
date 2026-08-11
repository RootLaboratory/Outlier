#include "AudioTagHelperModule.h"

#include "AudioTagHelperPanel.h"
#include "Framework/Docking/TabManager.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "FAudioTagHelperModule"

namespace AudioTagHelper
{
	const FName ToolTabName(TEXT("AudioTagHelper"));
}

void FAudioTagHelperModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		AudioTagHelper::ToolTabName,
		FOnSpawnTab::CreateRaw(this, &FAudioTagHelperModule::SpawnToolTab))
		.SetDisplayName(LOCTEXT("ToolTabTitle", "Audio Tag Helper"))
		.SetTooltipText(LOCTEXT("ToolTabTooltip", "Create audio Event and Context tags and bound data assets."))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FAudioTagHelperModule::RegisterMenus));
}

void FAudioTagHelperModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(AudioTagHelper::ToolTabName);
	}
}

void FAudioTagHelperModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu(TEXT("MainFrame.MainMenu.Tools"));
	FToolMenuSection& Section = ToolsMenu->FindOrAddSection(TEXT("Audio"));
	Section.AddMenuEntry(
		TEXT("OpenAudioTagHelper"),
		LOCTEXT("OpenToolLabel", "Audio Tag Helper"),
		LOCTEXT("OpenToolTooltip", "Create audio Event and Context tags in AudioTags.ini and bind them to an audio event Data Asset."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FAudioTagHelperModule::OpenToolWindow)));
}

void FAudioTagHelperModule::OpenToolWindow()
{
	FGlobalTabmanager::Get()->TryInvokeTab(AudioTagHelper::ToolTabName);
}

TSharedRef<SDockTab> FAudioTagHelperModule::SpawnToolTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SAudioTagHelperPanel)
		];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAudioTagHelperModule, AudioTagHelper)
