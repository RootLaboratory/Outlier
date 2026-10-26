#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Interaction/JournalInteraction.h"

#include "Components/StaticMeshComponent.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Interaction/InteractableComponent.h"
#include "Misc/AutomationTest.h"
#include "UI/JournalWidget.h"
#include "UObject/UnrealType.h"

namespace JournalInteractionTests
{
	struct FScopedWorld
	{
		UWorld* World = nullptr;
		FScopedWorld()
		{
			if (GEngine)
			{
				World = UWorld::CreateWorld(EWorldType::Game, false,
					MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("JournalInteractionTest")));
				if (World)
				{
					GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
					World->InitializeActorsForPlay(FURL());
				}
			}
		}
		~FScopedWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
				GEngine->DestroyWorldContext(World);
				World->RemoveFromRoot();
			}
		}
	};

	void SetEntry(AJournalInteraction* Journal, UDataTable* Table, FName RowName)
	{
		FDataTableRowHandle* Entry = FindFProperty<FStructProperty>(Journal->GetClass(), TEXT("JournalEntry"))
			->ContainerPtrToValuePtr<FDataTableRowHandle>(Journal);
		Entry->DataTable = Table;
		Entry->RowName = RowName;
	}

	FText Preview(AJournalInteraction* Journal, const TCHAR* Name)
	{
		return *FindFProperty<FTextProperty>(Journal->GetClass(), Name)->ContainerPtrToValuePtr<FText>(Journal);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJournalInteractionPreviewTest, "Outlier.UI.Journal.InteractionPreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FJournalInteractionPreviewTest::RunTest(const FString& Parameters)
{
	using namespace JournalInteractionTests;
	FScopedWorld Scope;
	if (!TestNotNull(TEXT("Test world exists"), Scope.World)) { return false; }
	AJournalInteraction* Journal = Scope.World->SpawnActor<AJournalInteraction>();
	if (!TestNotNull(TEXT("Journal is spawned"), Journal)) { return false; }
	TestFalse(TEXT("Journal does not tick"), Journal->PrimaryActorTick.bCanEverTick);
	TestTrue(TEXT("Journal context can replicate"), Journal->GetIsReplicated());
	TestTrue(TEXT("Journal remains repeatable"), Journal->GetInteractableComponent()->IsMultipleUse());
	TestFalse(TEXT("Journal opens without a hold"), Journal->GetInteractableComponent()->RequiresHoldInteract());
	FGameplayTagContainer ShooterTags;
	ShooterTags.AddTag(OutlierGameplayTags::Actor::Role::Shooter());
	TestTrue(TEXT("Shooter can use the interaction component"), Journal->GetInteractableComponent()->CanInteract(ShooterTags));
	// Shooter 전용 제한이 없는지 확인한다. 실제 캐릭터 종류 검사는 Interact 진입점에서 처리한다.
	TestTrue(TEXT("Interaction does not require Shooter role"), Journal->GetInteractableComponent()->CanInteract(FGameplayTagContainer()));
	TestFalse(TEXT("Missing interactor is rejected"), Journal->Interact(nullptr));
	UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Journal->GetDefaultSubobjectByName(TEXT("JournalMesh")));
	if (!TestNotNull(TEXT("Journal mesh exists"), Mesh)) { return false; }
	TestEqual(TEXT("Mesh supports the existing interaction trace"), Mesh->GetCollisionResponseToChannel(ECC_Visibility), ECR_Block);
	TestEqual(TEXT("Mesh has query-only collision"), Mesh->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);

	UDataTable* Table = NewObject<UDataTable>();
	Table->RowStruct = FJournalRow::StaticStruct();
	FJournalRow Row;
	Row.Title = FText::FromString(TEXT("첫 번째 기록"));
	Row.Desc = FText::FromString(TEXT("첫 줄\n다음 줄"));
	Table->AddRow(TEXT("First"), Row);
	SetEntry(Journal, Table, TEXT("First"));
	Journal->OnConstruction(Journal->GetActorTransform());
	TestEqual(TEXT("Construction resolves title"), Preview(Journal, TEXT("PreviewTitle")).ToString(), Row.Title.ToString());
	TestEqual(TEXT("Preview preserves newlines"), Preview(Journal, TEXT("PreviewDesc")).ToString(), Row.Desc.ToString());
	for (const TCHAR* Name : {TEXT("PreviewTitle"), TEXT("PreviewDesc")})
	{
		const FProperty* Property = FindFProperty<FProperty>(Journal->GetClass(), Name);
		TestTrue(TEXT("Preview is read-only"), Property->HasAnyPropertyFlags(CPF_EditConst));
		TestTrue(TEXT("Preview is transient"), Property->HasAnyPropertyFlags(CPF_Transient));
		TestFalse(TEXT("Preview is not replicated"), Property->HasAnyPropertyFlags(CPF_Net));
	}

	// 액터를 다시 생성하지 않고 DataTable 변경 통지만으로 갱신되는지 확인한다.
	Row.Title = FText::FromString(TEXT("수정된 기록"));
	Table->AddRow(TEXT("First"), Row);
	Table->HandleDataTableChanged(TEXT("First"));
	TestEqual(TEXT("Table edits refresh preview"), Preview(Journal, TEXT("PreviewTitle")).ToString(), Row.Title.ToString());

	SetEntry(Journal, Table, TEXT("Missing"));
	FPropertyChangedEvent Changed(FindFProperty<FProperty>(FDataTableRowHandle::StaticStruct(), TEXT("RowName")));
	Journal->PostEditChangeProperty(Changed);
	TestTrue(TEXT("Nested row edits clear missing title"), Preview(Journal, TEXT("PreviewTitle")).IsEmpty());
	TestTrue(TEXT("Nested row edits clear missing body"), Preview(Journal, TEXT("PreviewDesc")).IsEmpty());

	UDataTable* OtherTable = NewObject<UDataTable>();
	OtherTable->RowStruct = FJournalRow::StaticStruct();
	OtherTable->AddRow(TEXT("First"), Row);
	SetEntry(Journal, OtherTable, TEXT("First"));
	Journal->OnConstruction(Journal->GetActorTransform());
	TestFalse(TEXT("Switching tables unsubscribes the old table"), Table->OnDataTableChanged().IsBoundToObject(Journal));
	TestTrue(TEXT("Only the current table is watched"), OtherTable->OnDataTableChanged().IsBoundToObject(Journal));
	SetEntry(Journal, nullptr, NAME_None);
	Journal->OnConstruction(Journal->GetActorTransform());
	TestFalse(TEXT("Clearing the table removes its subscription"), OtherTable->OnDataTableChanged().IsBoundToObject(Journal));
	TestTrue(TEXT("Clearing the table clears preview"), Preview(Journal, TEXT("PreviewTitle")).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJournalInteractionContextTest, "Outlier.UI.Journal.InteractionContext",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FJournalInteractionContextTest::RunTest(const FString& Parameters)
{
	using namespace JournalInteractionTests;
	FScopedWorld Scope;
	if (!TestNotNull(TEXT("Test world exists"), Scope.World)) { return false; }
	AJournalInteraction* First = Scope.World->SpawnActor<AJournalInteraction>();
	AJournalInteraction* Second = Scope.World->SpawnActor<AJournalInteraction>();
	if (!TestNotNull(TEXT("First journal exists"), First) || !TestNotNull(TEXT("Second journal exists"), Second)) { return false; }
	UDataTable* Table = NewObject<UDataTable>();
	Table->RowStruct = FJournalRow::StaticStruct();
	FJournalRow Row;
	Row.Title = FText::FromString(TEXT("첫 번째"));
	Row.Desc = FText::FromString(TEXT("본문"));
	Table->AddRow(TEXT("First"), Row);
	Row.Title = FText::FromString(TEXT("두 번째"));
	Table->AddRow(TEXT("Second"), Row);
	SetEntry(First, Table, TEXT("First"));
	SetEntry(Second, Table, TEXT("Second"));

	UJournalWidget* Widget = NewObject<UJournalWidget>();
	UTextBlock* Title = NewObject<UTextBlock>(Widget);
	UTextBlock* Desc = NewObject<UTextBlock>(Widget);
	FindFProperty<FObjectPropertyBase>(Widget->GetClass(), TEXT("Title"))->SetObjectPropertyValue_InContainer(Widget, Title);
	FindFProperty<FObjectPropertyBase>(Widget->GetClass(), TEXT("Desc"))->SetObjectPropertyValue_InContainer(Widget, Desc);
	IUILayerContextReceiver::Execute_InitializeUILayerContext(Widget, TArray<AActor*>{First});
	TestEqual(TEXT("Context supplies the actor's row"), Title->GetText().ToString(), FString(TEXT("첫 번째")));
	TestEqual(TEXT("Context supplies the body"), Desc->GetText().ToString(), Row.Desc.ToString());

	// Actor 행 복제가 UI 생성보다 늦어지는 순서를 RepNotify 진입점으로 재현한다.
	SetEntry(First, Table, TEXT("Second"));
	First->ProcessEvent(First->FindFunctionChecked(TEXT("OnRep_JournalEntry")), nullptr);
	TestEqual(TEXT("RepNotify updates an already open view"), Title->GetText().ToString(), FString(TEXT("두 번째")));
	IUILayerContextReceiver::Execute_InitializeUILayerContext(Widget, TArray<AActor*>{Second});
	TestFalse(TEXT("Context replacement unbinds the old actor"), First->OnJournalEntryChanged.IsBoundToObject(Widget));
	TestTrue(TEXT("Context replacement watches the new actor"), Second->OnJournalEntryChanged.IsBoundToObject(Widget));
	AddExpectedErrorPlain(TEXT("[Journal] Refresh failed:"), EAutomationExpectedErrorFlags::Contains, 1);
	IUILayerContextReceiver::Execute_InitializeUILayerContext(Widget, TArray<AActor*>{});
	TestTrue(TEXT("Empty context clears stale content"), Title->GetText().IsEmpty() && Desc->GetText().IsEmpty());
	TestFalse(TEXT("Empty context removes the subscription"), Second->OnJournalEntryChanged.IsBoundToObject(Widget));
	return true;
}

#endif
