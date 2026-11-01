#if WITH_DEV_AUTOMATION_TESTS

#include "UI/SettingInputPageWidget.h"
#include "UI/InputBindingRowWidget.h"
#include "UI/SprintInputModeWidget.h"
#include "Input/OutlierInputBindingSettings.h"
#include "Settings/LocalPlayerSettingsSubsystem.h"
#include "GameFramework/Character.h"
#include "OutlierPlayerState.h"
#include "FrontendPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedActionKeyMapping.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "PlayerMappableKeySettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInputSettingsScopeTest,
	"Outlier.Settings.InputPresentation.Scope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInputSettingsScopeTest::RunTest(const FString& Parameters)
{
	using EScope = EOutlierInputSettingScope;
	using ERole = EOutlierPlayerRole;
	const ERole Roles[] = { ERole::None, ERole::Shooter, ERole::Partner };
	const EScope Scopes[] = { EScope::Common, EScope::BeforeRoleSelection, EScope::Shooter, EScope::Partner };
	for (int32 RoleIndex = 0; RoleIndex < 3; ++RoleIndex)
	{
		for (int32 ScopeIndex = 0; ScopeIndex < 4; ++ScopeIndex)
		{
			TestEqual(FString::Printf(TEXT("Scope %d / role %d"), ScopeIndex, RoleIndex),
				UOutlierInputBindingTable::IsScopeVisibleForRole(static_cast<int32>(Scopes[ScopeIndex]), Roles[RoleIndex]),
				ScopeIndex == 0 || ScopeIndex == RoleIndex + 1);
		}
		TestTrue(TEXT("Combined role scope visible"), UOutlierInputBindingTable::IsScopeVisibleForRole(
			static_cast<int32>(EScope::BeforeRoleSelection | EScope::Shooter | EScope::Partner), Roles[RoleIndex]));
	}
	FOutlierInputBindingTableRow Row;
	TestTrue(TEXT("Unclassified contexts remain Common"), Row.IsVisibleForRole(ERole::None));
	Row.MappingContext = TSoftObjectPtr<UInputMappingContext>(FSoftObjectPath(TEXT("/Game/Input/Contexts/IMC_Shooter.IMC_Shooter")));
	TestFalse(TEXT("Configured Shooter context hidden before role selection"), Row.IsVisibleForRole(ERole::None));
	TestTrue(TEXT("Configured Shooter context visible to Shooter"), Row.IsVisibleForRole(ERole::Shooter));
	TestFalse(TEXT("Configured Shooter context hidden from Partner"), Row.IsVisibleForRole(ERole::Partner));
	Row.DisplayScopes = static_cast<int32>(EScope::Partner);
	TestTrue(TEXT("Explicit row metadata overrides context rule"), Row.IsVisibleForRole(ERole::Partner));
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInputSettingsMetadataTest,
	"Outlier.Settings.InputPresentation.Regeneration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInputSettingsMetadataTest::RunTest(const FString& Parameters)
{
	UOutlierInputBindingSettings* Settings = GetMutableDefault<UOutlierInputBindingSettings>();
	const auto PreviousContexts = Settings->InputMappingContextsToScan;
	const auto PreviousDirectory = Settings->ContentInputDirectory;
	ON_SCOPE_EXIT { Settings->InputMappingContextsToScan = PreviousContexts; Settings->ContentInputDirectory = PreviousDirectory; };
	// 임시 IMC만 스캔하여 실제 DataAsset과 사용자 설정 파일을 변경하지 않는다.
	UInputMappingContext* Context = NewObject<UInputMappingContext>();
	UInputAction* Action = NewObject<UInputAction>();
	FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, EKeys::F);
	UPlayerMappableKeySettings* Mappable = NewObject<UPlayerMappableKeySettings>(Context);
	Mappable->Name = TEXT("InputPresentation.Test");
	FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(FEnhancedActionKeyMapping::StaticStruct(), TEXT("PlayerMappableKeySettings"));
	if (!TestNotNull(TEXT("Mapping-owned metadata property"), Property)) return false;
	Property->SetObjectPropertyValue_InContainer(&Mapping, Mappable);
	Settings->InputMappingContextsToScan = { Context };
	Settings->ContentInputDirectory.Path.Empty();
	UOutlierInputBindingTable* Table = NewObject<UOutlierInputBindingTable>();
	Table->RebuildRowsFromConfiguredIMCs();
	if (!TestEqual(TEXT("Temporary mapping generated"), Table->KeyboardRows.Num(), 1)) return false;
	Table->KeyboardRows[0].DisplayScopes = static_cast<int32>(EOutlierInputSettingScope::BeforeRoleSelection | EOutlierInputSettingScope::Partner);
	Table->KeyboardRows[0].bAllowMouseWheel = true;
	int32 Notifications = 0;
	Table->OnRowsChanged.AddLambda([&Notifications]() { ++Notifications; });
	Table->RebuildRowsFromConfiguredIMCs();
	if (!TestEqual(TEXT("Mapping survives regeneration"), Table->KeyboardRows.Num(), 1)) return false;
	TestEqual(TEXT("Display metadata preserved"), Table->KeyboardRows[0].DisplayScopes, 10);
	TestTrue(TEXT("Existing wheel metadata preserved"), Table->KeyboardRows[0].bAllowMouseWheel);
	TestEqual(TEXT("Definition refresh broadcast"), Notifications, 1);
	TestEqual(TEXT("Hidden role rows remain in full binding definitions"), Table->GetVisibleRowsSorted().Num(), 1);
	Table->OnRowsChanged.Clear();
	return true;
}
#endif

struct FInputSettingsPresentationTestAccess
{
	static void CheckPage(FAutomationTestBase& Test, USettingInputPageWidget* Page, UWorld* World, UClass* RowClass,
		ULocalPlayerSettingsSubsystem* Settings)
	{
		using ERole = EOutlierPlayerRole;
		using EScope = EOutlierInputSettingScope;
		Page->InputBindingListHost = Page->WidgetTree->ConstructWidget<UCanvasPanel>();
		// 아직 WBP에 행이 없어도 대체 UI를 자동 생성하지 않는다.
		Page->SprintModeRow = nullptr;
		Page->RefreshSprintInputModeRow();
		Test.TestNull(TEXT("Missing module has no generated fallback"), Page->SprintModeRow.Get());
		USprintInputModeWidget* Module = CreateWidget<USprintInputModeWidget>(World);
		if (!Test.TestNotNull(TEXT("Sprint mode module"), Module)) return;
		UVerticalBox* DesignerHost = Page->WidgetTree->ConstructWidget<UVerticalBox>();
		DesignerHost->AddChildToVerticalBox(Module);
		Page->SprintModeRow = Module;
		Module->SprintModeLeftButton = Page->WidgetTree->ConstructWidget<UButton>();
		Module->SprintModeRightButton = Page->WidgetTree->ConstructWidget<UButton>();
		Module->SprintModeText = Page->WidgetTree->ConstructWidget<UTextBlock>();
		Module->BindButtons();
		// IsVisible은 생성된 Slate 위젯을 조회한다. 디자이너 Host가 화면에 없는 테스트도 그 수명을 유지한다.
		const TSharedRef<SWidget> ModuleSlate = Module->TakeWidget();
		Test.TestTrue(TEXT("Module left button bound"), Module->SprintModeLeftButton->OnClicked.IsBound());
		Test.TestTrue(TEXT("Module right button bound"), Module->SprintModeRightButton->OnClicked.IsBound());
		UWidget* OriginalSprintRow = Page->SprintModeRow;
		const EScope Scopes[] = { EScope::Common, EScope::BeforeRoleSelection, EScope::Shooter, EScope::Partner };
		const FKey Keys[] = { EKeys::F, EKeys::G, EKeys::H, EKeys::J };
		for (int32 Index = 0; Index < 4; ++Index)
		{
			UInputBindingRowWidget* Widget = CreateWidget<UInputBindingRowWidget>(World, RowClass);
			if (!Test.TestNotNull(TEXT("Binding fixture"), Widget)) return;
			FOutlierInputBindingTableRow Row;
			Row.DisplayScopes = static_cast<int32>(Scopes[Index]);
			Row.DefaultKey = Keys[Index];
			Row.MappingName = FName(*FString::Printf(TEXT("Presentation%d"), Index));
			Widget->InitializeInputBindingRow(Row, Index);
			Page->CachedInputBindingRows.Add(Widget);
		}
		Page->bInputBindingListBuilt = true;
		Page->RuntimeInputKeyOverrides.Add(TEXT("Presentation3"), EKeys::L);
		const auto OriginalRows = Page->CachedInputBindingRows;
		// 실제 WBP의 상하 패딩도 행 간격에 포함된다. 필터로 생기는 빈 행만 검사한다.
		const float ExpectedRowStride = Page->InputBindingRowYOffset
			+ Page->InputBindingRowPadding.Top + Page->InputBindingRowPadding.Bottom;
		for (ERole Role : { ERole::None, ERole::Shooter, ERole::Partner, ERole::None, ERole::Shooter })
		{
			Page->HandleInputSettingsContextChanged(Role);
			Test.TestEqual(TEXT("Exactly two binding rows visible"), Page->DisplayedInputBindingRows.Num(), 2);
			Test.TestEqual(TEXT("Only key bindings enter generated list"), Page->InputBindingListHost->GetChildrenCount(), 2);
			Test.TestEqual(TEXT("Designer mode row visible only after role selection"), ModuleSlate->GetVisibility().IsVisible(), Role != ERole::None);
			Test.TestTrue(TEXT("Designer mode row retains its parent"), Page->SprintModeRow->GetParent() == DesignerHost);
			Test.TestTrue(TEXT("All widget references reused"), Page->CachedInputBindingRows == OriginalRows);
			Test.TestTrue(TEXT("Mode row reused"), Page->SprintModeRow == OriginalSprintRow);
			for (int32 Index = 0; Index < Page->InputBindingListHost->GetChildrenCount(); ++Index)
			{
				const UCanvasPanelSlot* Slot = CastChecked<UCanvasPanelSlot>(Page->InputBindingListHost->GetChildAt(Index)->Slot);
				Test.TestEqual(TEXT("Filtered Canvas rows are contiguous"), Slot->GetPosition().Y, static_cast<double>(ExpectedRowStride * Index));
			}
			for (UInputBindingRowWidget* Row : Page->CachedInputBindingRows)
			{
				Test.TestEqual(TEXT("Hidden bindings excluded from focus"), Page->KeyboardFocusTargets.Contains(Row), Row->GetInputBindingRow().IsVisibleForRole(Role));
			}
		}
		Test.TestTrue(TEXT("Hidden Partner binding retains rebound key conflict"), Page->IsInputKeyDuplicate(EKeys::L, OriginalRows[0]));
		Page->bWaitingForInputRebind = true;
		Page->PendingRebindInputBindingRow = OriginalRows[2];
		Page->HandleInputSettingsContextChanged(ERole::Partner);
		Test.TestFalse(TEXT("Role change cancels capture"), Page->IsCapturingKeyboardInput());
		Test.TestNull(TEXT("Role change clears pending row"), Page->PendingRebindInputBindingRow.Get());
		Page->bInputPageActive = true;
		Test.TestFalse(TEXT("Hidden stale callback cannot restart capture"), Page->StartInputRebind(OriginalRows[2]));
		Test.TestFalse(TEXT("Cancelled key cannot commit"), Page->TryCommitInputRebind(EKeys::K));
		Page->BoundSettingsSubsystem = Settings;
		Page->DisplayedRole = Settings->GetInputSettingsRole();
		Page->RefreshSprintInputModeRow();
		Page->PendingRebindInputBindingRow = OriginalRows[0];
		Page->bWaitingForInputRebind = true;
		Page->RebindContextRevision = Settings->GetInputSettingsContextRevision() - 1;
		Test.TestFalse(TEXT("Stale owner revision rejects even a still-visible Common row"), Page->TryCommitInputRebind(EKeys::K));
		Test.TestFalse(TEXT("Stale sprint click rejected before writing settings"),
			Module->CycleMode(Settings->GetInputSettingsContextRevision() - 1));
		Module->HandlePressed();
		Module->SetSettingsContext(Settings, Settings->GetInputSettingsRole(), false);
		Test.TestEqual(TEXT("Module context refresh cancels pending click"), Module->PressRevision, MAX_uint32);
		Test.TestFalse(TEXT("Inactive module cannot change settings"), Module->CycleMode());
		Page->BoundSettingsSubsystem = nullptr;
		Page->RefreshSprintInputModeRow();
		Page->bInputPageActive = false;
		// 기존 Canvas뿐 아니라 VerticalBox에서도 동일한 캐시를 옮겨 표시한다.
		Page->InputBindingListHost->ClearChildren();
		Page->InputBindingListHost = Page->WidgetTree->ConstructWidget<UVerticalBox>();
		Page->HandleInputSettingsContextChanged(ERole::Partner);
		Test.TestEqual(TEXT("VerticalBox displays two bindings"), Page->InputBindingListHost->GetChildrenCount(), 2);
		Page->HandleInputSettingsContextChanged(ERole::None);
		Test.TestEqual(TEXT("VerticalBox removes mode before role selection"), Page->InputBindingListHost->GetChildrenCount(), 2);
		Test.TestTrue(TEXT("VerticalBox preserves cached definitions"), Page->CachedInputBindingRows == OriginalRows);
		Test.TestTrue(TEXT("Host refresh preserves designer row layout"), Page->SprintModeRow->GetParent() == DesignerHost);
		CheckModeIntegration(Test, Page, Settings);
	}

	static void CheckModeIntegration(FAutomationTestBase& Test, USettingInputPageWidget* Page,
		ULocalPlayerSettingsSubsystem* Settings)
	{
		using ERole = EOutlierPlayerRole;
		using EMode = EOutlierSprintInputMode;
		AOutlierPlayerState* State = Settings->InputSettingsPlayerState.Get();
		if (!Test.TestNotNull(TEXT("Mode integration uses the local role source"), State)) return;
		USprintInputModeWidget* Module = Page->SprintModeRow;
		UOutlierInputUserSettings* PreviousSettings = Settings->InputUserSettings;
		UOutlierInputUserSettings* UserSettings = NewObject<UOutlierInputUserSettings>();
		Settings->InputUserSettings = UserSettings;
		Page->BoundSettingsSubsystem = Settings;
		Page->bInputPageActive = true;
		Settings->OnInputSettingsContextChanged.AddUObject(Page, &USettingInputPageWidget::HandleInputSettingsContextChanged);
		ON_SCOPE_EXIT
		{
			Settings->OnInputSettingsContextChanged.RemoveAll(Page);
			Module->SetSettingsContext(nullptr, ERole::None, false);
			Page->BoundSettingsSubsystem = nullptr;
			Page->bInputPageActive = false;
			Settings->InputUserSettings = PreviousSettings;
		};
		// 사용자 세이브 경로와 연결하지 않은 객체로 실제 버튼→설정 변경 경로만 실행한다.
		// SaveSettings의 소유자 없음 경고는 세 번의 유효한 클릭만 발생해야 한다.
		Test.TestNull(TEXT("UI integration never attaches a user save owner"), UserSettings->GetLocalPlayer());
		Test.AddExpectedMessagePlain(TEXT("Attempting to save Enhanced Input User settings without an owning local player!"),
			ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 3);
		const auto Click = [](UButton* Button)
		{
			Button->OnPressed.Broadcast();
			Button->OnClicked.Broadcast();
		};
		State->SetPlayerRole(ERole::Shooter);
		Page->HandleInputSettingsContextChanged(Settings->GetInputSettingsRole());
		Test.TestEqual(TEXT("Role source selects Shooter in the shared module"), Module->DisplayedRole, ERole::Shooter);
		Test.TestEqual(TEXT("Shooter initially displays Toggle"), Module->SprintModeText->GetText().ToString(), FString(TEXT("Toggle")));
		Click(Module->SprintModeRightButton);
		Test.TestEqual(TEXT("Right button changes Shooter preference through the subsystem"), Settings->GetSprintInputMode(ERole::Shooter), EMode::Hold);
		Test.TestEqual(TEXT("Mode change delegate refreshes the text"), Module->SprintModeText->GetText().ToString(), FString(TEXT("Hold")));
		Test.TestEqual(TEXT("Shooter click preserves Partner preference"), Settings->GetSprintInputMode(ERole::Partner), EMode::Toggle);

		State->SetPlayerRole(ERole::Partner);
		Test.TestEqual(TEXT("Role event retargets the existing module"), Module->DisplayedRole, ERole::Partner);
		Test.TestEqual(TEXT("Role refresh reads Partner without overwriting it"), Module->SprintModeText->GetText().ToString(), FString(TEXT("Toggle")));
		Click(Module->SprintModeLeftButton);
		Test.TestEqual(TEXT("Left button changes only Partner preference"), Settings->GetSprintInputMode(ERole::Partner), EMode::Hold);
		Test.TestEqual(TEXT("Partner click preserves Shooter Hold"), Settings->GetSprintInputMode(ERole::Shooter), EMode::Hold);
		// 페이지 재진입처럼 바인딩을 반복해도 한 클릭은 한 번만 처리한다.
		Module->BindButtons();
		Module->BindButtons();
		Click(Module->SprintModeRightButton);
		Test.TestEqual(TEXT("Repeated button binding still toggles once"), Settings->GetSprintInputMode(ERole::Partner), EMode::Toggle);

		Module->SprintModeLeftButton->OnPressed.Broadcast();
		State->SetPlayerRole(ERole::Shooter);
		Module->SprintModeLeftButton->OnClicked.Broadcast();
		Test.TestEqual(TEXT("Old-role click cannot change Shooter"), Settings->GetSprintInputMode(ERole::Shooter), EMode::Hold);
		Test.TestEqual(TEXT("Old-role click cannot change Partner"), Settings->GetSprintInputMode(ERole::Partner), EMode::Toggle);
		Test.TestEqual(TEXT("Returning role displays its independent preference"), Module->SprintModeText->GetText().ToString(), FString(TEXT("Hold")));
		Module->SprintModeRightButton->OnPressed.Broadcast();
		State->SetPlayerRole(ERole::None);
		Module->SprintModeRightButton->OnClicked.Broadcast();
		Test.TestFalse(TEXT("Role removal hides the shared mode module"), Module->IsVisible());
		Test.TestEqual(TEXT("Role removal discards a pending click"), Settings->GetSprintInputMode(ERole::Shooter), EMode::Hold);

		TArray<uint8> Bytes;
		if (Test.TestTrue(TEXT("UI preferences serialize without writing a save slot"), UGameplayStatics::SaveGameToMemory(UserSettings, Bytes)))
		{
			UOutlierInputUserSettings* Loaded = Cast<UOutlierInputUserSettings>(UGameplayStatics::LoadGameFromMemory(Bytes));
			if (Test.TestNotNull(TEXT("UI preferences reload in memory"), Loaded))
			{
				Test.TestEqual(TEXT("UI round trip keeps Shooter Hold"), Loaded->GetSprintInputMode(ERole::Shooter), EMode::Hold);
				Test.TestEqual(TEXT("UI round trip keeps Partner Toggle"), Loaded->GetSprintInputMode(ERole::Partner), EMode::Toggle);
			}
		}
		UOutlierInputUserSettings* OtherUser = NewObject<UOutlierInputUserSettings>();
		Test.TestEqual(TEXT("UI edits do not affect another user"), OtherUser->GetSprintInputMode(ERole::Shooter), EMode::Toggle);
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInputSettingsPresentationTest,
	"Outlier.Settings.InputPresentation.ContextAndWidgets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInputSettingsPresentationTest::RunTest(const FString& Parameters)
{
	using ERole = EOutlierPlayerRole;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Fixture world"), World)) return false;
	World->AddToRoot();
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); World->RemoveFromRoot(); };
	ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
	ULocalPlayerSettingsSubsystem* Settings = NewObject<ULocalPlayerSettingsSubsystem>(LocalPlayer);
	ON_SCOPE_EXIT { LocalPlayer->PlayerController = nullptr; Settings->RefreshInputSettingsContext(); };
	AFrontendPlayerController* LobbyController = World->SpawnActor<AFrontendPlayerController>();
	AOutlierPlayerState* State = World->SpawnActor<AOutlierPlayerState>();
	if (!TestNotNull(TEXT("Lobby controller"), LobbyController) || !TestNotNull(TEXT("PlayerState"), State)) return false;
	LocalPlayer->PlayerController = LobbyController;
	Settings->RefreshInputSettingsContext();
	TestEqual(TEXT("Late PlayerState starts with no role"), Settings->GetInputSettingsRole(), ERole::None);
	LobbyController->PlayerState = State;
	Settings->RefreshInputSettingsContext();
	TestEqual(TEXT("No pending role before selection"), Settings->GetInputSettingsRole(), ERole::None);
	State->SetPlayerRole(ERole::Shooter);
	TestEqual(TEXT("Lobby ignores gameplay role"), Settings->GetInputSettingsRole(), ERole::None);
	State->SetPendingLobbyRole(ERole::Partner);
	TestEqual(TEXT("Lobby follows pending role without polling"), Settings->GetInputSettingsRole(), ERole::Partner);
	const uint32 Revision = Settings->GetInputSettingsContextRevision();
	AOutlierPlayerState* OtherState = World->SpawnActor<AOutlierPlayerState>();
	OtherState->SetPendingLobbyRole(ERole::Shooter);
	TestEqual(TEXT("Other player cannot change local context"), Settings->GetInputSettingsContextRevision(), Revision);
	APlayerController* GameController = World->SpawnActor<APlayerController>();
	GameController->PlayerState = State;
	LocalPlayer->PlayerController = GameController;
	Settings->RefreshInputSettingsContext();
	TestEqual(TEXT("Gameplay follows actual role"), Settings->GetInputSettingsRole(), ERole::Shooter);
	TestTrue(TEXT("Controller change invalidates previous callbacks"), Settings->GetInputSettingsContextRevision() > Revision);
	LocalPlayer->PlayerController = nullptr;
	Settings->RefreshInputSettingsContext();
	TestEqual(TEXT("Owner removal resets context"), Settings->GetInputSettingsRole(), ERole::None);
	const uint32 DetachedRevision = Settings->GetInputSettingsContextRevision();
	State->SetPlayerRole(ERole::Partner);
	TestEqual(TEXT("Detached PlayerState no longer updates context"), Settings->GetInputSettingsContextRevision(), DetachedRevision);
	LocalPlayer->PlayerController = GameController;
	Settings->RefreshInputSettingsContext();
	UClass* PageClass = LoadClass<USettingInputPageWidget>(nullptr, TEXT("/Game/Blueprints/Widget/Outlier/Setting/WBP_SettingInputPageWidget.WBP_SettingInputPageWidget_C"));
	UClass* RowClass = LoadClass<UInputBindingRowWidget>(nullptr, TEXT("/Game/Blueprints/Widget/Outlier/Setting/WBP_SettingInputBindWidget.WBP_SettingInputBindWidget_C"));
	if (!TestNotNull(TEXT("Actual input page class"), PageClass) || !TestNotNull(TEXT("Actual binding row class"), RowClass)) return false;
	USettingInputPageWidget* Page = CreateWidget<USettingInputPageWidget>(World, PageClass);
	if (!TestNotNull(TEXT("Input page fixture"), Page)) return false;
	FInputSettingsPresentationTestAccess::CheckPage(*this, Page, World, RowClass, Settings);
	return true;
}

#endif
