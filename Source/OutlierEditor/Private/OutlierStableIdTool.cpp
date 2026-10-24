#include "OutlierStableIdTool.h"

#include "OutlierStableIdValidator.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "OutlierStableIdTool"

void FOutlierStableIdTool::Open()
{
	ShowReport(false);
}

void FOutlierStableIdTool::CheckBeforePIE(bool bIsSimulating)
{
	ShowReport(true);
}

void FOutlierStableIdTool::Shutdown()
{
	if (TSharedPtr<SWindow> Existing = Window.Pin())
	{
		// Destroy immediately before this module's Slate lambdas are unloaded.
		FSlateApplication::Get().DestroyWindowImmediately(Existing.ToSharedRef());
	}
	Window.Reset();
}

void FOutlierStableIdTool::ShowReport(bool bOnlyIfInvalid)
{
	if (!GEditor || !FSlateApplication::IsInitialized())
	{
		return;
	}
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return;
	}
	const OutlierEditor::FStableIdReport Report = OutlierEditor::ScanStableIds(World);
	if (bOnlyIfInvalid && Report.Issues.IsEmpty())
	{
		return;
	}
	TSharedPtr<SWindow> ReportWindow = Window.Pin();
	const bool bNewWindow = !ReportWindow.IsValid();
	if (bNewWindow)
	{
		ReportWindow = SNew(SWindow)
			.Title(LOCTEXT("Title", "Outlier Checkpoint ID Check"))
			.ClientSize(FVector2D(1120, 620))
			.SupportsMaximize(true).SupportsMinimize(false);
		Window = ReportWindow;
	}
	TSharedRef<SScrollBox> Rows = SNew(SScrollBox);
	for (const OutlierEditor::FStableIdIssue& Issue : Report.Issues)
	{
		const OutlierEditor::FStableIdEntry& Entry = Issue.Entry;
		const FString Description = FString::Printf(TEXT("[%s]  %s\nLevel: %s\n%s = %s"),
			Issue.bDuplicate ? TEXT("Duplicate") : TEXT("Missing"),
			*Entry.ActorName, *Entry.LevelName, *Entry.Property, *Entry.Id.ToString());
		Rows->AddSlot().Padding(0, 3)
		[
			SNew(SButton)
			.ToolTipText(LOCTEXT("SelectActor", "Select this actor in the editor and focus the viewport."))
			.OnClicked_Lambda([Actor = Entry.Actor]()
			{
				if (GEditor && Actor.IsValid())
				{
					GEditor->SelectNone(false, true);
					GEditor->SelectActor(Actor.Get(), true, true, true);
					GEditor->MoveViewportCamerasToActor(*Actor.Get(), false);
				}
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Text(FText::FromString(Description)).AutoWrapText(true)
			]
		];
	}
	ReportWindow->SetContent(
		SNew(SBox).Padding(12)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(STextBlock).AutoWrapText(true)
				.Text(FText::Format(LOCTEXT("Summary", "Map: {0}\nScanned {1} actors / {2} persistent IDs. Issues: {3}"),
					FText::FromString(World->GetOutermost()->GetName()), FText::AsNumber(Report.ActorCount),
					FText::AsNumber(Report.IdCount), FText::AsNumber(Report.Issues.Num())))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(STextBlock).AutoWrapText(true)
				.Text(LOCTEXT("Scope", "Loaded editor levels only, including loaded streaming / World Partition actors. Load the target regions and sublevels to check them.\nIDs share one namespace across actor types and levels. Empty optional hack IDs and nonpersistent explosives are excluded.\nClick a result to select its actor. This check does not modify IDs or block Play."))
			]
			+ SVerticalBox::Slot().FillHeight(1)
			[
				Rows
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
			[
				SNew(SButton).HAlign(HAlign_Center)
				.Text(LOCTEXT("Rescan", "Rescan loaded levels"))
				.OnClicked_Lambda([this]() { Open(); return FReply::Handled(); })
			]
		]);
	if (bNewWindow)
	{
		FSlateApplication::Get().AddWindow(ReportWindow.ToSharedRef());
	}
	else
	{
		ReportWindow->BringToFront();
	}
}

#undef LOCTEXT_NAMESPACE
