#include "OutlierEventToolWidget.h"

#include "Editor.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Event/OutlierEventDataAsset.h"
#include "Event/OutlierEventSettings.h"
#include "Event/OutlierEventTypes.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "OutlierPlayerState.h"
#include "Tutorial/TutorialComponent.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SOutlierEventToolWidget"

namespace
{
	const FName EventColumnId(TEXT("Event"));
	const FName ScriptColumnId(TEXT("Script"));
	const FName UpperColumnId(TEXT("Upper"));
	const FName GifColumnId(TEXT("Gif"));
	const FName WarningColumnId(TEXT("Warning"));

	// 런타임 EventSubsystem과 같은 규칙: RowName == 태그, 행 구조체 일치.
	template <typename RowType>
	const RowType* FindToolRow(const UDataTable* Table, FGameplayTag Tag)
	{
		return Table && Tag.IsValid() && Table->GetRowStruct() == RowType::StaticStruct()
			? Table->FindRow<RowType>(Tag.GetTagName(), TEXT("OutlierEventTool"), false)
			: nullptr;
	}

	FString FirstLine(const FText& Text)
	{
		FString Line = Text.ToString();
		int32 BreakIndex = INDEX_NONE;
		if (Line.FindChar(TEXT('\n'), BreakIndex))
		{
			Line.LeftInline(BreakIndex);
		}
		return Line.TrimEnd();
	}

	FText RoleText(EOutlierPlayerRole Role)
	{
		switch (Role)
		{
		case EOutlierPlayerRole::Shooter: return LOCTEXT("RoleShooter", "Shooter");
		case EOutlierPlayerRole::Partner: return LOCTEXT("RolePartner", "Partner");
		default: return LOCTEXT("RoleNone", "No Role");
		}
	}

	FText NetModeText(ENetMode NetMode)
	{
		switch (NetMode)
		{
		case NM_Standalone: return LOCTEXT("NetStandalone", "Standalone");
		case NM_ListenServer: return LOCTEXT("NetListen", "Listen Server");
		case NM_Client: return LOCTEXT("NetClient", "Client");
		default: return LOCTEXT("NetOther", "Server");
		}
	}

	// 로컬 플레이어가 있는 PIE 월드만 대상이다(데디 서버 월드 제외).
	void CollectPIEWorlds(TArray<UWorld*>& OutWorlds)
	{
		if (!GEngine)
		{
			return;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (Context.WorldType == EWorldType::PIE && World && GEngine->GetFirstLocalPlayerController(World))
			{
				OutWorlds.Add(World);
			}
		}
	}

	class SOutlierEventToolTableRow : public SMultiColumnTableRow<TSharedPtr<FOutlierEventToolRow>>
	{
	public:
		SLATE_BEGIN_ARGS(SOutlierEventToolTableRow) {}
			SLATE_ARGUMENT(TSharedPtr<FOutlierEventToolRow>, RowItem)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTableView)
		{
			RowItem = InArgs._RowItem;
			SMultiColumnTableRow<TSharedPtr<FOutlierEventToolRow>>::Construct(
				FSuperRowType::FArguments().Padding(2.0f),
				OwnerTableView);
		}

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
		{
			if (!RowItem.IsValid())
			{
				return SNew(STextBlock);
			}
			if (ColumnName == WarningColumnId)
			{
				return SNew(STextBlock)
					.Text(FText::FromString(RowItem->Warning))
					.ToolTipText(FText::FromString(RowItem->Warning))
					.ColorAndOpacity(FLinearColor(1.0f, 0.55f, 0.2f));
			}
			FString Value;
			if (ColumnName == EventColumnId) Value = RowItem->EventTag.ToString();
			else if (ColumnName == ScriptColumnId) Value = RowItem->Script;
			else if (ColumnName == UpperColumnId) Value = RowItem->Upper;
			else if (ColumnName == GifColumnId) Value = RowItem->Gif;
			return SNew(STextBlock)
				.Text(FText::FromString(Value))
				.ToolTipText(FText::FromString(Value));
		}

	private:
		TSharedPtr<FOutlierEventToolRow> RowItem;
	};
}

void SOutlierEventToolWidget::Construct(const FArguments& InArgs)
{
	Role = EOutlierPlayerRole::Shooter;
	FEditorDelegates::PostPIEStarted.AddSP(this, &SOutlierEventToolWidget::HandlePIEChanged);
	FEditorDelegates::ShutdownPIE.AddSP(this, &SOutlierEventToolWidget::HandlePIEChanged);

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("TargetLabel", "Target"))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SAssignNew(TargetComboBox, SComboBox<FTargetPtr>)
				.OptionsSource(&Targets)
				.OnComboBoxOpening(this, &SOutlierEventToolWidget::RefreshTargets)
				.OnGenerateWidget(this, &SOutlierEventToolWidget::GenerateTargetItem)
				.OnSelectionChanged(this, &SOutlierEventToolWidget::OnTargetSelected)
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						return SelectedTarget.IsValid() ? SelectedTarget->Label : FText::GetEmpty();
					})
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SSegmentedControl<EOutlierPlayerRole>)
				.ToolTipText(LOCTEXT("RoleTooltip", "Which role's Event DT to list. Picking a single PIE player switches to that player's role."))
				.Value_Lambda([this]() { return Role; })
				.OnValueChanged_Lambda([this](EOutlierPlayerRole NewRole) { SetRole(NewRole); })
				+ SSegmentedControl<EOutlierPlayerRole>::Slot(EOutlierPlayerRole::Shooter)
				.Text(RoleText(EOutlierPlayerRole::Shooter))
				+ SSegmentedControl<EOutlierPlayerRole>::Slot(EOutlierPlayerRole::Partner)
				.Text(RoleText(EOutlierPlayerRole::Partner))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Reload", "Reload"))
				.ToolTipText(LOCTEXT("ReloadTooltip", "Re-read the DataTables and the running PIE players."))
				.OnClicked_Lambda([this]()
				{
					RefreshTargets();
					ReloadRows();
					return FReply::Handled();
				})
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f, 0.0f, 4.0f, 4.0f)
		[
			SNew(SSearchBox)
			.HintText(LOCTEXT("SearchHint", "Search event tag, script, upper or GIF"))
			.OnTextChanged_Lambda([this](const FText& Text)
			{
				SearchText = Text.ToString();
				RefreshFilteredRows();
			})
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(4.0f, 0.0f)
		[
			SAssignNew(RowListView, SListView<FRowPtr>)
			.ListItemsSource(&FilteredRows)
			.SelectionMode(ESelectionMode::Single)
			.OnGenerateRow(this, &SOutlierEventToolWidget::GenerateRow)
			.OnMouseButtonDoubleClick_Lambda([this](FRowPtr Row)
			{
				if (Row.IsValid() && RowListView.IsValid())
				{
					RowListView->SetSelection(Row);
					SendSelectedEvent();
				}
			})
			.HeaderRow
			(
				SNew(SHeaderRow)
				+ SHeaderRow::Column(EventColumnId)
				.DefaultLabel(LOCTEXT("EventColumn", "Event"))
				.FillWidth(0.26f)
				+ SHeaderRow::Column(ScriptColumnId)
				.DefaultLabel(LOCTEXT("ScriptColumn", "TTS Script"))
				.FillWidth(0.34f)
				+ SHeaderRow::Column(UpperColumnId)
				.DefaultLabel(LOCTEXT("UpperColumn", "Upper"))
				.FillWidth(0.18f)
				+ SHeaderRow::Column(GifColumnId)
				.DefaultLabel(LOCTEXT("GifColumn", "GIF"))
				.FillWidth(0.1f)
				+ SHeaderRow::Column(WarningColumnId)
				.DefaultLabel(LOCTEXT("WarningColumn", "Warning"))
				.FillWidth(0.12f)
			)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text_Lambda([this]() { return StatusText; })
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Send", "Send"))
				.ToolTipText(LOCTEXT("SendTooltip", "Send the selected event to the target PIE players. Double-clicking a row also sends."))
				.IsEnabled_Lambda([this]() { return CanSend(); })
				.OnClicked_Lambda([this]()
				{
					SendSelectedEvent();
					return FReply::Handled();
				})
			]
		]
	];

	RefreshTargets();
	ReloadRows();
}

SOutlierEventToolWidget::~SOutlierEventToolWidget()
{
	FEditorDelegates::PostPIEStarted.RemoveAll(this);
	FEditorDelegates::ShutdownPIE.RemoveAll(this);
}

void SOutlierEventToolWidget::ReloadRows()
{
	Rows.Reset();
	const UOutlierEventSettings* Settings = GetDefault<UOutlierEventSettings>();
	const UOutlierEventDataAsset* DataAsset = Role == EOutlierPlayerRole::Partner
		? Settings->PartnerDataAsset.LoadSynchronous()
		: Settings->ShooterDataAsset.LoadSynchronous();
	const UDataTable* EventTable = DataAsset ? DataAsset->EventTable.LoadSynchronous() : nullptr;
	if (!EventTable || EventTable->GetRowStruct() != FOutlierEventDataRow::StaticStruct())
	{
		StatusText = FText::Format(LOCTEXT("NoEventTable",
			"{0}: no Event DT. Check Project Settings > Outlier Events and the role Data Asset."), RoleText(Role));
		RefreshFilteredRows();
		return;
	}
	const UDataTable* TTSTable = DataAsset->TTSTable.LoadSynchronous();
	const UDataTable* InputTable = DataAsset->InputTable.LoadSynchronous();
	const UDataTable* GoalTable = DataAsset->GoalTable.LoadSynchronous();
	const UDataTable* GifTable = DataAsset->GifTable.LoadSynchronous();

	EventTable->ForeachRow<FOutlierEventDataRow>(TEXT("OutlierEventTool"),
		[&](const FName& Key, const FOutlierEventDataRow& Event)
		{
			FRowPtr Row = MakeShared<FOutlierEventToolRow>();
			Row->EventTag = Event.EventTag;
			TArray<FString> Warnings;
			if (!Event.EventTag.IsValid() || Event.EventTag.GetTagName() != Key)
			{
				Warnings.Add(TEXT("RowName != EventTag"));
			}
			if (Event.TTSTag.IsValid())
			{
				const FOutlierTTSDataRow* TTS = FindToolRow<FOutlierTTSDataRow>(TTSTable, Event.TTSTag);
				Row->Script = TTS ? FirstLine(TTS->Script) : Event.TTSTag.ToString();
				if (!TTS) Warnings.Add(TEXT("No TTS row"));
			}
			// 런타임과 같이 Input/Goal 동시 지정은 Upper를 띄우지 않는다.
			if (Event.InputTag.IsValid() && Event.GoalTag.IsValid())
			{
				Warnings.Add(TEXT("Input+Goal"));
			}
			else if (Event.InputTag.IsValid())
			{
				const FOutlierInputDataRow* Input = FindToolRow<FOutlierInputDataRow>(InputTable, Event.InputTag);
				Row->Upper = TEXT("Input: ") + (Input ? FirstLine(Input->Text) : Event.InputTag.ToString());
				if (!Input) Warnings.Add(TEXT("No Input row"));
				else if (!IsValid(Input->InputAction)) Warnings.Add(TEXT("No IA"));
			}
			else if (Event.GoalTag.IsValid())
			{
				const FOutlierGoalDataRow* Goal = FindToolRow<FOutlierGoalDataRow>(GoalTable, Event.GoalTag);
				Row->Upper = TEXT("Goal: ") + (Goal ? FirstLine(Goal->Text) : Event.GoalTag.ToString());
				if (!Goal) Warnings.Add(TEXT("No Goal row"));
			}
			if (Event.GifTag.IsValid())
			{
				const FOutlierGifDataRow* Gif = FindToolRow<FOutlierGifDataRow>(GifTable, Event.GifTag);
				Row->Gif = Gif ? Gif->Subject.ToString() : Event.GifTag.ToString();
				if (!Gif) Warnings.Add(TEXT("No GIF row"));
			}
			Row->Warning = FString::Join(Warnings, TEXT(", "));
			Rows.Add(Row);
		});

	StatusText = FText::Format(LOCTEXT("Loaded", "{0}: {1} events from {2}."),
		RoleText(Role), FText::AsNumber(Rows.Num()), FText::FromString(EventTable->GetName()));
	RefreshFilteredRows();
}

void SOutlierEventToolWidget::RefreshFilteredRows()
{
	FilteredRows.Reset();
	for (const FRowPtr& Row : Rows)
	{
		if (SearchText.IsEmpty()
			|| Row->EventTag.ToString().Contains(SearchText)
			|| Row->Script.Contains(SearchText)
			|| Row->Upper.Contains(SearchText)
			|| Row->Gif.Contains(SearchText))
		{
			FilteredRows.Add(Row);
		}
	}
	if (RowListView.IsValid())
	{
		RowListView->RequestListRefresh();
	}
}

void SOutlierEventToolWidget::SetRole(EOutlierPlayerRole NewRole)
{
	if (Role == NewRole)
	{
		return;
	}
	Role = NewRole;
	ReloadRows();
}

void SOutlierEventToolWidget::RefreshTargets()
{
	if (!AllTarget.IsValid())
	{
		AllTarget = MakeShared<FOutlierEventToolTarget>();
	}
	TArray<UWorld*> Worlds;
	CollectPIEWorlds(Worlds);
	AllTarget->Label = Worlds.IsEmpty()
		? LOCTEXT("AllTargetIdle", "All PIE players (PIE not running)")
		: FText::Format(LOCTEXT("AllTarget", "All PIE players ({0})"), FText::AsNumber(Worlds.Num()));

	// 기존 항목을 재사용해 콤보가 열린 채로 갱신돼도 선택이 바뀌지 않게 한다.
	TArray<FTargetPtr> NewTargets;
	NewTargets.Add(AllTarget);
	for (UWorld* World : Worlds)
	{
		const FTargetPtr* Existing = Targets.FindByPredicate([World](const FTargetPtr& Target)
		{
			return Target.IsValid() && Target->World.Get() == World;
		});
		FTargetPtr Target = Existing ? *Existing : MakeShared<FOutlierEventToolTarget>();
		const APlayerController* Controller = GEngine->GetFirstLocalPlayerController(World);
		const AOutlierPlayerState* PlayerState = Controller ? Controller->GetPlayerState<AOutlierPlayerState>() : nullptr;
		const FWorldContext* Context = GEngine->GetWorldContextFromWorld(World);
		Target->World = World;
		Target->Role = PlayerState ? PlayerState->GetPlayerRole() : EOutlierPlayerRole::None;
		Target->Label = FText::Format(LOCTEXT("WorldTarget", "PIE {0} | {1} | {2}"),
			FText::AsNumber(Context ? Context->PIEInstance : 0), NetModeText(World->GetNetMode()), RoleText(Target->Role));
		NewTargets.Add(Target);
	}
	Targets = MoveTemp(NewTargets);

	const bool bSelectionGone = !SelectedTarget.IsValid() || !Targets.Contains(SelectedTarget);
	if (TargetComboBox.IsValid())
	{
		TargetComboBox->RefreshOptions();
		if (bSelectionGone)
		{
			TargetComboBox->SetSelectedItem(AllTarget);
		}
	}
	else if (bSelectionGone)
	{
		SelectedTarget = AllTarget;
	}
}

TArray<UWorld*> SOutlierEventToolWidget::GetTargetWorlds() const
{
	TArray<UWorld*> Worlds;
	CollectPIEWorlds(Worlds);
	if (SelectedTarget.IsValid() && SelectedTarget != AllTarget)
	{
		UWorld* SelectedWorld = SelectedTarget->World.Get();
		Worlds.RemoveAll([SelectedWorld](const UWorld* World) { return World != SelectedWorld; });
	}
	return Worlds;
}

void SOutlierEventToolWidget::HandlePIEChanged(bool bIsSimulating)
{
	RefreshTargets();
}

TSharedRef<SWidget> SOutlierEventToolWidget::GenerateTargetItem(FTargetPtr Target) const
{
	return SNew(STextBlock)
		.Text_Lambda([Target]() { return Target.IsValid() ? Target->Label : FText::GetEmpty(); });
}

void SOutlierEventToolWidget::OnTargetSelected(FTargetPtr Target, ESelectInfo::Type SelectInfo)
{
	if (!Target.IsValid())
	{
		return;
	}
	SelectedTarget = Target;
	// 한 플레이어를 고르면 그 플레이어 역할의 DT로 목록을 맞춘다.
	if (Target != AllTarget && Target->Role != EOutlierPlayerRole::None)
	{
		SetRole(Target->Role);
	}
}

TSharedRef<ITableRow> SOutlierEventToolWidget::GenerateRow(FRowPtr Row, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SOutlierEventToolTableRow, OwnerTable)
		.RowItem(Row);
}

bool SOutlierEventToolWidget::CanSend() const
{
	return RowListView.IsValid() && RowListView->GetNumItemsSelected() > 0 && !GetTargetWorlds().IsEmpty();
}

void SOutlierEventToolWidget::SendSelectedEvent()
{
	const TArray<FRowPtr> Selected = RowListView.IsValid() ? RowListView->GetSelectedItems() : TArray<FRowPtr>();
	if (Selected.IsEmpty() || !Selected[0].IsValid() || !Selected[0]->EventTag.IsValid())
	{
		StatusText = LOCTEXT("NoRowSelected", "Select an event with a valid tag.");
		return;
	}
	const FGameplayTag EventTag = Selected[0]->EventTag;
	const TArray<UWorld*> Worlds = GetTargetWorlds();
	if (Worlds.IsEmpty())
	{
		StatusText = LOCTEXT("NoTargets", "No PIE player to send to. Start PIE first.");
		return;
	}
	int32 SentCount = 0;
	for (UWorld* World : Worlds)
	{
		APlayerController* Controller = GEngine->GetFirstLocalPlayerController(World);
		// 그 PIE 창의 콘솔처럼 해당 월드를 GWorld/PIE ID로 잡은 채 보낸다.
		UWorld* EditorWorld = GIsPlayInEditorWorld ? nullptr : SetPlayInEditorWorld(World);
		if (UTutorialComponent::SendTriggerEvent(Controller ? Controller->GetPawn() : nullptr, EventTag, TEXT("EventTool")))
		{
			++SentCount;
		}
		if (EditorWorld)
		{
			RestoreEditorWorld(EditorWorld);
		}
	}
	// 각 플레이어는 자기 역할의 DT로 태그를 해석한다. 실패 사유는 Output Log에 남는다.
	StatusText = FText::Format(LOCTEXT("SentStatus", "{0}: sent to {1}/{2} PIE players. See Output Log [Event] / [EventTrigger]."),
		FText::FromString(EventTag.ToString()), FText::AsNumber(SentCount), FText::AsNumber(Worlds.Num()));
}

#undef LOCTEXT_NAMESPACE
