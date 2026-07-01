// Copyright Epic Games, Inc. All Rights Reserved.

#include "StructCreatorPluginCommands.h"

#define LOCTEXT_NAMESPACE "FStructCreatorPluginModule"

void FStructCreatorPluginCommands::RegisterCommands()
{
	UI_COMMAND(
		PluginAction,
		"New C++ Struct...",
		"Create a new USTRUCT header in the project's Source folder.",
		EUserInterfaceActionType::Button,
		FInputChord());

	UI_COMMAND(
		ReimportAllCsvAction,
		"Reimport All CSV DataTables",
		"Reimport every DataTable in the project that was imported from a CSV file.",
		EUserInterfaceActionType::Button,
		FInputChord());
}

#undef LOCTEXT_NAMESPACE
