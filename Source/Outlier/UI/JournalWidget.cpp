#include "UI/JournalWidget.h"

#include "Components/TextBlock.h"
#include "Engine/LocalPlayer.h"
#include "Input/ControllerInputConfig.h"
#include "Interaction/JournalInteraction.h"
#include "UI/HintKeyDisplayWidget.h"
#include "UI/LocalPlayerUILayerSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogJournalWidget, Log, All);

void UJournalWidget::InitializeUILayerContext_Implementation(const TArray<AActor*>& ContextActors)
{
	// Layer가 기존 화면을 재사용할 수도 있으므로, 이전 일지 구독부터 해제한다.
	UnbindJournalContext();
	for (AActor* Actor : ContextActors)
	{
		if (AJournalInteraction* Journal = Cast<AJournalInteraction>(Actor))
		{
			JournalContext = Journal;
			JournalEntryChangedHandle = Journal->OnJournalEntryChanged.AddUObject(this, &ThisClass::RefreshContextEntry);
			RefreshContextEntry();
			return;
		}
	}
	SetJournalEntry(FDataTableRowHandle());
}

void UJournalWidget::RefreshContextEntry()
{
	if (const AJournalInteraction* Journal = JournalContext.Get())
	{
		SetJournalEntry(Journal->GetJournalEntry());
	}
}

void UJournalWidget::UnbindJournalContext()
{
	if (AJournalInteraction* Journal = JournalContext.Get())
	{
		Journal->OnJournalEntryChanged.Remove(JournalEntryChangedHandle);
	}
	JournalContext.Reset();
	JournalEntryChangedHandle.Reset();
}

void UJournalWidget::NativeDestruct()
{
	UnbindJournalContext();
	Super::NativeDestruct();
}

void UJournalWidget::SetJournalEntry(const FDataTableRowHandle& InJournalEntry)
{
	JournalEntry = InJournalEntry;
	RefreshJournal();
}

bool UJournalWidget::RefreshJournal()
{
	// 생성 전에도 행을 지정할 수 있다. 바인딩이 준비되면 Construct 경로에서 다시 적용한다.
	if (!Title || !Desc)
	{
		return false;
	}

	const UDataTable* Table = JournalEntry.DataTable;
	const FJournalRow* Row = nullptr;
	const TCHAR* FailureReason = nullptr;
	if (!Table)
	{
		FailureReason = TEXT("DataTable is not set");
	}
	else if (!Table->GetRowStruct() || !Table->GetRowStruct()->IsChildOf(FJournalRow::StaticStruct()))
	{
		// 에디터의 RowType 필터와 별개로, 런타임에 전달된 테이블도 형식을 먼저 검증한다.
		FailureReason = TEXT("DataTable row type must be JournalRow");
	}
	else if (JournalEntry.RowName.IsNone())
	{
		FailureReason = TEXT("RowName is not set");
	}
	else
	{
		Row = Table->FindRow<FJournalRow>(JournalEntry.RowName, TEXT("JournalWidget"), false);
		if (!Row)
		{
			FailureReason = TEXT("Row was not found");
		}
	}

	// 실패한 교체 요청에 이전 일지가 남지 않도록 두 텍스트를 같은 경로에서 갱신한다.
	Title->SetText(Row ? Row->Title : FText::GetEmpty());
	Desc->SetText(Row ? Row->Desc : FText::GetEmpty());
	if (FailureReason && !IsDesignTime())
	{
		UE_LOG(LogJournalWidget, Warning, TEXT("[Journal] Refresh failed: %s. Widget=%s Table=%s Row=%s"),
			FailureReason, *GetName(), *GetNameSafe(Table), *JournalEntry.RowName.ToString());
	}
	return Row != nullptr;
}

void UJournalWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	RefreshJournal();
}

void UJournalWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	RefreshJournal();
}

void UJournalWidget::NativeConstruct()
{
	Super::NativeConstruct();
	RefreshJournal();

	const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	const ULocalPlayerUILayerSubsystem* Layers = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>() : nullptr;
	const UControllerInputConfig* InputConfig = Layers ? Layers->GetWidgetInputConfig() : nullptr;
	if (BackKeyDisplay)
	{
		// 실제 닫기 입력과 같은 액션을 전달한다. 이후 키 변경 통지는 키 표시 부품이 처리한다.
		BackKeyDisplay->SetWatchedInputAction(InputConfig ? InputConfig->WidgetEscapeAction.Get() : nullptr);
	}
}

bool UJournalWidget::HandleUILayerEscape_Implementation()
{
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	ULocalPlayerUILayerSubsystem* Layers = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>() : nullptr;
	return Layers && Layers->PopWidget(this);
}

bool UJournalWidget::HandleUILayerConfirmed_Implementation() { return false; }
bool UJournalWidget::HandleUILayerUp_Implementation() { return false; }
bool UJournalWidget::HandleUILayerDown_Implementation() { return false; }
bool UJournalWidget::HandleUILayerLeft_Implementation() { return false; }
bool UJournalWidget::HandleUILayerRight_Implementation() { return false; }
