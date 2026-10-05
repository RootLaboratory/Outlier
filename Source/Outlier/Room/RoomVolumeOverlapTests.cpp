#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "Misc/AutomationTest.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomTagComponent.h"
#include "Room/RoomVolume.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomVolumeStreamingOverlapTest,
	"Outlier.Room.StreamingOverlapAssignment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomVolumeStreamingOverlapTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("RoomStreamingOverlapTest")));
	if (!TestNotNull(TEXT("Overlap test world exists"), World))
	{
		return false;
	}
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	const auto Cleanup = [World]()
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};

	const FGameplayTag RoomTag = FGameplayTag::RequestGameplayTag(TEXT("Room.Level01.1"));
	const FGameplayTag OtherRoomTag = FGameplayTag::RequestGameplayTag(TEXT("Room.Level01.2"));
	URoomCombatDefinition* Definition = NewObject<URoomCombatDefinition>(World);
	for (const FGameplayTag Tag : { RoomTag, OtherRoomTag })
	{
		FRoomCombatRoomDefinition& RoomDefinition = Definition->RoomDefinitions.AddDefaulted_GetRef();
		RoomDefinition.RoomTag = Tag;
		FRoomCombatPhaseDefinition& Phase = RoomDefinition.CombatPhases.AddDefaulted_GetRef();
		Phase.StartPolicy = ERoomCombatPhaseStartPolicy::ExternalTrigger;
		Phase.Waves.AddDefaulted();
	}
	World->GetSubsystem<URoomCombatSubsystem>()->SetCombatDefinitionForTesting(Definition);

	// Pawn을 먼저 만든 뒤, 스트리밍처럼 알림 없이 Volume의 overlap 캐시를 채운다.
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AFirstPersonCharacter* Player = World->SpawnActor<AFirstPersonCharacter>(
		AFirstPersonCharacter::StaticClass(), FTransform::Identity, SpawnParams);
	ARoomVolume* Room = World->SpawnActor<ARoomVolume>(
		ARoomVolume::StaticClass(), FVector(2000.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Player exists before the room loads"), Player)
		|| !TestNotNull(TEXT("Room exists"), Room))
	{
		Cleanup();
		return false;
	}
	UCapsuleComponent* Capsule = Player->GetCapsuleComponent();
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Capsule->SetCollisionObjectType(ECC_Pawn);
	Capsule->SetCollisionResponseToAllChannels(ECR_Overlap);
	Capsule->SetGenerateOverlapEvents(true);
	UBoxComponent* Box = CastChecked<UBoxComponent>(Room->GetRootComponent());
	Box->SetBoxExtent(FVector(500.0f));
	FStructProperty* TagProperty = FindFProperty<FStructProperty>(ARoomVolume::StaticClass(), TEXT("RoomTag"));
	if (!TestNotNull(TEXT("RoomTag fixture property exists"), TagProperty)
		|| !TestNotNull(TEXT("Fixture has a physics scene"), World->GetPhysicsScene()))
	{
		Cleanup();
		return false;
	}
	*TagProperty->ContainerPtrToValuePtr<FGameplayTag>(Room) = RoomTag;
	Player->DispatchBeginPlay();
	Room->DispatchBeginPlay();
	// BeginPlay 이후에만 엔진이 overlap 조회를 수행한다. Volume을 알림 없이 넓혀
	// UpdateInitialOverlaps(false)와 같은 캐시가 있지만 입장 알림은 없는 상태를 만든다.
	Box->SetBoxExtent(FVector(3000.0f), false);
	World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
	World->GetPhysicsScene()->Flush();
	Box->UpdateOverlaps(nullptr, false);
	TestTrue(TEXT("The silent overlap is cached"), Box->IsOverlappingActor(Player));
	URoomTagComponent* RoomTags = Player->GetRoomTagComp();
	TestFalse(TEXT("Silent overlap did not assign a RoomTag"), Player->GetCurrentRoomTag().IsValid());

	Box->UpdateOverlaps();
	TestFalse(TEXT("Updating existing overlaps does not replay BeginOverlap"), Player->GetCurrentRoomTag().IsValid());
	int32 EnterNotifications = 0;
	Room->OnRoomActorOverlapChanged.AddLambda([&](AActor* Actor, bool bEntered)
	{
		if (Actor == Player && bEntered)
		{
			++EnterNotifications;
			TestEqual(TEXT("Entry notification sees the restored tag"), Player->GetCurrentRoomTag(), RoomTag);
		}
	});
	Room->RefreshOverlappingRoomAssignments();
	TestEqual(TEXT("Reconciliation restores a cached silent overlap"), Player->GetCurrentRoomTag(), RoomTag);
	TestEqual(TEXT("Recovered entry is notified once"), EnterNotifications, 1);
	Room->RefreshOverlappingRoomAssignments();
	TestEqual(TEXT("Reload-ready reconciliation does not repeat entry"), EnterNotifications, 1);

	// 여러 Volume에 겹친 경우 재검사가 기존의 마지막 입장 순서를 뒤집으면 안 된다.
	ARoomVolume* OtherRoom = World->SpawnActor<ARoomVolume>(
		ARoomVolume::StaticClass(), FVector(2000.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Other room exists"), OtherRoom))
	{
		Cleanup();
		return false;
	}
	*TagProperty->ContainerPtrToValuePtr<FGameplayTag>(OtherRoom) = OtherRoomTag;
	CastChecked<UBoxComponent>(OtherRoom->GetRootComponent())->SetBoxExtent(FVector(3000.0f));
	World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
	World->GetPhysicsScene()->Flush();
	OtherRoom->DispatchBeginPlay();
	TestTrue(TEXT("Room BeginPlay assigns a player already inside"), RoomTags->HasActiveRoom(OtherRoom));
	Room->RefreshOverlappingRoomAssignments();
	TestEqual(TEXT("Reconciliation preserves the existing room priority"), Player->GetCurrentRoomTag(), OtherRoomTag);
	RoomTags->LeaveRoom(OtherRoom);

	// Ready 시점에도 캐시와 태그가 어긋나 있으면 다시 복구해야 한다.
	RoomTags->ClearRuntimeRoomAssignment();
	Room->RefreshOverlappingRoomAssignments();
	TestEqual(TEXT("Reload-ready reconciliation restores a missing assignment"), Player->GetCurrentRoomTag(), RoomTag);
	TestEqual(TEXT("A genuinely missing assignment emits another entry"), EnterNotifications, 2);
	Player->SetActorLocation(FVector(10000.0f, 0.0f, 0.0f));
	TestFalse(TEXT("Leaving the room removes the recovered tag"), Player->GetCurrentRoomTag().IsValid());
	Room->RefreshOverlappingRoomAssignments();
	TestFalse(TEXT("An outside player is not assigned to the room"), RoomTags->HasActiveRoom(Room));

	Cleanup();
	return true;
}

#endif
