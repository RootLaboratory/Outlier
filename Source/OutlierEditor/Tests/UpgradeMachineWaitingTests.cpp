#include "UpgradeMachineTestWidget.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Misc/AutomationTest.h"

struct FUpgradeMachineTestAccessor
{
	static void Prepare(UUpgradeMachineTestWidget* Widget)
	{
		Widget->Button = NewObject<UButton>(Widget);
		Widget->TextBlock_76 = NewObject<UTextBlock>(Widget);
		Widget->bUIOpenedReported = true;
		Widget->OpenedGameplayGeneration = 42;
		Widget->SessionSyncRequestId = 100;
	}
	static void Apply(UUpgradeMachineTestWidget* Widget, uint32 Session, bool bPending, uint32 Sync = 0)
	{
		Widget->HandleSessionChanged(Session, 42, bPending, false, Sync);
	}
	static void SetInFlight(UUpgradeMachineTestWidget* Widget) { Widget->bExitRequestInFlight = true; }
	static bool Confirm(UUpgradeMachineTestWidget* Widget) { return Widget->HandleUILayerConfirmed_Implementation(); }
	static bool ButtonEnabled(UUpgradeMachineTestWidget* Widget) { return Widget->Button->GetIsEnabled(); }
	static FText Message(UUpgradeMachineTestWidget* Widget) { return Widget->TextBlock_76->GetText(); }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUpgradeMachineWaitingTest,
	"Outlier.UI.StatAllocator.WaitingWidget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUpgradeMachineWaitingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UUpgradeMachineTestWidget* Widget = NewObject<UUpgradeMachineTestWidget>();
	FUpgradeMachineTestAccessor::Prepare(Widget);
	FUpgradeMachineTestAccessor::Apply(Widget, 7, true, 99);
	TestFalse(TEXT("Old open response cannot bind the widget"), Widget->IsWaitingForPartner());
	FUpgradeMachineTestAccessor::Apply(Widget, 7, true, 100);
	TestTrue(TEXT("Server readiness shows waiting"), Widget->IsWaitingForPartner());
	TestTrue(TEXT("Waiting can be cancelled"), Widget->CanCancelExit());
	TestFalse(TEXT("Waiting disables upgrade button"), FUpgradeMachineTestAccessor::ButtonEnabled(Widget));
	// 입력 매핑이 없는 fixture에서는 고정 Esc 대신 미지정 취소 안내를 표시한다.
	TestTrue(TEXT("Waiting displays the unmapped cancel hint"), FUpgradeMachineTestAccessor::Message(Widget).ToString().Contains(TEXT("미지정: 취소")));
	FUpgradeMachineTestAccessor::Apply(Widget, 7, false);
	TestFalse(TEXT("Accepted cancel leaves waiting"), Widget->IsWaitingForPartner());
	TestTrue(TEXT("Accepted cancel restores upgrade button"), FUpgradeMachineTestAccessor::ButtonEnabled(Widget));
	FUpgradeMachineTestAccessor::SetInFlight(Widget);
	TestFalse(TEXT("Enter cannot open upgrades during an exit request"), FUpgradeMachineTestAccessor::Confirm(Widget));
	FUpgradeMachineTestAccessor::Apply(Widget, 8, true);
	TestFalse(TEXT("Other session cannot change the current widget"), Widget->IsWaitingForPartner());
	return true;
}
#endif