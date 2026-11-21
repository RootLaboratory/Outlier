#if WITH_DEV_AUTOMATION_TESTS

#include "Level/Elevator/ElevatorShaftController.h"
#include "Level/Elevator/ElevatorShaftSection.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "Components/AudioComponent.h"
#include "Components/ChildActorComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Interaction/InteractableDoor.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElevatorPresentationLifecycleTest,
	"Outlier.Elevator.Presentation.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElevatorPresentationLifecycleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("ElevatorPresentationTest")));
	if (!TestNotNull(TEXT("Presentation world exists"), World)) return false;
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};

	const FTransform EntrancePlacement(FRotator(0, 90, 0), FVector(5000, 1000, 200));
	const FTransform ArrivalPlacement(FRotator(0, 90, 0), FVector(7000, 1500, -2000));
	const FTransform ServerEntrance(FRotator(0, 90, 0), FVector(5100, 1000, 200));
	AElevatorShaftSection* DuplicateDoorSection = World->SpawnActor<AElevatorShaftSection>();
	AInteractableDoor* ReplicatedDoor = World->SpawnActor<AInteractableDoor>();
	if (!TestNotNull(TEXT("Arrival section exists"), DuplicateDoorSection)
		|| !TestNotNull(TEXT("Replicated door exists"), ReplicatedDoor))
	{
		return false;
	}
	UChildActorComponent* ChildDoorComponent = NewObject<UChildActorComponent>(DuplicateDoorSection);
	DuplicateDoorSection->AddInstanceComponent(ChildDoorComponent);
	ChildDoorComponent->SetupAttachment(DuplicateDoorSection->GetRootComponent());
	ChildDoorComponent->SetChildActorClass(AInteractableDoor::StaticClass());
	ChildDoorComponent->RegisterComponent();
	AActor* LocalDoor = ChildDoorComponent->GetChildActor();
	if (!TestNotNull(TEXT("Locally constructed arrival door exists"), LocalDoor))
	{
		return false;
	}
	DuplicateDoorSection->RefreshChildActors();
	TestFalse(TEXT("Local door is visible before the replicated reference is resolved"), LocalDoor->IsHidden());
	TestTrue(TEXT("Local door has collision before the replicated reference is resolved"), LocalDoor->GetActorEnableCollision());
	DuplicateDoorSection->SetReplicatedArrivalDoor(ReplicatedDoor);
	DuplicateDoorSection->RefreshChildActors();
	DuplicateDoorSection->SetSectionVisible(false);
	DuplicateDoorSection->SetSectionVisible(true);
	TestTrue(TEXT("Showing the section again keeps the duplicate door hidden"), LocalDoor->IsHidden());
	TestFalse(TEXT("The duplicate stops blocking the local pawn"), LocalDoor->GetActorEnableCollision());
	TestFalse(TEXT("The replicated door stays visible"), ReplicatedDoor->IsHidden());
	TestTrue(TEXT("The replicated door still owns passage collision"), ReplicatedDoor->GetActorEnableCollision());

	for (const bool bMotionArrivesFirst : {false, true})
	{
		AElevatorShaftController* StreamingController = World->SpawnActor<AElevatorShaftController>();
		if (!TestNotNull(TEXT("Streaming controller exists"), StreamingController))
		{
			return false;
		}
		StreamingController->EntranceSection = World->SpawnActor<AElevatorShaftSection>();
		StreamingController->ArrivalSection = World->SpawnActor<AElevatorShaftSection>();
		if (!TestNotNull(TEXT("Streaming entrance exists"), StreamingController->EntranceSection.Get())
			|| !TestNotNull(TEXT("Streaming arrival exists"), StreamingController->ArrivalSection.Get()))
		{
			return false;
		}
		StreamingController->EntranceSection->SetActorTransform(EntrancePlacement);
		StreamingController->ArrivalSection->SetActorTransform(ArrivalPlacement);
		StreamingController->LoopSectionClass = AElevatorShaftSection::StaticClass();

		// 복제와 로컬 참조 준비 중 하나만 끝난 시점의 호출을 각각 재현한다.
		FElevatorShaftMotion InitialMotion;
		InitialMotion.ExecutionId = FGuid::NewGuid();
		InitialMotion.EntranceTransform = ServerEntrance;
		InitialMotion.ArrivalTransform = ArrivalPlacement;
		if (bMotionArrivesFirst)
		{
			StreamingController->Motion = InitialMotion;
		}
		else
		{
			StreamingController->bInitialized = true;
		}
		StreamingController->OnRep_Motion();
		StreamingController->UpdateVisuals();
		StreamingController->InitializeVisualPool();
		TestTrue(TEXT("Before readiness the entrance stays at its authored transform"),
			StreamingController->EntranceSection->GetActorTransform().Equals(EntrancePlacement));
		TestTrue(TEXT("Before readiness the arrival stays at its authored transform"),
			StreamingController->ArrivalSection->GetActorTransform().Equals(ArrivalPlacement));
		TestFalse(TEXT("Before readiness the entrance remains visible"), StreamingController->EntranceSection->IsHidden());
		TestTrue(TEXT("Before readiness no loop sections are spawned"), StreamingController->LoopSections.IsEmpty());
		TestFalse(TEXT("Before readiness the incoming execution is not presented"),
			StreamingController->PresentationExecutionId.IsValid());

		if (!bMotionArrivesFirst)
		{
			StreamingController->Motion = InitialMotion;
		}
		StreamingController->bInitialized = true;
		StreamingController->OnRep_Motion();
		TestTrue(TEXT("Once ready the entrance applies the authoritative transform"),
			StreamingController->EntranceSection->GetActorTransform().Equals(ServerEntrance));
		TestEqual(TEXT("Once ready the visual pool is created"),
			StreamingController->LoopSections.Num(), StreamingController->LoopSectionCount);
		StreamingController->OnRep_Motion();
		TestEqual(TEXT("Reapplying motion does not duplicate loop sections"),
			StreamingController->LoopSections.Num(), StreamingController->LoopSectionCount);
	}

	// 먼저 언로드되는 Controller가 아직 로드된 별도 도착층의 배치를 덮어쓰지 않아야 한다.
	AElevatorShaftController* UnloadingController = World->SpawnActor<AElevatorShaftController>();
	AElevatorShaftSection* RemainingArrival = World->SpawnActor<AElevatorShaftSection>();
	if (!TestNotNull(TEXT("Unloading controller exists"), UnloadingController)
		|| !TestNotNull(TEXT("Remaining arrival exists"), RemainingArrival))
	{
		return false;
	}
	RemainingArrival->SetActorTransform(ArrivalPlacement);
	UnloadingController->bInitialized = true;
	UnloadingController->ArrivalSection = RemainingArrival;
	UnloadingController->EndPlay(EEndPlayReason::RemovedFromWorld);
	TestTrue(TEXT("Unloading before replication preserves the authored arrival transform"),
		RemainingArrival->GetActorTransform().Equals(ArrivalPlacement));

	AElevatorShaftController* Controller = World->SpawnActor<AElevatorShaftController>();
	if (!TestNotNull(TEXT("Controller exists"), Controller)) return false;
	Controller->bInitialized = true;
	Controller->bProfilesReady = true;
	Controller->Motion.ExecutionId = FGuid::NewGuid();
	Controller->PresentationExecutionId = Controller->Motion.ExecutionId;
	Controller->Motion.bStarted = true;
	Controller->Motion.State = EElevatorShaftState::RisingCombat;
	Controller->Motion.StartServerTime = Controller->ServerTime() - 0.5;
	Controller->UpdateMotionAudio();
	TestEqual(TEXT("Acceleration is presented once"), Controller->PresentedSegments, static_cast<uint8>(2));
	Controller->UpdateMotionAudio();
	TestEqual(TEXT("Repeated update does not reissue acceleration"), Controller->PresentedSegments, static_cast<uint8>(2));

	UAudioComponent* Audio = NewObject<UAudioComponent>(Controller);
	Controller->AddInstanceComponent(Audio);
	Audio->RegisterComponent();
	Controller->RegisterMotionAudioComponent(Audio);
	Controller->RegisterMotionAudioComponent(Audio);
	TestEqual(TEXT("An execution tracks each owned audio component once"), Controller->MotionAudioComponents.Num(), 1);
	Controller->Motion.StartServerTime = Controller->ServerTime() - 10.0;
	Controller->UpdateMotionAudio();
	TestEqual(TEXT("Cruise joins the presented acceleration segment"), Controller->PresentedSegments, static_cast<uint8>(6));
	Controller->Motion.State = EElevatorShaftState::FinishingCurrentSection;
	Controller->UpdateMotionAudio();
	Controller->Motion.State = EElevatorShaftState::TransitionOut;
	Controller->UpdateMotionAudio();
	TestEqual(TEXT("Finishing a floor and fading do not restart cruise"), Controller->PresentedSegments, static_cast<uint8>(6));

	// 도착 좌표계 변경은 동일 운행이다. 암전 준비와 이어지는 등속은 새 시작음을 만들지 않는다.
	Controller->Motion.bArrival = true;
	Controller->Motion.DepartureElapsed = 10.0;
	Controller->Motion.BrakeStartSeconds = 4.0;
	Controller->Motion.ArrivalStartTime = Controller->ServerTime() - 0.5;
	Controller->Motion.State = EElevatorShaftState::PreparingArrival;
	Controller->UpdateMotionAudio();
	Controller->Motion.State = EElevatorShaftState::Arriving;
	Controller->UpdateMotionAudio();
	TestEqual(TEXT("Arrival preparation and resumed cruise preserve segment identity"), Controller->PresentedSegments, static_cast<uint8>(6));
	Controller->Motion.ArrivalStartTime = Controller->ServerTime() - 4.5;
	Controller->UpdateMotionAudio();
	TestEqual(TEXT("Arrival braking is presented once"), Controller->PresentedSegments, static_cast<uint8>(14));
	Controller->UpdateMotionAudio();
	TestEqual(TEXT("Repeated braking update is idempotent"), Controller->PresentedSegments, static_cast<uint8>(14));
	Controller->Motion.bLanded = true;
	Controller->OnRep_Motion();
	TestEqual(TEXT("Landing releases all registered audio"), Controller->MotionAudioComponents.Num(), 0);
	TestEqual(TEXT("Landing clears presented segments"), Controller->PresentedSegments, static_cast<uint8>(0));
	Controller->UpdateMotionAudio();
	TestEqual(TEXT("Landed state cannot restart previous audio"), Controller->PresentedSegments, static_cast<uint8>(0));

	Controller->Motion.bLanded = false;
	Controller->Motion.bArrival = false;
	Controller->Motion.State = EElevatorShaftState::RisingCombat;
	Controller->UpdateMotionAudio();
	TestEqual(TEXT("Late observation marks skipped acceleration without replaying its history"), Controller->PresentedSegments, static_cast<uint8>(6));
	Controller->RegisterMotionAudioComponent(Audio);
	const FGuid PreviousExecution = Controller->Motion.ExecutionId;
	Controller->Motion.ExecutionId = FGuid::NewGuid();
	Controller->Motion.bStarted = false;
	Controller->Motion.State = EElevatorShaftState::WaitingForPair;
	Controller->OnRep_Motion();
	TestEqual(TEXT("New execution stops and forgets old audio components"), Controller->MotionAudioComponents.Num(), 0);
	TestEqual(TEXT("New execution has no historical segment markers"), Controller->PresentedSegments, static_cast<uint8>(0));
	TestTrue(TEXT("Presentation identity follows the current execution"), Controller->PresentationExecutionId == Controller->Motion.ExecutionId);
	Controller->RegisterMotionAudioComponent(Audio);
	TestEqual(TEXT("Inactive execution cannot acquire motion audio"), Controller->MotionAudioComponents.Num(), 0);

	// Section은 Transform을 복제하지 않는다. 서버 초기화와 별개로 로컬 취소 경로도 최종 배치를 복구해야 한다.
	Controller->EntranceSection = World->SpawnActor<AElevatorShaftSection>();
	Controller->ArrivalSection = World->SpawnActor<AElevatorShaftSection>();
	if (!TestNotNull(TEXT("Source section exists"), Controller->EntranceSection.Get())
		|| !TestNotNull(TEXT("Arrival section exists"), Controller->ArrivalSection.Get())) return false;
	Controller->Motion.ArrivalTransform = FTransform(FRotator(0, 90, 0), FVector(5000, 1000, 200));
	FTransform Lowered = Controller->Motion.ArrivalTransform;
	Lowered.AddToTranslation(FVector(0, 0, -1450));
	Controller->ArrivalSection->SetActorTransform(Lowered);
	Controller->ArrivalSection->SetSectionVisible(false);
	Controller->UpdateVisuals();
	TestTrue(TEXT("Cancelled arrival returns the local section to its authored transform"),
		Controller->ArrivalSection->GetActorTransform().Equals(Controller->Motion.ArrivalTransform));
	TestFalse(TEXT("Cancelled arrival restores section visibility"), Controller->ArrivalSection->IsHidden());

	Controller->InputBlockId = FGuid::NewGuid();
	Controller->Motion.State = EElevatorShaftState::PreparingArrival;
	Controller->NotifyClientArrivalReady(nullptr, Controller->Motion.ExecutionId);
	TestFalse(TEXT("Null sender cannot acknowledge arrival preparation"), Controller->bShooterArrivalReady);
	Controller->Motion.State = EElevatorShaftState::TransitionOut;
	Controller->NotifyClientFadeFinished(nullptr, Controller->Motion.ExecutionId, false);
	TestFalse(TEXT("Null sender cannot acknowledge blackout"), Controller->bShooterFadeReady);
	AFirstPersonPlayerController* Player = World->SpawnActor<AFirstPersonPlayerController>();
	if (!TestNotNull(TEXT("Owner transport exists"), Player)) return false;
	Controller->ShooterController = Player;
	Controller->Motion.State = EElevatorShaftState::PreparingArrival;
	Controller->NotifyClientArrivalReady(Player, PreviousExecution);
	TestFalse(TEXT("Old execution cannot acknowledge current arrival"), Controller->bShooterArrivalReady);
	Controller->NotifyClientFadeFinished(Player, Controller->Motion.ExecutionId, false);
	TestFalse(TEXT("Late blackout callback cannot acknowledge the preparation phase"), Controller->bShooterFadeReady);
	Controller->Motion.State = EElevatorShaftState::Arriving;
	Controller->NotifyClientArrivalReady(Player, Controller->Motion.ExecutionId);
	TestFalse(TEXT("Late preparation callback cannot alter an arrival already underway"), Controller->bShooterArrivalReady);
	// Fade In 응답이 먼저 끝났어도 착층 시 페어가 유효하지 않으면 출구를 개방하지 않는다.
	Controller->Motion.bArrival = true;
	Controller->Motion.bLanded = false;
	Controller->bRevealRequested = true;
	Controller->bShooterFadeReady = true;
	Controller->bPartnerFadeReady = true;
	Controller->CompleteLanding();
	TestTrue(TEXT("Landing rechecks gameplay readiness before opening the exit"),
		Controller->Motion.State == EElevatorShaftState::TransitionOut);
	TestFalse(TEXT("Invalidated arrival returns to the departure coordinate system"), Controller->Motion.bArrival);
	TestFalse(TEXT("Invalidated arrival releases its input block"), Controller->InputBlockId.IsValid());
	Controller->InputBlockId.Invalidate();
	return true;
}

#endif
