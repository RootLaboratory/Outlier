#if WITH_DEV_AUTOMATION_TESTS

#include "UI/JournalWidget.h"

#include "Components/TextBlock.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

namespace JournalWidgetTests
{
	struct FJournalView
	{
		UJournalWidget* Widget = NewObject<UJournalWidget>();
		UTextBlock* Title = NewObject<UTextBlock>(Widget);
		UTextBlock* Desc = NewObject<UTextBlock>(Widget);

		FJournalView()
		{
			FindFProperty<FObjectPropertyBase>(Widget->GetClass(), TEXT("Title"))
				->SetObjectPropertyValue_InContainer(Widget, Title);
			FindFProperty<FObjectPropertyBase>(Widget->GetClass(), TEXT("Desc"))
				->SetObjectPropertyValue_InContainer(Widget, Desc);
		}
	};

	FDataTableRowHandle Entry(UDataTable* Table, FName Name)
	{
		FDataTableRowHandle Handle;
		Handle.DataTable = Table;
		Handle.RowName = Name;
		return Handle;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJournalRowPresentationTest, "Outlier.UI.Journal.RowPresentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FJournalRowPresentationTest::RunTest(const FString& Parameters)
{
	using namespace JournalWidgetTests;
	FJournalView View;
	UDataTable* Table = NewObject<UDataTable>();
	Table->RowStruct = FJournalRow::StaticStruct();
	FJournalRow First;
	First.Title = FText::FromString(TEXT("관찰 기록 142"));
	First.Desc = FText::FromString(TEXT("첫째 줄, \"질문\"\n둘째 줄"));
	Table->AddRow(TEXT("First"), First);
	View.Widget->SetJournalEntry(Entry(Table, TEXT("First")));
	TestEqual(TEXT("Row title is displayed"), View.Title->GetText().ToString(), First.Title.ToString());
	TestEqual(TEXT("Punctuation and line breaks are preserved"), View.Desc->GetText().ToString(), First.Desc.ToString());

	FJournalRow Second;
	Second.Title = FText::FromString(TEXT("다음 일지"));
	Table->AddRow(TEXT("Second"), Second);
	View.Widget->SetJournalEntry(Entry(Table, TEXT("Second")));
	TestEqual(TEXT("Switching rows replaces title"), View.Title->GetText().ToString(), Second.Title.ToString());
	TestTrue(TEXT("An intentionally empty body clears the previous body"), View.Desc->GetText().IsEmpty());

	Second.Desc = FText::FromString(TEXT("재수입 이후 본문"));
	Table->AddRow(TEXT("Second"), Second);
	TestTrue(TEXT("Explicit refresh succeeds"), View.Widget->RefreshJournal());
	TestEqual(TEXT("Refresh reads changed table contents"), View.Desc->GetText().ToString(), Second.Desc.ToString());
	Second.Title = FText::FromString(TEXT("미리보기 갱신"));
	Table->AddRow(TEXT("Second"), Second);
	View.Widget->SynchronizeProperties();
	TestEqual(TEXT("Property synchronization refreshes displayed data"), View.Title->GetText().ToString(), Second.Title.ToString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJournalInvalidEntryTest, "Outlier.UI.Journal.InvalidEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FJournalInvalidEntryTest::RunTest(const FString& Parameters)
{
	using namespace JournalWidgetTests;
	FJournalView View;
	UDataTable* Table = NewObject<UDataTable>();
	Table->RowStruct = FJournalRow::StaticStruct();
	UDataTable* WrongTable = NewObject<UDataTable>();
	WrongTable->RowStruct = FTableRowBase::StaticStruct();
	const FDataTableRowHandle InvalidEntries[] =
	{
		Entry(nullptr, TEXT("MissingTable")), Entry(Table, NAME_None),
		Entry(Table, TEXT("MissingRow")), Entry(WrongTable, TEXT("WrongType"))
	};
	AddExpectedErrorPlain(TEXT("[Journal] Refresh failed:"), EAutomationExpectedErrorFlags::Contains, UE_ARRAY_COUNT(InvalidEntries));
	for (const FDataTableRowHandle& Invalid : InvalidEntries)
	{
		View.Title->SetText(FText::FromString(TEXT("Previous title")));
		View.Desc->SetText(FText::FromString(TEXT("Previous body")));
		View.Widget->SetJournalEntry(Invalid);
		TestTrue(TEXT("Invalid entry clears title"), View.Title->GetText().IsEmpty());
		TestTrue(TEXT("Invalid entry clears body"), View.Desc->GetText().IsEmpty());
	}

	UJournalWidget* Unbound = NewObject<UJournalWidget>();
	Unbound->SetJournalEntry(Entry(Table, TEXT("Pending")));
	TestFalse(TEXT("Refresh before bindings are ready safely returns false"), Unbound->RefreshJournal());
	TestFalse(TEXT("Escape without an owning layer safely returns false"),
		IUILayerInputReceiver::Execute_HandleUILayerEscape(Unbound));
	return true;
}

#if WITH_EDITOR

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJournalCSVImportTest, "Outlier.UI.Journal.CSVImport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FJournalCSVImportTest::RunTest(const FString& Parameters)
{
	using namespace JournalWidgetTests;
	UDataTable* Table = NewObject<UDataTable>();
	Table->RowStruct = FJournalRow::StaticStruct();
	const FString CSV = TEXT("Name,Title,Desc\nJournal_142,\"관찰 기록 142\",\"첫째 줄, \"\"질문\"\"\n둘째 줄\"\n");
	const TArray<FString> ImportErrors = Table->CreateTableFromCSVString(CSV);
	for (const FString& Error : ImportErrors)
	{
		AddError(Error);
	}
	if (!ImportErrors.IsEmpty())
	{
		return false;
	}

	FJournalView View;
	View.Widget->SetJournalEntry(Entry(Table, TEXT("Journal_142")));
	TestEqual(TEXT("CSV title is displayed"), View.Title->GetText().ToString(), FString(TEXT("관찰 기록 142")));
	TestEqual(TEXT("CSV quoted punctuation and multiline body are preserved"), View.Desc->GetText().ToString(),
		FString(TEXT("첫째 줄, \"질문\"\n둘째 줄")));
	return true;
}

#endif

#endif
