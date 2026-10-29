#if WITH_DEV_AUTOMATION_TESTS

#include "Components/ChildActorComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/InteractableComponent.h"
#include "Interaction/InteractableDoor.h"
#include "Interaction/InteractableSwitch.h"
#include "Level/Elevator/ElevatorShaftController.h"
#include "Level/Elevator/ElevatorShaftSection.h"
#include "Level/Elevator/ElevatorShaftWall.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Save/OutlierSaveSubSystem.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteractableSwitchDoorBindingTest,
	"Outlier.Interaction.Switch.ElevatorDoorBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInteractableSwitchDoorBindingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("SwitchDoorBindingTest")));
	if (!TestNotNull(TEXT("Test world exists"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
	World->SetGameInstance(GameInstance);
	GameInstance->Init();
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		GameInstance->Shutdown();
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	UOutlierSaveSubSystem* Save = GameInstance->GetSubsystem<UOutlierSaveSubSystem>();
	if (!TestNotNull(TEXT("Save subsystem exists"), Save)) return false;

	const auto AddChild = [](AActor* ParentActor, UClass* Class)
	{
		UChildActorComponent* Component = NewObject<UChildActorComponent>(ParentActor);
		ParentActor->AddInstanceComponent(Component);
		Component->SetupAttachment(ParentActor->GetRootComponent());
		Component->SetChildActorClass(Class);
		Component->RegisterComponent();
		return Component;
	};
	APartnerCharacter* Interactor = World->SpawnActor<APartnerCharacter>(
		APartnerCharacter::StaticClass(), FVector(10000, 0, 0), FRotator::ZeroRotator);
	AElevatorShaftController* Elevator = World->SpawnActor<AElevatorShaftController>();
	AInteractableDoor* DirectDoor = World->SpawnActor<AInteractableDoor>();
	AElevatorShaftSection* Arrival = World->SpawnActor<AElevatorShaftSection>();
	if (!TestNotNull(TEXT("Interactor exists"), Interactor)
		|| !TestNotNull(TEXT("Elevator exists"), Elevator)
		|| !TestNotNull(TEXT("Direct door exists"), DirectDoor)
		|| !TestNotNull(TEXT("Arrival section exists"), Arrival)) return false;
	UChildActorComponent* EntranceComponent = AddChild(Elevator, AElevatorShaftSection::StaticClass());
	AElevatorShaftSection* Entrance = Cast<AElevatorShaftSection>(EntranceComponent->GetChildActor());
	if (!TestNotNull(TEXT("Entrance section constructed"), Entrance)) return false;
	UChildActorComponent* WallComponent = AddChild(Entrance, AElevatorShaftWall::StaticClass());
	if (!TestNotNull(TEXT("Nested wall constructed"), WallComponent->GetChildActor())) return false;
	UChildActorComponent* DoorComponent = AddChild(WallComponent->GetChildActor(), AInteractableDoor::StaticClass());
	AInteractableDoor* EntranceDoor = Cast<AInteractableDoor>(DoorComponent->GetChildActor());
	UChildActorComponent* ArrivalDoorComponent = AddChild(Arrival, AInteractableDoor::StaticClass());
	AInteractableDoor* ArrivalDoor = Cast<AInteractableDoor>(ArrivalDoorComponent->GetChildActor());
	if (!TestNotNull(TEXT("Entrance door constructed"), EntranceDoor)
		|| !TestNotNull(TEXT("Arrival door constructed"), ArrivalDoor)) return false;
	Elevator->ArrivalSection = Arrival;
	Elevator->EntranceDoorId = TEXT("BindingEntranceDoor");
	Elevator->ArrivalDoorId = TEXT("BindingArrivalDoor");
	DirectDoor->DoorId = TEXT("BindingDirectDoor");
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Safety fixture mesh exists"), Cube)) return false;
	ArrivalDoor->DoorMeshLeft->SetStaticMesh(Cube);
	ArrivalDoor->DoorMeshRight->SetStaticMesh(Cube);
	EntranceDoor->DispatchBeginPlay();
	ArrivalDoor->DispatchBeginPlay();
	DirectDoor->DispatchBeginPlay();

	// 각 요청은 실제 Interact 경로를 거친다. 실패 시 활성화/저장/성공 통보까지 함께 검사한다.
	int32 SwitchIndex = 0;
	const auto MakeSwitch = [&](EInteractableSwitchDoorSource Source)
	{
		AInteractableSwitch* Switch = World->SpawnActor<AInteractableSwitch>();
		Switch->DoorSource = Source;
		Switch->TargetDoor = DirectDoor;
		Switch->TargetElevator = Elevator;
		Switch->TargetSection = Arrival;
		Switch->SwitchId = FName(*FString::Printf(TEXT("BindingSwitch_%d"), ++SwitchIndex));
		Switch->DispatchBeginPlay();
		return Switch;
	};
	const auto CheckRejected = [&](const TCHAR* Label, AInteractableSwitch* Switch)
	{
		TestFalse(Label, Switch->Interact(Interactor));
		TestFalse(TEXT("Rejected request does not activate switch"), Switch->bIsActivated);
		TestFalse(TEXT("Rejected request does not dispatch success callback"), Switch->bActivationEventApplied);
		TestFalse(TEXT("Rejected request does not record progress"),
			Save->HasWorldProgress(EOutlierWorldProgressType::ActivatedSwitch, Switch->SwitchId));
		TestFalse(TEXT("Rejected request does not consume switch"),
			Switch->InteractableComponent->InteractableTags.HasTagExact(OutlierGameplayTags::State::Used()));
	};
	TestEqual(TEXT("Legacy assets default to enum zero"),
		static_cast<uint8>(GetDefault<AInteractableSwitch>()->DoorSource), uint8(0));
	AInteractableSwitch* DirectSwitch = MakeSwitch(EInteractableSwitchDoorSource::DirectDoor);
	TestTrue(TEXT("Direct connection opens selected door"), DirectSwitch->Interact(Interactor));
	TestTrue(TEXT("Direct door opens"), DirectDoor->IsDoorOpen());
	TestFalse(TEXT("Inactive elevator reference is ignored"), EntranceDoor->IsDoorOpen());
	TestTrue(TEXT("Success callback dispatched"), DirectSwitch->bActivationEventApplied);
	TestTrue(TEXT("Success progress recorded"), Save->HasWorldProgress(
		EOutlierWorldProgressType::ActivatedSwitch, DirectSwitch->SwitchId));
	TestTrue(TEXT("Legacy toggle closes again"), DirectSwitch->Interact(Interactor));
	TestFalse(TEXT("Direct door closes"), DirectDoor->IsDoorOpen());
	DirectSwitch->bCanToggleDoor = false;
	TestTrue(TEXT("Open-only opens"), DirectSwitch->Interact(Interactor));
	TestTrue(TEXT("Open-only repeat succeeds"), DirectSwitch->Interact(Interactor));
	TestTrue(TEXT("Open-only repeat leaves door open"), DirectDoor->IsDoorOpen());

	AInteractableSwitch* EntranceSwitch = MakeSwitch(EInteractableSwitchDoorSource::ElevatorEntrance);
	TestTrue(TEXT("Entrance resolves nested wall door without Controller BeginPlay"), EntranceSwitch->Interact(Interactor));
	TestTrue(TEXT("Entrance door opens"), EntranceDoor->IsDoorOpen());
	TestTrue(TEXT("Inactive direct reference remains untouched"), DirectDoor->IsDoorOpen());
	DoorComponent->DestroyComponent();
	Save->SetWorldProgressState(EOutlierWorldProgressType::OpenedDoor, Elevator->EntranceDoorId, false);
	DoorComponent = AddChild(WallComponent->GetChildActor(), AInteractableDoor::StaticClass());
	EntranceDoor = Cast<AInteractableDoor>(DoorComponent->GetChildActor());
	if (!TestNotNull(TEXT("Replacement entrance child constructed"), EntranceDoor)) return false;
	EntranceDoor->DispatchBeginPlay();
	TestTrue(TEXT("Recreated child is resolved without refreshing Controller cache"), EntranceSwitch->Interact(Interactor));
	TestTrue(TEXT("Recreated entrance door opens"), EntranceDoor->IsDoorOpen());
	AInteractableSwitch* ArrivalSwitch = MakeSwitch(EInteractableSwitchDoorSource::ElevatorArrival);
	TestTrue(TEXT("Arrival selects separate section"), ArrivalSwitch->Interact(Interactor));
	TestTrue(TEXT("Arrival door opens"), ArrivalDoor->IsDoorOpen());
	AInteractableSwitch* SectionSwitch = MakeSwitch(EInteractableSwitchDoorSource::ElevatorSection);
	SectionSwitch->TargetElevator = nullptr;
	SectionSwitch->bCanToggleDoor = false;
	SectionSwitch->RefreshDoorPreview();
	TestEqual(TEXT("Standalone arrival section shows its nested door"), SectionSwitch->ResolvedTargetDoor.Get(), ArrivalDoor);
	TestTrue(TEXT("Section preview contains a diagnostic"), !SectionSwitch->DoorBindingStatus.IsEmpty());
	TestTrue(TEXT("Preview does not activate switch"), !SectionSwitch->bIsActivated);
	TestTrue(TEXT("Separate arrival section can be used without a Controller reference"), SectionSwitch->Interact(Interactor));

	// 이전 문이 살아 있어도 ArrivalSection 참조 교체 뒤에는 새 계층을 사용해야 한다.
	AElevatorShaftSection* Replacement = World->SpawnActor<AElevatorShaftSection>();
	UChildActorComponent* ReplacementComponent = AddChild(Replacement, AInteractableDoor::StaticClass());
	AInteractableDoor* ReplacementDoor = Cast<AInteractableDoor>(ReplacementComponent->GetChildActor());
	if (!TestNotNull(TEXT("Replacement door constructed"), ReplacementDoor)) return false;
	Elevator->ArrivalSection = Replacement;
	Elevator->ArrivalDoorId = TEXT("BindingReplacementDoor");
	ReplacementDoor->DispatchBeginPlay();
	TestTrue(TEXT("Current replacement opens"), ArrivalSwitch->Interact(Interactor));
	TestTrue(TEXT("New door receives request"), ReplacementDoor->IsDoorOpen());
	TestTrue(TEXT("Old valid door is not toggled"), ArrivalDoor->IsDoorOpen());
	SectionSwitch->TargetSection = Replacement;
	TestEqual(TEXT("Preview still holds old door until refresh/request"), SectionSwitch->ResolvedTargetDoor.Get(), ArrivalDoor);
	TestTrue(TEXT("Section interaction ignores stale preview"), SectionSwitch->Interact(Interactor));
	TestEqual(TEXT("Server request updates read-only door display"), SectionSwitch->ResolvedTargetDoor.Get(), ReplacementDoor);
	TestTrue(TEXT("Direct reference is ignored in Section mode"), DirectDoor->IsDoorOpen());

	AInteractableSwitch* Missing = MakeSwitch(EInteractableSwitchDoorSource::ElevatorEntrance);
	Missing->TargetElevator = nullptr;
	CheckRejected(TEXT("Missing elevator does not fall back to direct door"), Missing);
	AInteractableSwitch* Unknown = MakeSwitch(static_cast<EInteractableSwitchDoorSource>(255));
	CheckRejected(TEXT("Unknown mode rejected"), Unknown);
	AInteractableSwitch* MissingDirect = MakeSwitch(EInteractableSwitchDoorSource::DirectDoor);
	MissingDirect->TargetDoor = nullptr;
	CheckRejected(TEXT("Missing direct door does not fall back to elevator"), MissingDirect);
	AInteractableSwitch* MissingSection = MakeSwitch(EInteractableSwitchDoorSource::ElevatorSection);
	MissingSection->RefreshDoorPreview();
	MissingSection->TargetSection = nullptr;
	MissingSection->RefreshDoorPreview();
	TestNull(TEXT("Missing Section clears old preview"), MissingSection->ResolvedTargetDoor.Get());
	CheckRejected(TEXT("Missing Section does not fall back to Controller or direct door"), MissingSection);
	AElevatorShaftSection* Empty = World->SpawnActor<AElevatorShaftSection>();
	Elevator->ArrivalSection = Empty;
	CheckRejected(TEXT("Empty section rejected"), MakeSwitch(EInteractableSwitchDoorSource::ElevatorArrival));
	Elevator->ArrivalSection = Arrival;
	UChildActorComponent* DuplicateDoor = AddChild(Arrival, AInteractableDoor::StaticClass());
	CheckRejected(TEXT("Duplicate doors rejected"), MakeSwitch(EInteractableSwitchDoorSource::ElevatorArrival));
	CheckRejected(TEXT("Direct Section mode also rejects duplicate doors"), MakeSwitch(EInteractableSwitchDoorSource::ElevatorSection));
	DuplicateDoor->DestroyComponent();
	UChildActorComponent* DuplicateSection = AddChild(Elevator, AElevatorShaftSection::StaticClass());
	CheckRejected(TEXT("Duplicate entrance sections rejected"), MakeSwitch(EInteractableSwitchDoorSource::ElevatorEntrance));
	DuplicateSection->DestroyComponent();

	TestTrue(TEXT("Close arrival before presentation mode"), ArrivalDoor->TrySetDoorOpen(false));
	TestTrue(TEXT("Presentation-only enabled"), ArrivalDoor->SetPresentationOnly(true));
	CheckRejected(TEXT("Presentation-only door rejects switch"), MakeSwitch(EInteractableSwitchDoorSource::ElevatorArrival));
	TestFalse(TEXT("Rejected presentation door stays closed"), ArrivalDoor->IsDoorOpen());
	ArrivalDoor->SetPresentationOnly(false);
	AInteractableSwitch* Locked = MakeSwitch(EInteractableSwitchDoorSource::ElevatorArrival);
	Locked->InteractableComponent->InteractableTags.AddTag(OutlierGameplayTags::State::Locked());
	CheckRejected(TEXT("Hack lock blocks door request"), Locked);
	TestFalse(TEXT("Locked switch does not open door"), ArrivalDoor->IsDoorOpen());
	Locked->InteractableComponent->InteractableTags.RemoveTag(OutlierGameplayTags::State::Locked());
	TestTrue(TEXT("Unlocked switch uses same resolver"), Locked->Interact(Interactor));
	// 엘리베이터 연결도 기존 문 안전 검사를 그대로 통과해야 한다.
	APlayerController* PlayerController = World->SpawnActor<APlayerController>();
	FPhysScene* PhysicsScene = World->GetPhysicsScene();
	if (!TestNotNull(TEXT("Safety player controller exists"), PlayerController)
		|| !TestNotNull(TEXT("Safety physics scene exists"), PhysicsScene)) return false;
	PlayerController->Possess(Interactor);
	Interactor->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Interactor->SetActorLocation(ArrivalDoor->SafetyRegionLeft->GetComponentLocation());
	PhysicsScene->ProcessDeferredCreatePhysicsState();
	PhysicsScene->Flush();
	if (!TestTrue(TEXT("Player occupies closing path"), ArrivalDoor->HasBlockingPlayer())) return false;
	CheckRejected(TEXT("Occupied elevator door rejects closing"), MakeSwitch(EInteractableSwitchDoorSource::ElevatorArrival));
	TestTrue(TEXT("Safety rejection leaves door open"), ArrivalDoor->IsDoorOpen());
	Interactor->SetActorLocation(FVector(10000, 0, 0));
	PlayerController->UnPossess();
	PhysicsScene->Flush();
	AInteractableSwitch* ClientSwitch = MakeSwitch(EInteractableSwitchDoorSource::ElevatorArrival);
	ClientSwitch->SetRole(ROLE_SimulatedProxy);
	CheckRejected(TEXT("Client cannot activate"), ClientSwitch);
	ClientSwitch->SetRole(ROLE_Authority);
	ArrivalDoor->SetRole(ROLE_SimulatedProxy);
	CheckRejected(TEXT("Server rejects non-authority door"), MakeSwitch(EInteractableSwitchDoorSource::ElevatorArrival));
	ArrivalDoor->SetRole(ROLE_Authority);

	// 저장된 사용 상태 복원은 문 요청을 재실행하지 않는다.
	AInteractableSwitch* Restored = World->SpawnActor<AInteractableSwitch>();
	Restored->DoorSource = EInteractableSwitchDoorSource::ElevatorArrival;
	Restored->TargetElevator = Elevator;
	Restored->SwitchId = TEXT("RestoredBindingSwitch");
	Save->SetWorldProgressState(EOutlierWorldProgressType::ActivatedSwitch, Restored->SwitchId, true);
	Restored->DispatchBeginPlay();
	TestTrue(TEXT("Activated state restored"), Restored->bIsActivated);
	TestTrue(TEXT("Used state restored"), Restored->InteractableComponent->InteractableTags.HasTagExact(
		OutlierGameplayTags::State::Used()));
	TestTrue(TEXT("Restore does not toggle open door"), ArrivalDoor->IsDoorOpen());

#if WITH_EDITOR
	FDataValidationContext ValidContext;
	DirectSwitch->TargetElevator = nullptr;
	TestEqual(TEXT("Only selected mode is validated"), DirectSwitch->IsDataValid(ValidContext), EDataValidationResult::Valid);
	FDataValidationContext InvalidContext;
	TestEqual(TEXT("Missing selected reference is invalid"), Missing->IsDataValid(InvalidContext), EDataValidationResult::Invalid);
	FDataValidationContext SectionContext;
	SectionSwitch->TargetElevator = nullptr;
	TestEqual(TEXT("Section mode validates without Controller"), SectionSwitch->IsDataValid(SectionContext), EDataValidationResult::Valid);
	FDataValidationContext TemplateContext;
	GetDefault<AInteractableSwitch>()->IsDataValid(TemplateContext);
	TestEqual(TEXT("Class defaults do not require level references"), TemplateContext.GetNumErrors(), uint32(0));
	// 미등록 Child Actor 컴포넌트는 클래스만 있고 실체는 아직 없다. 구성 오류와 구별한다.
	UChildActorComponent* PendingChild = NewObject<UChildActorComponent>(Arrival);
	Arrival->AddInstanceComponent(PendingChild);
	PendingChild->SetChildActorClass(AInteractableDoor::StaticClass());
	FDataValidationContext PendingContext;
	ArrivalSwitch->IsDataValid(PendingContext);
	TestEqual(TEXT("Pending construction is not a configuration error"), PendingContext.GetNumErrors(), uint32(0));
	TestEqual(TEXT("Pending construction is diagnosed as warning"), PendingContext.GetNumWarnings(), uint32(1));
	CheckRejected(TEXT("Runtime does not defer pending request"), MakeSwitch(EInteractableSwitchDoorSource::ElevatorArrival));
	PendingChild->DestroyComponent();
#endif
	UWorld* OtherWorld = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("SwitchOtherWorld")));
	if (!TestNotNull(TEXT("Second world exists"), OtherWorld)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(OtherWorld);
	OtherWorld->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		OtherWorld->DestroyWorld(false);
		GEngine->DestroyWorldContext(OtherWorld);
		OtherWorld->RemoveFromRoot();
	};
	AInteractableSwitch* CrossWorld = MakeSwitch(EInteractableSwitchDoorSource::DirectDoor);
	CrossWorld->TargetDoor = OtherWorld->SpawnActor<AInteractableDoor>();
	CheckRejected(TEXT("Cross-world direct door rejected"), CrossWorld);
	CrossWorld->DoorSource = EInteractableSwitchDoorSource::ElevatorEntrance;
	CrossWorld->TargetElevator = OtherWorld->SpawnActor<AElevatorShaftController>();
	CheckRejected(TEXT("Cross-world controller rejected"), CrossWorld);
	Elevator->ArrivalSection = OtherWorld->SpawnActor<AElevatorShaftSection>();
	CheckRejected(TEXT("Cross-world arrival section rejected"), MakeSwitch(EInteractableSwitchDoorSource::ElevatorArrival));
	AInteractableSwitch* CrossWorldSection = MakeSwitch(EInteractableSwitchDoorSource::ElevatorSection);
	CrossWorldSection->TargetSection = Elevator->ArrivalSection;
	CheckRejected(TEXT("Cross-world direct Section rejected"), CrossWorldSection);
	Elevator->ArrivalSection = Arrival;
	ArrivalDoor->Destroy();
	CheckRejected(TEXT("Destroying door rejected"), MakeSwitch(EInteractableSwitchDoorSource::ElevatorArrival));
	return true;
}

#endif
