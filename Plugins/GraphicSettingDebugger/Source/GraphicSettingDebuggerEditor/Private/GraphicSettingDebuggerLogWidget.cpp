#include "GraphicSettingDebuggerLogWidget.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Notifications/SProgressBar.h"

void SGraphicSettingDebuggerLogWidget::Construct(const FArguments& InArgs)
{
	SAssignNew(DetailsText, STextBlock)
		.Text(FText::FromString(TEXT("Select a snapshot slot.")))
		.AutoWrapText(true);

	SAssignNew(GpuBar, SProgressBar).Percent(0.0f);
	SAssignNew(FrameBar, SProgressBar).Percent(0.0f);
	SAssignNew(FpsBar, SProgressBar).Percent(0.0f);

	SAssignNew(SlotList, SListView<TSharedPtr<FString>>)
		.ListItemsSource(&Slots)
		.OnGenerateRow(this, &SGraphicSettingDebuggerLogWidget::MakeSlotRow)
		.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
		{
			if (Item.IsValid())
			{
				LoadSlot(*Item);
			}
		});

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Graphic Setting Debugger - Snapshot Slots")))
				.Font(FCoreStyle::Get().GetFontStyle("Heading2"))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton).Text(FText::FromString(TEXT("Refresh"))).OnClicked_Lambda([this]() { RefreshSlots(); return FReply::Handled(); })
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.35f).Padding(8.0f)
			[
				SNew(SBorder).Padding(4.0f)[SlotList.ToSharedRef()]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 8.0f)
			[
				SNew(SSeparator)
			]
			+ SHorizontalBox::Slot().FillWidth(0.65f).Padding(8.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)
					[
						DetailsText.ToSharedRef()
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 16.0f, 4.0f, 2.0f)
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("GPU frame time")))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)
					[
						GpuBar.ToSharedRef()
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 8.0f, 4.0f, 2.0f)
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("Frame time")))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)
					[
						FrameBar.ToSharedRef()
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 8.0f, 4.0f, 2.0f)
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("FPS")))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)
					[
						FpsBar.ToSharedRef()
					]
				]
			]
		]
	];

	RefreshSlots();
}

void SGraphicSettingDebuggerLogWidget::RefreshSlots()
{
	Slots.Empty();
	const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("GraphicSettingDebugger"));
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(Directory, TEXT("*.json")), true, false);
	Files.Sort();
	for (const FString& File : Files)
	{
		Slots.Add(MakeShared<FString>(File));
	}
	if (SlotList.IsValid())
	{
		SlotList->RequestListRefresh();
	}
}

void SGraphicSettingDebuggerLogWidget::LoadSlot(const FString& SlotName)
{
	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("GraphicSettingDebugger"), SlotName);
	FString Json;
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FFileHelper::LoadFileToString(Json, *Path) || !FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		DetailsText->SetText(FText::FromString(TEXT("Could not load snapshot.")));
		return;
	}

	double Fps = 0.0;
	double GpuFrameMs = 0.0;
	double FrameMs = 0.0;
	Root->TryGetNumberField(TEXT("fps"), Fps);
	Root->TryGetNumberField(TEXT("gpuFrameMs"), GpuFrameMs);
	Root->TryGetNumberField(TEXT("frameMs"), FrameMs);
	const TSharedPtr<FJsonObject>* Settings = nullptr;
	int32 SettingCount = Root->TryGetObjectField(TEXT("settings"), Settings) && Settings ? (*Settings)->Values.Num() : 0;

	DetailsText->SetText(FText::FromString(FString::Printf(TEXT("Slot: %s\nTimestamp: %s\nSettings: %d\nGPU: %.2f ms\nFrame: %.2f ms\nFPS: %.1f"),
		*SlotName, *Root->GetStringField(TEXT("timestampUtc")), SettingCount, GpuFrameMs, FrameMs, Fps)));
	GpuBar->SetPercent(FMath::Clamp(static_cast<float>(GpuFrameMs / 33.33), 0.0f, 1.0f));
	FrameBar->SetPercent(FMath::Clamp(static_cast<float>(FrameMs / 33.33), 0.0f, 1.0f));
	FpsBar->SetPercent(FMath::Clamp(static_cast<float>(Fps / 120.0), 0.0f, 1.0f));
}

TSharedRef<ITableRow> SGraphicSettingDebuggerLogWidget::MakeSlotRow(TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
	[
		SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : TEXT("Invalid slot")))
	];
}
