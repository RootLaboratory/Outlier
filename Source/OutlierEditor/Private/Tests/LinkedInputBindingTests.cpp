#if WITH_DEV_AUTOMATION_TESTS

#include "OutlierEditor/Tests/LinkedInputBindingTestSettings.h"
#include "Input/OutlierInputBindingTable.h"
#include "Input/OutlierInputBindingSettings.h"
#include "Settings/LocalPlayerSettingsSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedActionKeyMapping.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "PlayerMappableKeySettings.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

namespace
{
	FOutlierInputBindingTableRow MakeBinding(FName Name, FKey Key, FName ConflictGroup)
	{
		UInputMappingContext* Context = NewObject<UInputMappingContext>();
		UInputAction* Action = NewObject<UInputAction>();
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, Key);
		UPlayerMappableKeySettings* Settings = NewObject<UPlayerMappableKeySettings>(Context);
		Settings->Name = Name;
		FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(
			FEnhancedActionKeyMapping::StaticStruct(), TEXT("PlayerMappableKeySettings"));
		check(Property);
		Property->SetObjectPropertyValue_InContainer(&Mapping, Settings);
		FEnumProperty* Behavior = FindFProperty<FEnumProperty>(FEnhancedActionKeyMapping::StaticStruct(), TEXT("SettingBehavior"));
		if (Behavior) Behavior->GetUnderlyingProperty()->SetIntPropertyValue(
			Behavior->ContainerPtrToValuePtr<void>(&Mapping), static_cast<int64>(EPlayerMappableKeySettingBehaviors::OverrideSettings));
		else
		{
			FByteProperty* Byte = FindFProperty<FByteProperty>(FEnhancedActionKeyMapping::StaticStruct(), TEXT("SettingBehavior"));
			check(Byte);
			Byte->SetPropertyValue_InContainer(&Mapping, static_cast<uint8>(EPlayerMappableKeySettingBehaviors::OverrideSettings));
		}
		FOutlierInputBindingTableRow Row;
		Row.MappingContext = Context;
		Row.MappingName = Name;
		Row.InputAction = Action;
		Row.DefaultKey = Key;
		Row.ConflictGroup = ConflictGroup;
		return Row;
	}

	FOutlierLinkedInputMapping LinkTo(const FOutlierInputBindingTableRow& Row)
	{
		FOutlierLinkedInputMapping Link;
		Link.MappingContext = Row.MappingContext;
		Link.MappingName = Row.MappingName;
		Link.InputAction = Row.InputAction;
		Link.DefaultKey = Row.DefaultKey;
		Link.Slot = Row.Slot;
		return Link;
	}

	ULinkedInputBindingTestSettings* MakeSettings(UOutlierInputBindingTable* Table)
	{
		ULinkedInputBindingTestSettings* Settings = NewObject<ULinkedInputBindingTestSettings>();
		FPlayerMappableKeyProfileCreationArgs Args;
		Args.ProfileType = UEnhancedPlayerMappableKeyProfile::StaticClass();
		Args.ProfileStringIdentifier = TEXT("LinkedInputBindings.Test");
		Args.bSetAsCurrentProfile = true;
		Settings->CreateNewKeyProfile(Args);
		Settings->RegisterBindingContexts(Table);
		return Settings;
	}

	void MapSingle(UOutlierInputUserSettings* Settings, const FOutlierInputBindingTableRow& Row, FKey Key,
		EPlayerMappableKeySlot Slot = EPlayerMappableKeySlot::First)
	{
		FMapPlayerKeyArgs Args;
		Args.MappingName = Row.MappingName;
		Args.Slot = Slot;
		Args.NewKey = Key;
		FGameplayTagContainer Failure;
		Settings->MapPlayerKey(Args, Failure);
		check(Failure.IsEmpty());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLinkedInputBindingDefinitionsTest,
	"Outlier.Settings.LinkedInputBindings.Definitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLinkedInputBindingDefinitionsTest::RunTest(const FString& Parameters)
{
	UOutlierInputBindingTable* Table = NewObject<UOutlierInputBindingTable>();
	const auto Game = MakeBinding(TEXT("Linked.Game"), EKeys::W, TEXT("Gameplay"));
	const auto UI = MakeBinding(TEXT("Linked.UI"), EKeys::W, TEXT("UI"));
	Table->KeyboardRows = { Game, UI };
	TestEqual(TEXT("Unlinked rows remain independent"), Table->GetVisibleRowsSorted().Num(), 2);
	Table->KeyboardRows[0].LinkedMappings = { LinkTo(UI) };
	TestEqual(TEXT("Group has one displayed representative"), Table->GetVisibleRowsSorted().Num(), 1);
	TestEqual(TEXT("Hidden target remains in raw definitions"), Table->GetAllRowsSorted().Num(), 2);
	TArray<FOutlierInputBindingTableRow> Members;
	FString Error;
	TestTrue(TEXT("Valid group resolves"), Table->ResolveLinkedRows(Table->KeyboardRows[0], Members, Error));
	TestEqual(TEXT("Both mapping targets resolved"), Members.Num(), 2);
	Table->KeyboardRows[1].LinkedMappings = { LinkTo(Game) };
	TestEqual(TEXT("Cycle exposes both original rows"), Table->GetVisibleRowsSorted().Num(), 2);
	Table->KeyboardRows[1].LinkedMappings.Reset();
	Table->KeyboardRows[0].LinkedMappings.Add(LinkTo(UI));
	TestEqual(TEXT("Duplicate reference exposes originals"), Table->GetVisibleRowsSorted().Num(), 2);
	Table->KeyboardRows[0].LinkedMappings.SetNum(1);
	Table->KeyboardRows[0].LinkedMappings[0].MappingName = TEXT("Missing.Mapping");
	TestFalse(TEXT("Missing mapping rejected"), Table->ResolveLinkedRows(Table->KeyboardRows[0], Members, Error));
	Table->KeyboardRows[0].LinkedMappings = { LinkTo(UI) };
	Table->KeyboardRows[1].DefaultKey = EKeys::Up;
	TestFalse(TEXT("Mismatched defaults rejected"), Table->ResolveLinkedRows(Table->KeyboardRows[0], Members, Error));
	Table->KeyboardRows[1] = UI;
	auto Other = MakeBinding(TEXT("Linked.Other"), EKeys::W, TEXT("Other"));
	Other.LinkedMappings = { LinkTo(UI) };
	Table->KeyboardRows.Add(Other);
	TestEqual(TEXT("Both conflicting owners expose all originals"), Table->GetVisibleRowsSorted().Num(), 3);
	Table->KeyboardRows.Pop();

	// 실제 에셋을 변경하지 않고 임시 IMC를 재생성하여 연결 정보 보존을 확인한다.
	UOutlierInputBindingSettings* Scan = GetMutableDefault<UOutlierInputBindingSettings>();
	const auto PreviousContexts = Scan->InputMappingContextsToScan;
	const auto PreviousDirectory = Scan->ContentInputDirectory;
	ON_SCOPE_EXIT { Scan->InputMappingContextsToScan = PreviousContexts; Scan->ContentInputDirectory = PreviousDirectory; };
	Scan->InputMappingContextsToScan = { Game.MappingContext, UI.MappingContext };
	Scan->ContentInputDirectory.Path.Empty();
	Table->RebuildRowsFromConfiguredIMCs();
	TestEqual(TEXT("Regeneration retains both raw rows"), Table->GetAllRowsSorted().Num(), 2);
	TestEqual(TEXT("Regeneration retains the explicit group"), Table->GetVisibleRowsSorted().Num(), 1);
	Table->KeyboardRows = { Game, UI };
	Table->KeyboardRows[1].bRebindable = false;
	Table->KeyboardRows[0].LinkedMappings = { LinkTo(UI) };
	TestFalse(TEXT("Non-rebindable target rejected"), Table->ResolveLinkedRows(Table->KeyboardRows[0], Members, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLinkedInputBindingProfileTest,
	"Outlier.Settings.LinkedInputBindings.ProfileAndRollback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLinkedInputBindingProfileTest::RunTest(const FString& Parameters)
{
	UOutlierInputBindingTable* Table = NewObject<UOutlierInputBindingTable>();
	auto Game = MakeBinding(TEXT("Linked.Game"), EKeys::W, TEXT("Gameplay"));
	const auto UI = MakeBinding(TEXT("Linked.UI"), EKeys::W, TEXT("UI"));
	const auto Hidden = MakeBinding(TEXT("Linked.Hidden"), EKeys::H, TEXT("UI"));
	Game.LinkedMappings = { LinkTo(UI) };
	Table->KeyboardRows = { Game, UI, Hidden };
	ULinkedInputBindingTestSettings* Settings = MakeSettings(Table);
	FString Error;
	FKey Key;
	TestEqual(TEXT("UI-only external conflict rejects whole change"), Settings->ChangeLinkedBinding(Table, Game, EKeys::H, Error), EOutlierLinkedBindingResult::Conflict);
	Settings->GetBindingKey(Game, Key);
	TestEqual(TEXT("Rejected group preserves game key"), Key, EKeys::W);
	TestEqual(TEXT("Both contexts accept same new key"), Settings->ChangeLinkedBinding(Table, Game, EKeys::K, Error), EOutlierLinkedBindingResult::Success);
	Settings->GetBindingKey(UI, Key);
	TestEqual(TEXT("Linked UI key updated"), Key, EKeys::K);
	MapSingle(Settings, Game, EKeys::L, EPlayerMappableKeySlot::Second);
	TestEqual(TEXT("External secondary slot is a conflict"), Settings->ValidateLinkedBinding(Table, Game, EKeys::L, Error), EOutlierLinkedBindingResult::Conflict);
	TestEqual(TEXT("Reset succeeds for owned slots"), Settings->ChangeLinkedBinding(Table, Game, EKeys::W, Error), EOutlierLinkedBindingResult::Success);
	const FPlayerKeyMapping* Secondary = Settings->FindCurrentMappingForSlot(Game.MappingName, EPlayerMappableKeySlot::Second);
	if (TestNotNull(TEXT("Secondary survives reset"), Secondary)) TestEqual(TEXT("Secondary key preserved"), Secondary->GetCurrentKey(), EKeys::L);
	Settings->RequestCount = 0;
	Settings->FailOnRequest = 2;
	TestEqual(TEXT("Second map failure rolls back first"), Settings->ChangeLinkedBinding(Table, Game, EKeys::J, Error), EOutlierLinkedBindingResult::Failed);
	for (const auto& Row : { Game, UI })
	{
		Settings->GetBindingKey(Row, Key);
		TestEqual(TEXT("Rollback restores original key"), Key, EKeys::W);
		const auto* Mapping = Settings->FindCurrentMappingForSlot(Row.MappingName, Row.Slot);
		if (TestNotNull(TEXT("Restored slot"), Mapping)) TestFalse(TEXT("Rollback restores customization state"), Mapping->IsCustomized());
	}
	TestEqual(TEXT("Transaction never saves partial state"), Settings->SaveCount, 0);
	Settings->FailOnRequest = INDEX_NONE;
	MapSingle(Settings, UI, EKeys::K);
	bool bNeedsSync = false;
	TestTrue(TEXT("Existing linked-only override resolves"), Settings->ChooseLinkedBindingKey(Table, Game, Key, bNeedsSync, Error));
	TestEqual(TEXT("Linked override is adopted"), Key, EKeys::K);
	TestTrue(TEXT("Mismatch requires synchronization"), bNeedsSync);
	MapSingle(Settings, Game, EKeys::J);
	Settings->ChooseLinkedBindingKey(Table, Game, Key, bNeedsSync, Error);
	TestEqual(TEXT("Representative override wins"), Key, EKeys::J);
	TestEqual(TEXT("Synchronize resolved key"), Settings->ChangeLinkedBinding(Table, Game, Key, Error), EOutlierLinkedBindingResult::Success);
	Settings->ChooseLinkedBindingKey(Table, Game, Key, bNeedsSync, Error);
	TestFalse(TEXT("Repeated initialization requires no rewrite"), bNeedsSync);
	Settings->RequestCount = 0;
	Settings->FailOnRequest = 2;
	TestEqual(TEXT("Second reset failure restores customized keys"), Settings->ChangeLinkedBinding(Table, Game, EKeys::W, Error), EOutlierLinkedBindingResult::Failed);
	Settings->GetBindingKey(Game, Key);
	TestEqual(TEXT("Failed reset restores representative override"), Key, EKeys::J);
	Settings->GetBindingKey(UI, Key);
	TestEqual(TEXT("Failed reset preserves linked override"), Key, EKeys::J);
	Settings->FailOnRequest = INDEX_NONE;
	TArray<uint8> Bytes;
	if (TestTrue(TEXT("Profile serializes in memory"), UGameplayStatics::SaveGameToMemory(Settings, Bytes)))
	{
		auto* Reloaded = Cast<UOutlierInputUserSettings>(UGameplayStatics::LoadGameFromMemory(Bytes));
		if (TestNotNull(TEXT("Profile reloads"), Reloaded))
		{
			Reloaded->RegisterBindingContexts(Table);
			Reloaded->GetBindingKey(Game, Key);
			TestEqual(TEXT("Representative key survives reload"), Key, EKeys::J);
			Reloaded->GetBindingKey(UI, Key);
			TestEqual(TEXT("Linked key survives reload"), Key, EKeys::J);
		}
	}
	return true;
}

struct FLinkedInputBindingSettingsTestAccess
{
	static void Bind(ULocalPlayerSettingsSubsystem* Subsystem, ULinkedInputBindingTestSettings* Settings,
		UOutlierInputBindingTable* Table)
	{
		Subsystem->InputUserSettings = Settings;
		Subsystem->InputBindingDefinitions = Table;
	}
	static void Synchronize(ULocalPlayerSettingsSubsystem* Subsystem) { Subsystem->SynchronizeLinkedInputBindings(); }
	static void ProfileChanged(ULocalPlayerSettingsSubsystem* Subsystem, UEnhancedPlayerMappableKeyProfile* Profile)
	{
		Subsystem->HandleInputKeyProfileChanged(Profile);
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLinkedInputBindingLifecycleTest,
	"Outlier.Settings.LinkedInputBindings.InitializationAndConflicts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLinkedInputBindingLifecycleTest::RunTest(const FString& Parameters)
{
	UOutlierInputBindingTable* Table = NewObject<UOutlierInputBindingTable>();
	auto Game = MakeBinding(TEXT("Linked.Game"), EKeys::W, TEXT("Gameplay"));
	const auto UI = MakeBinding(TEXT("Linked.UI"), EKeys::W, TEXT("UI"));
	const auto UISecond = MakeBinding(TEXT("Linked.UISecond"), EKeys::W, TEXT("UI"));
	Game.LinkedMappings = { LinkTo(UI), LinkTo(UISecond) };
	Table->KeyboardRows = { Game, UI, UISecond };
	ULinkedInputBindingTestSettings* Settings = MakeSettings(Table);
	MapSingle(Settings, UI, EKeys::J);
	MapSingle(Settings, UISecond, EKeys::K);
	FKey Key;
	FString Error;
	bool bNeedsSync = false;
	Settings->ChooseLinkedBindingKey(Table, Game, Key, bNeedsSync, Error);
	TestEqual(TEXT("Disagreeing linked-only overrides use representative default"), Key, EKeys::W);
	TestTrue(TEXT("Disagreement requires repair"), bNeedsSync);
	ULocalPlayer* Player = NewObject<ULocalPlayer>();
	ULocalPlayerSettingsSubsystem* Subsystem = NewObject<ULocalPlayerSettingsSubsystem>(Player);
	FLinkedInputBindingSettingsTestAccess::Bind(Subsystem, Settings, Table);
	FLinkedInputBindingSettingsTestAccess::Synchronize(Subsystem);
	for (const auto& Row : { Game, UI, UISecond })
	{
		Settings->GetBindingKey(Row, Key);
		TestEqual(TEXT("Initialization synchronizes without a settings widget"), Key, EKeys::W);
	}
	TestEqual(TEXT("Initialization saves once after repair"), Settings->SaveCount, 1);
	FLinkedInputBindingSettingsTestAccess::Synchronize(Subsystem);
	TestEqual(TEXT("Repeated initialization does not save again"), Settings->SaveCount, 1);
	MapSingle(Settings, Game, EKeys::H);
	auto External = MakeBinding(TEXT("Linked.ExternalUI"), EKeys::H, TEXT("UI"));
	Table->KeyboardRows.Add(External);
	Settings->RegisterBindingContexts(Table);
	FLinkedInputBindingSettingsTestAccess::Synchronize(Subsystem);
	TestFalse(TEXT("Failed initialization exposes representative error"), Subsystem->GetLinkedInputBindingError(Game).IsEmpty());
	Settings->GetBindingKey(UI, Key);
	TestEqual(TEXT("Failed initialization preserves linked key"), Key, EKeys::W);
	TestEqual(TEXT("Failed initialization never persists partial state"), Settings->SaveCount, 1);
	TestEqual(TEXT("User resolves mismatch with a valid key"), Subsystem->ChangeLinkedInputBinding(Table, Game, EKeys::J, Error), EOutlierLinkedBindingResult::Success);
	TestTrue(TEXT("Successful rebind clears mismatch status"), Subsystem->GetLinkedInputBindingError(Game).IsEmpty());
	TestEqual(TEXT("Mouse wheel restrictions apply to every target"), Settings->ValidateLinkedBinding(Table, Game, EKeys::MouseScrollUp, Error), EOutlierLinkedBindingResult::InputRequired);
	External.InputAction->bReserveAllMappings = true;
	Table->KeyboardRows.Last().ConflictGroup = TEXT("Unrelated");
	TestEqual(TEXT("Reserved action blocks even a different conflict group"), Settings->ValidateLinkedBinding(Table, Game, EKeys::H, Error), EOutlierLinkedBindingResult::Conflict);
	TestEqual(TEXT("Invalid analog replacement rejected"), Settings->ValidateLinkedBinding(Table, Game, EKeys::MouseX, Error), EOutlierLinkedBindingResult::InputRequired);
	UOutlierInputBindingTable* SharedTable = NewObject<UOutlierInputBindingTable>();
	auto SharedGame = MakeBinding(TEXT("Linked.SharedName"), EKeys::Escape, TEXT("Gameplay"));
	const auto SharedUI = MakeBinding(TEXT("Linked.SharedName"), EKeys::Escape, TEXT("UI"));
	SharedGame.LinkedMappings = { LinkTo(SharedUI) };
	SharedTable->KeyboardRows = { SharedGame, SharedUI };
	auto* SharedSettings = MakeSettings(SharedTable);
	SharedSettings->RequestCount = 0;
	TestEqual(TEXT("Same stored slot can connect two IMCs"), SharedSettings->ChangeLinkedBinding(SharedTable, SharedGame, EKeys::F, Error), EOutlierLinkedBindingResult::Success);
	TestEqual(TEXT("Shared slot is mapped only once"), SharedSettings->RequestCount, 1);
	TestEqual(TEXT("Shared Escape group restores both contexts"), SharedSettings->ChangeLinkedBinding(SharedTable, SharedGame, EKeys::Escape, Error), EOutlierLinkedBindingResult::Success);
	const uint32 Revision = Subsystem->GetInputSettingsContextRevision();
	FLinkedInputBindingSettingsTestAccess::ProfileChanged(Subsystem, Settings->GetActiveKeyProfile());
	TestTrue(TEXT("Profile event invalidates pending captures"), Subsystem->GetInputSettingsContextRevision() > Revision);
	return true;
}

#endif
