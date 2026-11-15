#if WITH_DEV_AUTOMATION_TESTS

#include "FirstPerson/FirstPersonPlayerController.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "OutlierGameMode.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPresentationProtectionPairRestoreTest,
	"Outlier.PresentationProtection.PairRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPresentationProtectionPairRestoreTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World exists"), World)) return false;
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	AFirstPersonPlayerController* First = World->SpawnActor<AFirstPersonPlayerController>();
	AFirstPersonPlayerController* Second = World->SpawnActor<AFirstPersonPlayerController>();
	AFirstPersonCharacter* FirstPawn = World->SpawnActor<AFirstPersonCharacter>();
	AFirstPersonCharacter* SecondPawn = World->SpawnActor<AFirstPersonCharacter>();
	if (!First || !Second || !FirstPawn || !SecondPawn) return false;
	First->Possess(FirstPawn);
	Second->Possess(SecondPawn);
	const FGuid Id = FGuid::NewGuid();
	TestTrue(TEXT("First participant acquires protection"), First->BeginPresentationProtection(Id, FirstPawn, Second));
	TestTrue(TEXT("Second participant acquires protection"), Second->BeginPresentationProtection(Id, SecondPawn, First));
	TestTrue(TEXT("Repeated acquire is idempotent"), First->BeginPresentationProtection(Id, FirstPawn, Second));
	First->AwaitPresentationProtectionRestore(Id, 0.0f);
	Second->AwaitPresentationProtectionRestore(Id, 0.0f);
	First->ServerNotifyPresentationRestored(Id, SecondPawn);
	TestTrue(TEXT("Wrong pawn cannot finish the first participant"), FirstPawn->IsPresentationProtected());
	First->ServerNotifyPresentationRestored(FGuid::NewGuid(), FirstPawn);
	TestTrue(TEXT("Wrong execution cannot finish protection"), FirstPawn->IsPresentationProtected());
	First->ServerNotifyPresentationRestored(Id, FirstPawn);
	TestTrue(TEXT("First screen waits for its peer"), FirstPawn->IsPresentationProtected());
	TestTrue(TEXT("Peer remains protected"), SecondPawn->IsPresentationProtected());
	Second->ServerNotifyPresentationRestored(Id, SecondPawn);
	TestFalse(TEXT("Both restored screens release the first participant"), FirstPawn->IsPresentationProtected());
	TestFalse(TEXT("Both restored screens release the second participant"), SecondPawn->IsPresentationProtected());
	First->ServerNotifyPresentationRestored(Id, FirstPawn);
	TestFalse(TEXT("Late acknowledgement cannot recreate protection"), FirstPawn->IsPresentationProtected());

	const FGuid MinimumId = FGuid::NewGuid();
	First->BeginPresentationProtection(MinimumId, FirstPawn, Second);
	Second->BeginPresentationProtection(MinimumId, SecondPawn, First);
	First->AwaitPresentationProtectionRestore(MinimumId, 5.0f);
	Second->AwaitPresentationProtectionRestore(MinimumId, 5.0f);
	First->ServerNotifyPresentationRestored(MinimumId, FirstPawn);
	Second->ServerNotifyPresentationRestored(MinimumId, SecondPawn);
	TestTrue(TEXT("Early ACK cannot bypass minimum screen duration"), FirstPawn->IsPresentationProtected());
	First->PresentationProtectionRequests.FindChecked(MinimumId).MinimumFinishTime = 0.0;
	Second->PresentationProtectionRequests.FindChecked(MinimumId).MinimumFinishTime = 0.0;
	First->TryFinishPresentationProtection(MinimumId);
	TestFalse(TEXT("Already received ACK finishes once minimum duration has elapsed"), SecondPawn->IsPresentationProtected());

	const FGuid CancelId = FGuid::NewGuid();
	First->BeginPresentationProtection(CancelId, FirstPawn, Second);
	Second->BeginPresentationProtection(CancelId, SecondPawn, First);
	First->AwaitPresentationProtectionRestore(CancelId, 0.0f);
	Second->AwaitPresentationProtectionRestore(CancelId, 0.0f);
	Second->ServerNotifyPresentationRestored(CancelId, SecondPawn);
	First->RecoverPresentationProtection(CancelId, 0.0f);
	TestFalse(TEXT("Recovering execution cannot be reused for a fresh begin"),
		First->BeginPresentationProtection(CancelId, FirstPawn, Second));
	First->ServerNotifyPresentationRestored(CancelId, FirstPawn);
	TestTrue(TEXT("Late normal ACK cannot finish cancellation cleanup"), FirstPawn->IsPresentationProtected());
	First->ServerNotifyPresentationRestored(CancelId, FirstPawn, true);
	TestTrue(TEXT("Recovery ACK cannot combine with peer's old normal ACK"), FirstPawn->IsPresentationProtected());
	Second->RecoverPresentationProtection(CancelId, 0.0f);
	Second->ServerNotifyPresentationRestored(CancelId, SecondPawn, true);
	TestFalse(TEXT("Both cleanup acknowledgements release protection"), FirstPawn->IsPresentationProtected());

	const FGuid PawnChangeId = FGuid::NewGuid();
	First->BeginPresentationProtection(PawnChangeId, FirstPawn, Second);
	Second->BeginPresentationProtection(PawnChangeId, SecondPawn, First);
	First->UnPossess();
	TestFalse(TEXT("Old pawn does not retain protection after unpossess"), FirstPawn->IsPresentationProtected());
	TestTrue(TEXT("Remaining peer stays protected during its cleanup"), SecondPawn->IsPresentationProtected());
	First->ServerNotifyPresentationRestored(PawnChangeId, FirstPawn, true);
	TestTrue(TEXT("Old pawn ACK cannot release surviving peer"), SecondPawn->IsPresentationProtected());
	Second->PresentationProtectionRequests.FindChecked(PawnChangeId).MinimumFinishTime = 0.0;
	Second->ServerNotifyPresentationRestored(PawnChangeId, SecondPawn, true);
	TestFalse(TEXT("Surviving peer finishes its own cleanup"), SecondPawn->IsPresentationProtected());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPresentationProtectionIntroPossessionTest,
	"Outlier.PresentationProtection.IntroWaitsForPossession",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPresentationProtectionIntroPossessionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World exists"), World)) return false;
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	UClass* ModeClass = LoadClass<AOutlierGameMode>(nullptr,
		TEXT("/Game/Blueprints/Outlier/BP_OutlierGM.BP_OutlierGM_C"));
	AOutlierGameMode* Mode = ModeClass ? World->SpawnActor<AOutlierGameMode>(ModeClass) : nullptr;
	AFirstPersonPlayerController* First = World->SpawnActor<AFirstPersonPlayerController>();
	AFirstPersonPlayerController* Second = World->SpawnActor<AFirstPersonPlayerController>();
	AFirstPersonCharacter* FirstPawn = World->SpawnActor<AFirstPersonCharacter>();
	AFirstPersonCharacter* SecondPawn = World->SpawnActor<AFirstPersonCharacter>();
	AOutlierPlayerState* FirstState = World->SpawnActor<AOutlierPlayerState>();
	AOutlierPlayerState* SecondState = World->SpawnActor<AOutlierPlayerState>();
	if (!TestNotNull(TEXT("Production GameMode exists"), Mode)
		|| !First || !Second || !FirstPawn || !SecondPawn || !FirstState || !SecondState) return false;
	First->PlayerState = FirstState;
	Second->PlayerState = SecondState;
	FirstState->SetPairId(0);
	SecondState->SetPairId(0);
	FirstState->SetPlayerRole(EOutlierPlayerRole::Shooter);
	SecondState->SetPlayerRole(EOutlierPlayerRole::Partner);
	Mode->ActiveMatchPairId = 0;
	Mode->bPendingInitialIntro = true;
	Mode->InitialIntroId = FGuid::NewGuid();
	Mode->InitialIntroShooter = First;
	Mode->InitialIntroPartner = Second;

	TestFalse(TEXT("Before possession protection waits"), Mode->TryAcquireInitialIntroProtection());
	TestFalse(TEXT("Missing pawn is not a preparation failure"), Mode->bInitialIntroFailed);
	First->Possess(FirstPawn);
	Mode->PendingPossessions.Add(Second, SecondPawn);
	TestFalse(TEXT("Listen host waits for guest level-ready possession"), Mode->TryAcquireInitialIntroProtection());
	TestFalse(TEXT("Waiting for guest does not fail the intro"), Mode->bInitialIntroFailed);
	TestFalse(TEXT("Protection does not start on only one participant"), FirstPawn->IsPresentationProtected());
	Second->Possess(SecondPawn);
	TestFalse(TEXT("Pending possession dispatch still blocks preparation"), Mode->TryAcquireInitialIntroProtection());
	Mode->PendingPossessions.Remove(Second);
	TestTrue(TEXT("Both possessed participants acquire protection"), Mode->TryAcquireInitialIntroProtection());
	TestTrue(TEXT("Shooter protection precedes sequence playback"), FirstPawn->IsPresentationProtected());
	TestTrue(TEXT("Partner protection precedes sequence playback"), SecondPawn->IsPresentationProtected());
	TestTrue(TEXT("Repeated readiness is idempotent"), Mode->TryAcquireInitialIntroProtection());
	TestFalse(TEXT("Protected intro has not failed"), Mode->bInitialIntroFailed);
	First->ReleasePresentationProtection(Mode->InitialIntroId);
	Second->ReleasePresentationProtection(Mode->InitialIntroId);
	return true;
}

#endif
