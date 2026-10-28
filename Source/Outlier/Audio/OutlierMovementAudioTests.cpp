#if WITH_DEV_AUTOMATION_TESTS

#include "Audio/OutlierAudioSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Shooter/ShooterCharacterMovementComponent.h"
#include "UObject/CoreNet.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierMovementAudioPredictionTest,
	"Outlier.Audio.Movement.Prediction", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierMovementAudioPredictionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Movement audio test world"), World)) return false;
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
	};
	APawn* Pawn = World->SpawnActor<APawn>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("Movement audio pawn"), Pawn) || !TestNotNull(TEXT("Local controller"), Controller)) return false;
	Controller->Possess(Pawn);
	if (!TestTrue(TEXT("Fixture pawn has a local listener"), Pawn->IsLocallyControlled())) return false;
	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>());
	TStrongObjectPtr<UOutlierAudioSubsystem> Audio(NewObject<UOutlierAudioSubsystem>(GameInstance.Get()));

	const UOutlierAudioSubsystem::FMovementAudioKey Jump { Pawn, 20 };
	const uint64 LocalToken = Audio->BeginMovementAudio(Jump, false).Token;
	TestTrue(TEXT("Local request starts pending"), Audio->IsMovementPlaybackCurrent(Pawn, Jump.EventId, LocalToken));
	Audio->FinishMovementPlayback(Pawn, Jump.EventId, LocalToken, true);
	FOutlierResolvedAudioPlay Server;
	Server.MovementAudioEmitter = Pawn;
	Server.MovementAudioEventId = Jump.EventId;
	TestFalse(TEXT("Server duplicate after successful local playback is suppressed"), Audio->PrepareDeliveredMovementAudio(Server));
	TestTrue(TEXT("Repeated jump replay uses the same retained event record"), Audio->MovementAudioRecords.Contains(Jump));

	Server.MovementAudioEventId = 22;
	const uint64 PendingLocalToken = Audio->BeginMovementAudio({ Pawn, 22 }, false).Token;
	TestTrue(TEXT("Server can replace a still-loading local request"), Audio->PrepareDeliveredMovementAudio(Server));
	TestTrue(TEXT("Server receives a new playback token"), Server.MovementPlaybackToken != PendingLocalToken);
	TestFalse(TEXT("Late local callback is invalidated"), Audio->IsMovementPlaybackCurrent(Pawn, 22, PendingLocalToken));
	TestTrue(TEXT("Current server callback is accepted"), Audio->IsMovementPlaybackCurrent(Pawn, 22, Server.MovementPlaybackToken));
	FOutlierResolvedAudioPlay ServerDuplicate = Server;
	ServerDuplicate.MovementPlaybackToken = 0;
	TestFalse(TEXT("Repeated server delivery cannot queue another playback"), Audio->PrepareDeliveredMovementAudio(ServerDuplicate));
	Audio->FinishMovementPlayback(Pawn, 22, PendingLocalToken, true);
	TestTrue(TEXT("Stale completion cannot mark the server request played"),
		Audio->IsMovementPlaybackCurrent(Pawn, 22, Server.MovementPlaybackToken));
	Audio->FinishMovementPlayback(Pawn, 22, Server.MovementPlaybackToken, true);
	TestFalse(TEXT("A callback cannot play the same request twice"),
		Audio->IsMovementPlaybackCurrent(Pawn, 22, Server.MovementPlaybackToken));

	const uint64 FailedLocalToken = Audio->BeginMovementAudio({ Pawn, 24 }, false).Token;
	Audio->FinishMovementPlayback(Pawn, 24, FailedLocalToken, false);
	Server = {};
	Server.MovementAudioEmitter = Pawn;
	Server.MovementAudioEventId = 24;
	TestTrue(TEXT("Failed local spawn permits server fallback"), Audio->PrepareDeliveredMovementAudio(Server));
	FOutlierAudioPlayRequest PendingRequest;
	PendingRequest.EmitterActor = Pawn;
	PendingRequest.MovementAudioEventId = 24;
	Audio->PendingPlaysByType.FindOrAdd({}).Emplace(PendingRequest, FOutlierAudioExecutionPolicy{});
	Audio->PendingPlaysByType.FindOrAdd({}).Emplace(FOutlierAudioPlayRequest{}, FOutlierAudioExecutionPolicy{});
	Audio->PendingResolvedPlaysByType.FindOrAdd({}).Emplace(Server, World);
	UOutlierAudioSubsystem::FPendingPlay PendingSound;
	PendingSound.MovementAudioEmitter = Pawn;
	PendingSound.MovementAudioEventId = 24;
	Audio->PendingPlaysBySound.FindOrAdd({}).Add(PendingSound);
	Audio->ClearPredictedMovementAudio(Pawn);
	TestFalse(TEXT("Pawn cleanup invalidates outstanding callbacks"),
		Audio->IsMovementPlaybackCurrent(Pawn, 24, Server.MovementPlaybackToken));
	TestTrue(TEXT("Pawn cleanup drops retained event keys"), Audio->MovementAudioRecords.IsEmpty());
	TestEqual(TEXT("Pawn cleanup preserves unrelated pending sounds"), Audio->PendingPlaysByType.FindChecked({}).Num(), 1);
	TestTrue(TEXT("Pawn cleanup removes resolved bank waits"), Audio->PendingResolvedPlaysByType.FindChecked({}).IsEmpty());
	TestTrue(TEXT("Pawn cleanup removes pending sound playback"), Audio->PendingPlaysBySound.FindChecked({}).IsEmpty());

	for (uint64 Event = 1; Event <= 160; ++Event) Audio->BeginMovementAudio({ Pawn, Event }, false);
	TestEqual(TEXT("Movement event history is bounded"), Audio->MovementAudioRecords.Num(), 128);
	TestFalse(TEXT("Evicted requests cannot revive through a delayed callback"), Audio->MovementAudioRecords.Contains({ Pawn, 1 }));
	Audio->ClearPredictedMovementAudio(nullptr);

	TStrongObjectPtr<UShooterCharacterMovementComponent> Movement(NewObject<UShooterCharacterMovementComponent>());
	Movement->AudioMoveId = 100;
	Movement->AudioAirborneId = 50;
	TestEqual(TEXT("Jump uses the saved move identifier"), Movement->GetMovementAudioEventId(false), uint64(200));
	const uint64 Landing = Movement->GetMovementAudioEventId(true);
	Movement->AudioMoveId = 101;
	TestEqual(TEXT("Landing key is independent of its simulation frame"), Movement->GetMovementAudioEventId(true), Landing);
	Movement->AudioMoveId = Movement->AudioAirborneId;
	TestTrue(TEXT("Jump and landing have separate action identity"), Landing != Movement->GetMovementAudioEventId(false));
	Movement->AudioAirborneId = 101;
	TestTrue(TEXT("A later airborne episode produces a new landing key"), Movement->GetMovementAudioEventId(true) != Landing);
	FShooterAudioMoveData Sent;
	Sent.AudioMoveId = 123456;
	Sent.AudioAirborneId = 120000;
	FNetBitWriter Writer(nullptr, 2048);
	if (TestTrue(TEXT("Packed movement serializes cosmetic identifiers"),
		Sent.Serialize(*Movement, Writer, nullptr, FCharacterNetworkMoveData::ENetworkMoveType::NewMove)))
	{
		FNetBitReader Reader(nullptr, Writer.GetData(), Writer.GetNumBits());
		FShooterAudioMoveData Received;
		TestTrue(TEXT("Packed movement reads cosmetic identifiers"),
			Received.Serialize(*Movement, Reader, nullptr, FCharacterNetworkMoveData::ENetworkMoveType::NewMove));
		TestEqual(TEXT("Jump move key survives transport"), Received.AudioMoveId, Sent.AudioMoveId);
		TestEqual(TEXT("Airborne key survives transport"), Received.AudioAirborneId, Sent.AudioAirborneId);
	}
	return true;
}

#endif
