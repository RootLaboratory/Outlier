#include "Debug/RDGDebugWindowManager.h"

#include "Debug/SRDGGraphicsDebugger.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SScrollBox.h"
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
		.ClientSize(FVector2D(460.0f, 560.0f))
		.SupportsMinimize(true)
		.SupportsMaximize(false)
		.FocusWhenFirstShown(true)
		[
			// 섹션이 창 높이보다 길어져서 아래쪽(사망 연출 등)이 잘리지 않게 스크롤로 감싼다.
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SNew(SRDGGraphicsDebugger)
			]
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
