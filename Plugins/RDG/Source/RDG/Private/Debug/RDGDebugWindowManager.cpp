#include "Debug/RDGDebugWindowManager.h"

#include "Debug/SRDGGraphicsDebugger.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"

void FRDGDebugWindowManager::OpenWindow()
{
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}

	if (DebugWindow.IsValid())
	{
		return;
	}

	TSharedRef<SWindow> NewWindow =
		SNew(SWindow)
		.Title(FText::FromString(TEXT("RDG Graphics Debugger")))
		.ClientSize(FVector2D(420.0f, 220.0f))
		.SupportsMinimize(true)
		.SupportsMaximize(false)
		.FocusWhenFirstShown(true)
		[
			SNew(SRDGGraphicsDebugger)
		];

	DebugWindow = NewWindow;
	FSlateApplication::Get().AddWindow(NewWindow);
}

void FRDGDebugWindowManager::CloseWindow()
{
	if (!FSlateApplication::IsInitialized())
	{
		DebugWindow.Reset();
		return;
	}

	if (TSharedPtr<SWindow> ExistingWindow = DebugWindow.Pin())
	{
		FSlateApplication::Get().RequestDestroyWindow(ExistingWindow.ToSharedRef());
	}

	DebugWindow.Reset();
}
