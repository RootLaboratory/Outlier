#if WITH_DEV_AUTOMATION_TESTS

#include "FirstPerson/FirstPersonPlayerController.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "OutlierArenaSettings.h"
#include "Network/OutlierArenaSubsystem.h"
#include "Engine/LevelStreamingDynamic.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierClientReloadRecoveryTest,
	"Outlier.Network.SingleArena.ClientRecoveryLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierClientReloadRecoveryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Client recovery test world"), World)) return false;
	AFirstPersonPlayerController* Controller = World->SpawnActor<AFirstPersonPlayerController>();
	if (!TestNotNull(TEXT("Client recovery controller"), Controller))
	{
		World->DestroyWorld(false);
		return false;
	}

	UOutlierArenaSubsystem* Arena = World->GetSubsystem<UOutlierArenaSubsystem>();
	if (!TestNotNull(TEXT("Reload subsystem exists"), Arena)) return false;
	ULevelStreamingDynamic* Base = NewObject<ULevelStreamingDynamic>(World);
	FObjectPropertyBase* Loaded = FindFProperty<FObjectPropertyBase>(ULevelStreaming::StaticClass(), TEXT("LoadedLevel"));
	Loaded->SetObjectPropertyValue_InContainer(Base, World->PersistentLevel);
	Arena->Arena.StreamingLevel = Base;
	Arena->GameplaySublevels = { TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Tests/ClientReload.ClientReload"))) };
	Arena->GameplayStreamingLevels = { NewObject<ULevelStreamingDynamic>(World) };
	Arena->bGameplayLevelsResolved = true;
	TestTrue(TEXT("Unshown sublevels can participate in reload bootstrap"), Arena->IsGameplayReloadBootstrapReady());
	TestFalse(TEXT("Bootstrap readiness does not claim gameplay readiness"), Arena->IsGameplayLevelsReady());
	Arena->GameplayGeneration = 5;
	Arena->PendingGameplayReload.Emplace();
	Arena->PendingGameplayReload->Generation = 5;
	Arena->PendingGameplayReload->Phase = EOutlierGameplayReloadPhase::WaitingForUnload;
	Controller->PendingGameplayGeneration = 5;
	Controller->ContinueClientGameplayReload();
	TestTrue(TEXT("Replacement controller listens to an already active unload"), Arena->OnArenaGameplayUnloaded.IsBoundToObject(Controller));
	TestTrue(TEXT("Replacement controller listens to the same generation ready"), Arena->OnArenaGameplayReady.IsBoundToObject(Controller));
	Arena->PendingGameplayReload.Reset();

	// 이미 준비 ACK를 보낸 클라이언트에 같은 세대 요청이 다시 오는 실제 RPC 진입점을 검사한다.
	Controller->PendingGameplayGeneration = 5;
	Controller->bClientArenaReadyReported = true;
	const double StartedAt = FPlatformTime::Seconds() - 1.0;
	Controller->ClientGameplayReloadStartedAt = StartedAt;
	Controller->ClientArenaGameplayReload_Implementation(5, FVector(1, 2, 3));
	TestEqual(TEXT("Duplicate reload does not extend the deadline"),
		Controller->ClientGameplayReloadStartedAt, StartedAt);
	TestTrue(TEXT("Duplicate request preserves the completed local ready check"), Controller->bClientArenaReadyReported);
	Controller->ClientArenaGameplayReload_Implementation(4, FVector::ZeroVector);
	TestEqual(TEXT("Old generation cannot replace current participation"), Controller->PendingGameplayGeneration, 5u);

	// Tick의 DeltaTime이 0이어도 실제 경과 시간은 만료된다. 콘텐츠 로딩/Ready ACK 이후 모두 같은 감시를 쓴다.
	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	Controller->ClientGameplayReloadStartedAt = FPlatformTime::Seconds()
		- FMath::Max(Settings->ArenaGameplayReloadFailureSeconds, 1.0f) - 1.0;
	Controller->bHasPendingArenaRequest = true;
	Controller->bClientReloadBootstrapPending = true;
	Controller->bClientGameplayLoadAuthorized = true;
	AddExpectedError(TEXT("Gameplay reload exceeded the total recovery deadline."), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Paused/dilated wait still expires"), Controller->TickClientGameplayReloadWatchdog(0.0f));
	TestTrue(TEXT("Expiry reaches terminal recovery"), Controller->bClientGameplayReloadFailed);
	TestFalse(TEXT("Terminal recovery clears bootstrap continuation"), Controller->bClientReloadBootstrapPending);
	TestFalse(TEXT("Terminal recovery clears activation authorization"), Controller->bClientGameplayLoadAuthorized);
	TestFalse(TEXT("Terminal recovery clears pending content wait"), Controller->bHasPendingArenaRequest);
	TestFalse(TEXT("Terminal recovery clears watchdog"), Controller->ClientGameplayReloadWatchdogHandle.IsValid());
	Controller->ClientRetryArenaGameplayReload_Implementation(5);
	TestFalse(TEXT("Late retry cannot restart a failed operation"), Controller->bHasPendingArenaRequest);

	World->DestroyWorld(false);
	return true;
}

#endif
