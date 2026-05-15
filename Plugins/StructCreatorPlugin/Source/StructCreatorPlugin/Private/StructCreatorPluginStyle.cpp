// Copyright Epic Games, Inc. All Rights Reserved.

#include "StructCreatorPluginStyle.h"
#include "StructCreatorPlugin.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/Paths.h"
#include "Styling/SlateStyleRegistry.h"
#include "Slate/SlateGameResources.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleMacros.h"

#define RootToContentDir Style->RootToContentDir

TSharedPtr<FSlateStyleSet> FStructCreatorPluginStyle::StyleInstance = nullptr;

void FStructCreatorPluginStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FStructCreatorPluginStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FStructCreatorPluginStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("StructCreatorPluginStyle"));
	return StyleSetName;
}


const FVector2D Icon16x16(16.0f, 16.0f);
const FVector2D Icon20x20(20.0f, 20.0f);

TSharedRef< FSlateStyleSet > FStructCreatorPluginStyle::Create()
{
	TSharedRef< FSlateStyleSet > Style = MakeShareable(new FSlateStyleSet("StructCreatorPluginStyle"));
	Style->SetContentRoot(FPaths::EngineContentDir() / TEXT("Editor/Slate"));

	Style->Set("StructCreatorPlugin.PluginAction", new IMAGE_BRUSH_SVG(TEXT("Starship/Common/ProjectC++"), Icon20x20));
	return Style;
}

void FStructCreatorPluginStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FStructCreatorPluginStyle::Get()
{
	return *StyleInstance;
}
