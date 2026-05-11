// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"
#include "StructCreatorPluginStyle.h"

class FStructCreatorPluginCommands : public TCommands<FStructCreatorPluginCommands>
{
public:

	FStructCreatorPluginCommands()
		: TCommands<FStructCreatorPluginCommands>(TEXT("StructCreatorPlugin"), NSLOCTEXT("Contexts", "StructCreatorPlugin", "StructCreatorPlugin Plugin"), NAME_None, FStructCreatorPluginStyle::GetStyleSetName())
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr< FUICommandInfo > PluginAction;
};
