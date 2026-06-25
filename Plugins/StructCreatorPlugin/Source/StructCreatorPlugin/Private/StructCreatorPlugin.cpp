// Copyright Epic Games, Inc. All Rights Reserved.

#include "StructCreatorPlugin.h"
#include "StructCreatorPluginStyle.h"
#include "StructCreatorPluginCommands.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "DesktopPlatformModule.h"
#include "EditorFramework/AssetImportData.h"
#include "EditorReimportHandler.h"
#include "Engine/DataTable.h"
#include "Framework/Application/SlateApplication.h"
#include "GameProjectGenerationModule.h"
#include "HAL/FileManager.h"
#include "IDesktopPlatform.h"
#include "ISourceCodeAccessModule.h"
#include "ISourceCodeAccessor.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "ToolMenus.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

static const FName StructCreatorPluginTabName("StructCreatorPlugin");

#define LOCTEXT_NAMESPACE "FStructCreatorPluginModule"

namespace
{
	FString NormalizeStructBaseName(FString RawName)
	{
		RawName.TrimStartAndEndInline();

		if (RawName.StartsWith(TEXT("F")) && RawName.Len() > 1)
		{
			RawName.RightChopInline(1);
		}

		return RawName;
	}

	bool IsValidStructBaseName(const FString& StructBaseName)
	{
		if (StructBaseName.IsEmpty())
		{
			return false;
		}

		if (!FChar::IsAlpha(StructBaseName[0]) && StructBaseName[0] != TEXT('_'))
		{
			return false;
		}

		for (const TCHAR Character : StructBaseName)
		{
			if (!FChar::IsAlnum(Character) && Character != TEXT('_'))
			{
				return false;
			}
		}

		return true;
	}

	FString MakeStructHeaderContent(
		const FString& FileName,
		const FString& StructBaseName,
		bool bIsDataTableRow,
		bool bAddDisplayFields,
		bool bAddGameplayTag,
		bool bAddSoftObjectReference)
	{
		const FString ProjectName = FApp::GetProjectName();
		const FString ApiMacro = ProjectName.ToUpper() + TEXT("_API");
		FString ExtraIncludes;
		if (bIsDataTableRow)
		{
			ExtraIncludes += TEXT("#include \"Engine/DataTable.h\"");
		}
		if (bAddGameplayTag)
		{
			ExtraIncludes += TEXT("#include \"GameplayTagContainer.h\"");
		}
		if (bAddSoftObjectReference)
		{
			ExtraIncludes += TEXT("#include \"UObject/SoftObjectPtr.h\"");
		}

		const FString BaseType = bIsDataTableRow ? TEXT(" : public FTableRowBase") : TEXT("");
		FString Fields = TEXT("\tUPROPERTY(EditAnywhere, BlueprintReadWrite)\n\tint32 ID = 0;\n");

		if (bAddDisplayFields)
		{
			Fields += TEXT("\n\tUPROPERTY(EditAnywhere, BlueprintReadWrite)\n\tFText DisplayName;\n");
			Fields += TEXT("\n\tUPROPERTY(EditAnywhere, BlueprintReadWrite)\n\tFText Description;\n");
		}

		if (bAddGameplayTag)
		{
			Fields += TEXT("\n\tUPROPERTY(EditAnywhere, BlueprintReadWrite)\n\tFGameplayTag Tag;\n");
		}

		if (bAddSoftObjectReference)
		{
			Fields += TEXT("\n\tUPROPERTY(EditAnywhere, BlueprintReadWrite)\n\tTSoftObjectPtr<UObject> Asset;\n");
		}

		return FString::Printf(TEXT(R"(#pragma once

#include "CoreMinimal.h"
%s
#include "%s.generated.h"

USTRUCT(BlueprintType)
struct %s F%s%s
{
	GENERATED_BODY()

%s
};
)"),
			*ExtraIncludes,
			*FileName,
			*ApiMacro,
			*StructBaseName,
			*BaseType,
			*Fields);
	}

	FString ResolveCreateDirectory(FString RawDirectory)
	{
		RawDirectory.TrimStartAndEndInline();

		if (RawDirectory.IsEmpty())
		{
			return FString();
		}

		if (FPaths::IsRelative(RawDirectory))
		{
			RawDirectory = FPaths::Combine(FPaths::ProjectDir(), RawDirectory);
		}

		return FPaths::ConvertRelativePathToFull(RawDirectory);
	}

	bool RefreshCodeProject(FText& OutFailReason, FText& OutFailLog)
	{
		return FGameProjectGenerationModule::Get().UpdateCodeProject(OutFailReason, OutFailLog);
	}

	bool OpenHeaderInSourceCodeAccessor(const FString& HeaderPath)
	{
		ISourceCodeAccessModule& SourceCodeAccessModule = FModuleManager::LoadModuleChecked<ISourceCodeAccessModule>(TEXT("SourceCodeAccess"));
		ISourceCodeAccessor& SourceCodeAccessor = SourceCodeAccessModule.GetAccessor();

		if (!SourceCodeAccessor.CanAccessSourceCode())
		{
			SourceCodeAccessor.RefreshAvailability();
		}

		return SourceCodeAccessor.CanAccessSourceCode()
			&& SourceCodeAccessor.OpenFileAtLine(HeaderPath, 1);
	}

	bool HasCsvImportSource(const UDataTable* DataTable)
	{
		if (!DataTable || !DataTable->AssetImportData)
		{
			return false;
		}

		TArray<FString> SourceFilenames;
		DataTable->AssetImportData->ExtractFilenames(SourceFilenames);

		for (const FString& SourceFilename : SourceFilenames)
		{
			if (FPaths::GetExtension(SourceFilename).Equals(TEXT("csv"), ESearchCase::IgnoreCase))
			{
				return true;
			}
		}

		return false;
	}
}

void FStructCreatorPluginModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	
	FStructCreatorPluginStyle::Initialize();
	FStructCreatorPluginStyle::ReloadTextures();

	FStructCreatorPluginCommands::Register();
	
	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FStructCreatorPluginCommands::Get().PluginAction,
		FExecuteAction::CreateRaw(this, &FStructCreatorPluginModule::PluginButtonClicked),
		FCanExecuteAction());

	PluginCommands->MapAction(
		FStructCreatorPluginCommands::Get().ReimportAllCsvAction,
		FExecuteAction::CreateRaw(this, &FStructCreatorPluginModule::ReimportAllCsvButtonClicked),
		FCanExecuteAction());

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FStructCreatorPluginModule::RegisterMenus));

	if (UToolMenus::IsToolMenuUIEnabled())
	{
		RegisterMenus();
	}
}

void FStructCreatorPluginModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	UToolMenus::UnRegisterStartupCallback(this);

	UToolMenus::UnregisterOwner(this);

	FStructCreatorPluginStyle::Shutdown();

	FStructCreatorPluginCommands::Unregister();
}

void FStructCreatorPluginModule::PluginButtonClicked()
{
	TSharedPtr<SEditableTextBox> StructNameTextBox;
	TSharedPtr<SEditableTextBox> DirectoryTextBox;
	TSharedPtr<SCheckBox> DataTableRowCheckBox;
	TSharedPtr<SCheckBox> DisplayFieldsCheckBox;
	TSharedPtr<SCheckBox> GameplayTagCheckBox;
	TSharedPtr<SCheckBox> SoftObjectReferenceCheckBox;
	bool bCreateRequested = false;
	const FString DefaultDirectory = FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"), FApp::GetProjectName());

	TSharedRef<SWindow> DialogWindow = SNew(SWindow)
		.Title(LOCTEXT("CreateStructWindowTitle", "New C++ Struct..."))
		.ClientSize(FVector2D(560.0f, 350.0f))
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	DialogWindow->SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12.0f, 12.0f, 12.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("StructNameLabel", "Struct Name"))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12.0f, 0.0f, 12.0f, 8.0f)
		[
			SAssignNew(StructNameTextBox, SEditableTextBox)
			.HintText(LOCTEXT("StructNameHint", "ItemData or FItemData"))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12.0f, 2.0f, 12.0f, 8.0f)
		[
			SAssignNew(DataTableRowCheckBox, SCheckBox)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DataTableRowCheckBoxLabel", "DataTable Row (inherit FTableRowBase)"))
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12.0f, 0.0f, 12.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("CommonFieldsLabel", "Common Fields"))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12.0f, 0.0f, 12.0f, 4.0f)
		[
			SAssignNew(DisplayFieldsCheckBox, SCheckBox)
			.IsChecked(ECheckBoxState::Checked)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DisplayFieldsCheckBoxLabel", "DisplayName + Description"))
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12.0f, 0.0f, 12.0f, 4.0f)
		[
			SAssignNew(GameplayTagCheckBox, SCheckBox)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("GameplayTagCheckBoxLabel", "Gameplay Tag"))
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12.0f, 0.0f, 12.0f, 8.0f)
		[
			SAssignNew(SoftObjectReferenceCheckBox, SCheckBox)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("SoftObjectReferenceCheckBoxLabel", "Soft Object Reference"))
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12.0f, 4.0f, 12.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("StructDirectoryLabel", "Folder"))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12.0f, 0.0f, 12.0f, 8.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SAssignNew(DirectoryTextBox, SEditableTextBox)
				.Text(FText::FromString(DefaultDirectory))
				.HintText(LOCTEXT("StructDirectoryHint", "Source/Outlier or Source/Outlier/Weapon"))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("BrowseDirectoryButtonLabel", "Browse..."))
				.OnClicked_Lambda([DirectoryTextBox, DefaultDirectory]()
				{
					IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
					if (!DesktopPlatform || !DirectoryTextBox.IsValid())
					{
						return FReply::Handled();
					}

					FString SelectedDirectory;
					const bool bFolderSelected = DesktopPlatform->OpenDirectoryDialog(
						FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
						LOCTEXT("SelectStructDirectoryTitle", "Select Struct Folder").ToString(),
						DefaultDirectory,
						SelectedDirectory);

					if (bFolderSelected)
					{
						DirectoryTextBox->SetText(FText::FromString(SelectedDirectory));
					}

					return FReply::Handled();
				})
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(12.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("StructPathHint", "Relative paths are resolved from the project folder. Missing folders will be created."))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Right)
		.Padding(12.0f)
		[
			SNew(SUniformGridPanel)
			.SlotPadding(FMargin(4.0f, 0.0f))
			+ SUniformGridPanel::Slot(0, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("CreateButtonLabel", "Create"))
				.OnClicked_Lambda([&DialogWindow, &bCreateRequested]()
				{
					bCreateRequested = true;
					DialogWindow->RequestDestroyWindow();
					return FReply::Handled();
				})
			]
			+ SUniformGridPanel::Slot(1, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("CancelButtonLabel", "Cancel"))
				.OnClicked_Lambda([&DialogWindow]()
				{
					DialogWindow->RequestDestroyWindow();
					return FReply::Handled();
				})
			]
		]
	);

	FSlateApplication::Get().AddModalWindow(DialogWindow, nullptr);

	if (!bCreateRequested
		|| !StructNameTextBox.IsValid()
		|| !DirectoryTextBox.IsValid()
		|| !DataTableRowCheckBox.IsValid()
		|| !DisplayFieldsCheckBox.IsValid()
		|| !GameplayTagCheckBox.IsValid()
		|| !SoftObjectReferenceCheckBox.IsValid())
	{
		return;
	}

	const FString StructBaseName = NormalizeStructBaseName(StructNameTextBox->GetText().ToString());
	if (!IsValidStructBaseName(StructBaseName))
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			LOCTEXT("InvalidStructNameMessage", "Please enter a valid C++ struct name, such as ItemData or FItemData."));
		return;
	}

	const FString FileName = StructBaseName;
	const FString CreateDirectory = ResolveCreateDirectory(DirectoryTextBox->GetText().ToString());
	if (CreateDirectory.IsEmpty())
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			LOCTEXT("InvalidStructDirectoryMessage", "Please enter a folder for the new struct header."));
		return;
	}

	if (!FPaths::DirectoryExists(CreateDirectory) && !IFileManager::Get().MakeDirectory(*CreateDirectory, true))
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::Format(LOCTEXT("CreateStructDirectoryFailedMessage", "Could not create folder:\n{0}"), FText::FromString(CreateDirectory)));
		return;
	}

	const FString HeaderPath = FPaths::Combine(CreateDirectory, FileName + TEXT(".h"));

	if (FPaths::FileExists(HeaderPath))
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::Format(LOCTEXT("StructHeaderExistsMessage", "A header already exists at:\n{0}"), FText::FromString(HeaderPath)));
		return;
	}

	const bool bIsDataTableRow = DataTableRowCheckBox->IsChecked();
	const bool bAddDisplayFields = DisplayFieldsCheckBox->IsChecked();
	const bool bAddGameplayTag = GameplayTagCheckBox->IsChecked();
	const bool bAddSoftObjectReference = SoftObjectReferenceCheckBox->IsChecked();
	const FString HeaderContent = MakeStructHeaderContent(
		FileName,
		StructBaseName,
		bIsDataTableRow,
		bAddDisplayFields,
		bAddGameplayTag,
		bAddSoftObjectReference);
	if (!FFileHelper::SaveStringToFile(HeaderContent, *HeaderPath))
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::Format(LOCTEXT("StructHeaderSaveFailedMessage", "Failed to create struct header:\n{0}"), FText::FromString(HeaderPath)));
		return;
	}

	FText RefreshFailReason;
	FText RefreshFailLog;
	const bool bProjectRefreshed = RefreshCodeProject(RefreshFailReason, RefreshFailLog);
	const bool bHeaderOpened = OpenHeaderInSourceCodeAccessor(HeaderPath);

	if (!bProjectRefreshed)
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::Format(
				LOCTEXT("StructHeaderCreatedRefreshFailedMessage", "Created:\n{0}\n\nThe header was created, but refreshing the code project failed:\n{1}\n\nUse Tools > Refresh Visual Studio Project if it does not appear in the solution."),
				FText::FromString(HeaderPath),
				RefreshFailReason.IsEmpty() ? LOCTEXT("StructHeaderCreatedUnknownRefreshFailure", "Unknown error.") : RefreshFailReason));
		return;
	}

	FMessageDialog::Open(
		EAppMsgType::Ok,
		FText::Format(
			bHeaderOpened
				? LOCTEXT("StructHeaderCreatedMessage", "Created:\n{0}\n\nThe Visual Studio project was refreshed and the header was opened. Compile the project so Unreal Header Tool can detect the new USTRUCT.")
				: LOCTEXT("StructHeaderCreatedOpenFailedMessage", "Created:\n{0}\n\nThe Visual Studio project was refreshed. Open the header from the solution, then compile so Unreal Header Tool can detect the new USTRUCT."),
			FText::FromString(HeaderPath)));
}

void FStructCreatorPluginModule::ReimportAllCsvButtonClicked()
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FARFilter DataTableFilter;
	DataTableFilter.ClassPaths.Add(UDataTable::StaticClass()->GetClassPathName());
	DataTableFilter.PackagePaths.Add(FName(TEXT("/Game")));
	DataTableFilter.bRecursiveClasses = true;
	DataTableFilter.bRecursivePaths = true;

	TArray<FAssetData> DataTableAssets;
	AssetRegistry.GetAssets(DataTableFilter, DataTableAssets);

	int32 CsvDataTableCount = 0;
	int32 ReimportSuccessCount = 0;
	TArray<FString> FailedAssetNames;

	for (const FAssetData& AssetData : DataTableAssets)
	{
		UDataTable* DataTable = Cast<UDataTable>(AssetData.GetAsset());
		if (!HasCsvImportSource(DataTable))
		{
			continue;
		}

		++CsvDataTableCount;
		const bool bReimported = FReimportManager::Instance()->Reimport(DataTable, false, true);
		if (bReimported)
		{
			++ReimportSuccessCount;
		}
		else
		{
			FailedAssetNames.Add(AssetData.PackageName.ToString());
		}
	}

	if (CsvDataTableCount == 0)
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			LOCTEXT("NoCsvDataTablesMessage", "No DataTables with CSV import sources were found in this project."));
		return;
	}

	FText ResultMessage;
	if (FailedAssetNames.IsEmpty())
	{
		ResultMessage = FText::Format(
			LOCTEXT("ReimportCsvSuccessMessage", "Reimported {0} CSV DataTable(s)."),
			FText::AsNumber(ReimportSuccessCount));
	}
	else
	{
		ResultMessage = FText::Format(
			LOCTEXT("ReimportCsvPartialFailureMessage", "Reimported {0} of {1} CSV DataTable(s).\n\nFailed:\n{2}"),
			FText::AsNumber(ReimportSuccessCount),
			FText::AsNumber(CsvDataTableCount),
			FText::FromString(FString::Join(FailedAssetNames, TEXT("\n"))));
	}

	FMessageDialog::Open(EAppMsgType::Ok, ResultMessage);
}

void FStructCreatorPluginModule::RegisterMenus()
{
	UToolMenus::Get()->UnregisterOwner(this);

	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);

	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("MainFrame.MainMenu.Tools");
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("Programming");
			Section.AddMenuEntryWithCommandList(FStructCreatorPluginCommands::Get().PluginAction, PluginCommands);
			Section.AddMenuEntryWithCommandList(FStructCreatorPluginCommands::Get().ReimportAllCsvAction, PluginCommands);
		}
	}

}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FStructCreatorPluginModule, StructCreatorPlugin)
