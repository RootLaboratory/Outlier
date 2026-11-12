#if WITH_DEV_AUTOMATION_TESTS

#include "OutlierGameInstance.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierReconnectWatchdogTest,
	"Outlier.Network.Reconnect.TravelWatchdog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierReconnectWatchdogTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UOutlierGameInstance* Instance = NewObject<UOutlierGameInstance>();
	Instance->LastListenReconnectUrl = TEXT("127.0.0.1:7777?ListenReconnect=Test");
	Instance->ScheduleArenaReconnect();
	const double FirstDeadline = Instance->ArenaReconnectDeadlineSeconds;
	const double FirstAttempt = Instance->ArenaReconnectNextAttemptSeconds;
	TestTrue(TEXT("Initial connection loss starts an absolute thirty-second budget"),
		FirstDeadline > FPlatformTime::Seconds() + 29.0);
	TestTrue(TEXT("Initial reconnect waits the configured two-second interval"),
		FirstAttempt > FPlatformTime::Seconds() + 1.0);
	Instance->ScheduleArenaReconnect();
	TestEqual(TEXT("Duplicate failures do not extend the deadline"), Instance->ArenaReconnectDeadlineSeconds, FirstDeadline);
	TestEqual(TEXT("Duplicate failures do not postpone the next attempt"), Instance->ArenaReconnectNextAttemptSeconds, FirstAttempt);

	// Travel은 실제 NetDriver가 맡는다. 성공/실패 콜백이 오지 않는 동안에도 같은 감시가 살아 있어야 한다.
	Instance->bArenaReconnectTravelInFlight = true;
	Instance->ArenaReconnectNextAttemptSeconds = 0.0;
	TestTrue(TEXT("A pending Travel retains the real-time watchdog"), Instance->HandleArenaReconnectTick(100.0f));
	TestTrue(TEXT("Elapsed world delta alone does not replace an in-flight Travel"), Instance->bArenaReconnectTravelInFlight);
	TestEqual(TEXT("An in-flight Travel preserves the first deadline"), Instance->ArenaReconnectDeadlineSeconds, FirstDeadline);
	Instance->HandleTravelFailure(nullptr, ETravelFailure::ClientTravelFailure, TEXT("Injected travel failure"));
	TestFalse(TEXT("Travel failure permits a later attempt"), Instance->bArenaReconnectTravelInFlight);
	TestTrue(TEXT("A failed Travel waits before the next attempt"), Instance->ArenaReconnectNextAttemptSeconds > FPlatformTime::Seconds() + 1.0);
	TestEqual(TEXT("Travel failure keeps the original deadline"), Instance->ArenaReconnectDeadlineSeconds, FirstDeadline);

	// 실패 이벤트 없이 멈춘 Travel도 전체 제한이 먼저 끝나면 로컬 종료된다.
	Instance->bArenaReconnectTravelInFlight = true;
	Instance->ArenaReconnectDeadlineSeconds = FPlatformTime::Seconds() - 1.0;
	TestFalse(TEXT("Hung Travel expires without requiring another network failure"), Instance->HandleArenaReconnectTick(0.0f));
	TestFalse(TEXT("Expiry ends reconnect"), Instance->bArenaReconnectActive);
	TestFalse(TEXT("Expiry cancels the persistent ticker"), Instance->ArenaReconnectTickerHandle.IsValid());
	TestTrue(TEXT("Expiry discards the previous match route"), Instance->LastListenReconnectUrl.IsEmpty());
	Instance->ScheduleArenaReconnect();
	TestFalse(TEXT("Late callbacks cannot restart the terminated match"), Instance->bArenaReconnectActive);

	Instance->NotifyArenaHandoffStarted(TEXT("127.0.0.1:7780?MatchId=Next"));
	Instance->ScheduleArenaReconnect();
	TestTrue(TEXT("A new handoff can start a new reconnect lifetime"), Instance->bArenaReconnectActive);
	Instance->SetArenaRecoveryFailureReason(TEXT("Injected recovery failure"));
	Instance->PrepareForExplicitLeave();
	TestEqual(TEXT("Failure reason survives explicit cleanup until the return screen"),
		Instance->ArenaRecoveryFailureReason, FString(TEXT("Injected recovery failure")));
	TestFalse(TEXT("Explicit leave clears in-flight state"), Instance->bArenaReconnectTravelInFlight);
	TestFalse(TEXT("Explicit leave clears the watchdog"), Instance->ArenaReconnectTickerHandle.IsValid());
	TestTrue(TEXT("Explicit leave clears the old worker route"), Instance->LastArenaHandoffUrl.IsEmpty());
	return true;
}

#endif
