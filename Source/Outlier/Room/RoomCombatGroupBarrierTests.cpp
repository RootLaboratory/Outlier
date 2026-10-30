#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Enemy/EnemyBase.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Room/RoomCombatBarrier.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomTagComponent.h"
#include "Room/RoomVolume.h"
#include "UObject/UnrealType.h"

namespace
{
UE_DEFINE_GAMEPLAY_TAG_STATIC(BarrierA, "Room.GroupBarrier01.1.1");
UE_DEFINE_GAMEPLAY_TAG_STATIC(BarrierB, "Room.GroupBarrier01.1.2");
UE_DEFINE_GAMEPLAY_TAG_STATIC(BarrierC, "Room.GroupBarrier01.1.3");
UE_DEFINE_GAMEPLAY_TAG_STATIC(BarrierOther, "Room.GroupBarrier01.2.1");
UE_DEFINE_GAMEPLAY_TAG_STATIC(BarrierPrefix, "Room.GroupBarrier01.10.1");
UE_DEFINE_GAMEPLAY_TAG_STATIC(BarrierOtherLevel, "Room.GroupBarrier02.1.1");
UE_DEFINE_GAMEPLAY_TAG_STATIC(BarrierLegacy, "Room.GroupBarrierLegacy.1");

FRoomCombatPhaseDefinition BarrierPhase(ERoomCombatPhaseStartPolicy Policy = ERoomCombatPhaseStartPolicy::InitialDetection)
{
	FRoomCombatPhaseDefinition Phase;
	Phase.StartPolicy = Policy;
	Phase.DelayBeforeStartSeconds = 0;
	FRoomCombatWaveDefinition& Wave = Phase.Waves.AddDefaulted_GetRef();
	if (Policy != ERoomCombatPhaseStartPolicy::InitialDetection)
	{
		Wave.SpawnMode = ERoomCombatWaveSpawnMode::SpawnFromObjects;
		auto& Entry = Wave.Enemies.AddDefaulted_GetRef();
		Entry.EnemyClass = AEnemyBase::StaticClass();
		Entry.Count = 1;
	}
	return Phase;
}

struct FGroupBarrierWorld
{
	UWorld* World;
	UGameInstance* GameInstance;
	URoomCombatSubsystem* Combat;
	URoomCombatDefinition* Definition;
	AActor* Requester;

	FGroupBarrierWorld()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("GroupBarrierTest")));
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		GameInstance = NewObject<UGameInstance>(GEngine);
		World->SetGameInstance(GameInstance);
		GameInstance->Init();
		World->SetGameMode(FURL());
		World->InitializeActorsForPlay(FURL());
		Combat = World->GetSubsystem<URoomCombatSubsystem>();
		Definition = NewObject<URoomCombatDefinition>(World);
		Combat->SetCombatDefinitionForTesting(Definition);
		Requester = World->SpawnActor<AActor>();
	}

	~FGroupBarrierWorld()
	{
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(false);
		GameInstance->Shutdown();
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}

	ARoomVolume* AddRoom(FGameplayTag Tag, TArray<FRoomCombatPhaseDefinition> Phases = {BarrierPhase()})
	{
		auto& Entry = Definition->RoomDefinitions.AddDefaulted_GetRef();
		Entry.RoomTag = Tag;
		Entry.CombatPhases = MoveTemp(Phases);
		ARoomVolume* Room = World->SpawnActor<ARoomVolume>();
		*FindFProperty<FStructProperty>(ARoomVolume::StaticClass(), TEXT("RoomTag"))
			->ContainerPtrToValuePtr<FGameplayTag>(Room) = Tag;
		Combat->RegisterRoom(Room, Tag);
		return Room;
	}

	AEnemyBase* AddEnemy(FGameplayTag Tag)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AEnemyBase* Enemy = World->SpawnActor<AEnemyBase>(AEnemyBase::StaticClass(), FTransform(FVector(-10000, 0, 0)), Params);
		Enemy->GetRoomTagComp()->AssignDefaultRoomTag(Tag);
		Combat->RegisterPreplacedEnemy(Enemy);
		return Enemy;
	}

	ARoomCombatBarrier* AddBarrier(FGameplayTag Tag, FGameplayTag Connected = FGameplayTag(), FGameplayTag Additional = FGameplayTag())
	{
		ARoomCombatBarrier* Barrier = World->SpawnActorDeferred<ARoomCombatBarrier>(ARoomCombatBarrier::StaticClass(), FTransform::Identity);
		*FindFProperty<FStructProperty>(ARoomCombatBarrier::StaticClass(), TEXT("RoomTag"))
			->ContainerPtrToValuePtr<FGameplayTag>(Barrier) = Tag;
		*FindFProperty<FStructProperty>(ARoomCombatBarrier::StaticClass(), TEXT("ConnectedRoomTag"))
			->ContainerPtrToValuePtr<FGameplayTag>(Barrier) = Connected;
		if (Additional.IsValid())
		{
			FindFProperty<FStructProperty>(ARoomCombatBarrier::StaticClass(), TEXT("AdditionalRoomTags"))
				->ContainerPtrToValuePtr<FGameplayTagContainer>(Barrier)->AddTag(Additional);
		}
		Barrier->FinishSpawning(FTransform::Identity);
		if (!Barrier->HasActorBegunPlay()) Barrier->DispatchBeginPlay();
		return Barrier;
	}

	bool Start(FGameplayTag Tag)
	{
		FRoomCombatPreparationContext Preparation;
		return Combat->BeginInitialDetectionPreparation(Tag, Requester, Preparation)
			&& Combat->CompleteInitialDetectionPreparation(Preparation);
	}
};

void CheckBarrierPresentation(FAutomationTestBase& Test, ARoomCombatBarrier* Barrier, bool bBlocked)
{
	Test.TestEqual(TEXT("Authoritative barrier state"), Barrier->IsBlocked(), bBlocked);
	const auto* Box = CastChecked<UBoxComponent>(Barrier->GetRootComponent());
	Test.TestEqual(TEXT("Blocking collision follows authoritative state"), Box->GetCollisionEnabled(),
		bBlocked ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	const auto* Visual = Barrier->FindComponentByClass<UStaticMeshComponent>();
	const auto* Hidden = FindFProperty<FBoolProperty>(USceneComponent::StaticClass(), TEXT("bHiddenInGame"));
	Test.TestEqual(TEXT("Visual hidden state follows authoritative state"), Hidden->GetPropertyValue_InContainer(Visual), !bBlocked);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroupBarrierParticipationTest, "Outlier.Room.GroupBarrier.Participation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGroupBarrierParticipationTest::RunTest(const FString& Parameters)
{
	FGroupBarrierWorld Test;
	Test.AddRoom(BarrierA);
	Test.AddRoom(BarrierB);
	Test.AddRoom(BarrierC, {BarrierPhase(ERoomCombatPhaseStartPolicy::ExternalTrigger)});
	AEnemyBase* A = Test.AddEnemy(BarrierA);
	AEnemyBase* B = Test.AddEnemy(BarrierB);
	ARoomCombatBarrier* OuterA = Test.AddBarrier(BarrierA);
	ARoomCombatBarrier* OuterC = Test.AddBarrier(BarrierC);
	ARoomCombatBarrier* Internal = Test.AddBarrier(BarrierA, BarrierB);
	FRoomCombatPreparationContext Preparing;
	TestTrue(TEXT("A can prepare"), Test.Combat->BeginInitialDetectionPreparation(BarrierA, Test.Requester, Preparing));
	TestFalse(TEXT("Reservation alone never blocks group exits"), Test.Combat->IsGroupExitBlocked(BarrierC));
	CheckBarrierPresentation(*this, OuterA, false);
	TestTrue(TEXT("A starts"), Test.Combat->CompleteInitialDetectionPreparation(Preparing));
	CheckBarrierPresentation(*this, OuterA, true);
	CheckBarrierPresentation(*this, OuterC, true);
	CheckBarrierPresentation(*this, Internal, false);
	TestFalse(TEXT("Internal passage never blocks a join destination"), Internal->CanBlockRoom(BarrierA));
	TestTrue(TEXT("Group exterior can constrain sibling join destination"), OuterC->CanBlockRoom(BarrierA));
	TestTrue(TEXT("B prepares alongside A"), Test.Combat->BeginInitialDetectionPreparation(BarrierB, Test.Requester, Preparing));
	TestTrue(TEXT("B failed join cancels its attempt"), Test.Combat->CancelInitialDetectionPreparation(Preparing));
	CheckBarrierPresentation(*this, OuterA, true);
	// 실패한 합류는 재시도 간격을 지킨다. 대기 중에도 A의 외곽 차단은 유지되어야 한다.
	const float RetrySeconds = IConsoleManager::Get().FindConsoleVariable(
		TEXT("outlier.RoomCombat.InitialDetectionRetrySeconds"))->GetFloat();
	for (int32 Step = 0; Step <= FMath::CeilToInt(FMath::Max(0.0f, RetrySeconds) / 0.1f); ++Step)
	{
		Test.World->Tick(LEVELTICK_All, 0.1f);
	}
	CheckBarrierPresentation(*this, OuterA, true);
	TestTrue(TEXT("B later actually starts"), Test.Start(BarrierB));
	Test.Combat->NotifyEnemyDefeated(A);
	TestEqual(TEXT("A has independently cleared"), Test.Combat->GetRoomState(BarrierA), ERoomCombatState::Cleared);
	TestFalse(TEXT("Original room-specific query remains local"), Test.Combat->IsExitBlocked(BarrierA));
	TestTrue(TEXT("B keeps A group exit blocked"), Test.Combat->IsGroupExitBlocked(BarrierA));
	CheckBarrierPresentation(*this, OuterA, true);
	CheckBarrierPresentation(*this, Internal, false);
	Test.Combat->NotifyEnemyDefeated(B);
	CheckBarrierPresentation(*this, OuterA, false);
	CheckBarrierPresentation(*this, OuterC, false);
	TestEqual(TEXT("Nonparticipant C still waits for its own event"), Test.Combat->GetRoomState(BarrierC), ERoomCombatState::WaitingForTrigger);
	TestFalse(TEXT("Group completion does not reserve nonparticipant C"), Test.Combat->IsRoomCombatReserved(BarrierC));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroupBarrierLateParticipationTest, "Outlier.Room.GroupBarrier.LateParticipationAndUnload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGroupBarrierLateParticipationTest::RunTest(const FString& Parameters)
{
	FGroupBarrierWorld Test;
	Test.AddRoom(BarrierA);
	ARoomVolume* RoomB = Test.AddRoom(BarrierB);
	Test.AddRoom(BarrierC);
	AEnemyBase* A = Test.AddEnemy(BarrierA);
	Test.AddEnemy(BarrierB);
	AEnemyBase* C = Test.AddEnemy(BarrierC);
	TestTrue(TEXT("A starts"), Test.Start(BarrierA));
	ARoomCombatBarrier* Late = Test.AddBarrier(BarrierC);
	CheckBarrierPresentation(*this, Late, true);
	TestTrue(TEXT("B starts"), Test.Start(BarrierB));
	TestTrue(TEXT("C joins while group is active"), Test.Start(BarrierC));
	Test.Combat->NotifyEnemyDefeated(A);
	Test.Combat->UnregisterRoom(RoomB);
	CheckBarrierPresentation(*this, Late, true);
	TestTrue(TEXT("C remains blocking after A clears and B unloads"), Test.Combat->IsGroupExitBlocked(BarrierA));
	Test.Combat->NotifyEnemyDefeated(C);
	CheckBarrierPresentation(*this, Late, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroupBarrierWaitingResetTest, "Outlier.Room.GroupBarrier.WaitingAndReset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGroupBarrierWaitingResetTest::RunTest(const FString& Parameters)
{
	FGroupBarrierWorld Test;
	Test.AddRoom(BarrierA, {BarrierPhase(), BarrierPhase(ERoomCombatPhaseStartPolicy::ExternalTrigger)});
	Test.AddRoom(BarrierB);
	AEnemyBase* A = Test.AddEnemy(BarrierA);
	ARoomCombatBarrier* Outer = Test.AddBarrier(BarrierB);
	TestTrue(TEXT("A starts"), Test.Start(BarrierA));
	Test.Combat->NotifyEnemyDefeated(A);
	TestEqual(TEXT("A waits for later external phase"), Test.Combat->GetRoomState(BarrierA), ERoomCombatState::WaitingForTrigger);
	CheckBarrierPresentation(*this, Outer, true);
	Test.Combat->ResetRuntimeCombatState();
	TestFalse(TEXT("Reset clears group barrier state"), Test.Combat->IsGroupExitBlocked(BarrierA));
	CheckBarrierPresentation(*this, Outer, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroupBarrierTopologyTest, "Outlier.Room.GroupBarrier.TopologyAndLegacy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGroupBarrierTopologyTest::RunTest(const FString& Parameters)
{
	FGroupBarrierWorld Test;
	Test.AddRoom(BarrierA);
	Test.AddRoom(BarrierOther);
	Test.AddRoom(BarrierLegacy);
	AEnemyBase* A = Test.AddEnemy(BarrierA);
	AEnemyBase* Other = Test.AddEnemy(BarrierOther);
	AEnemyBase* Legacy = Test.AddEnemy(BarrierLegacy);
	ARoomCombatBarrier* Cross = Test.AddBarrier(BarrierA, BarrierOther);
	ARoomCombatBarrier* Prefix = Test.AddBarrier(BarrierPrefix);
	ARoomCombatBarrier* Level = Test.AddBarrier(BarrierOtherLevel);
	ARoomCombatBarrier* LegacyBarrier = Test.AddBarrier(BarrierLegacy);
	ARoomCombatBarrier* Shared = Test.AddBarrier(BarrierLegacy, FGameplayTag(), BarrierA);
	TestFalse(TEXT("Different groups form an exterior boundary"), Cross->IsGroupInternalPassage());
	TestTrue(TEXT("Connected endpoint is served"), Cross->ServesRoom(BarrierOther));
	TestTrue(TEXT("A starts"), Test.Start(BarrierA));
	CheckBarrierPresentation(*this, Cross, true);
	CheckBarrierPresentation(*this, Prefix, false);
	CheckBarrierPresentation(*this, Level, false);
	CheckBarrierPresentation(*this, LegacyBarrier, false);
	CheckBarrierPresentation(*this, Shared, true);
	Test.Combat->NotifyEnemyDefeated(A);
	CheckBarrierPresentation(*this, Cross, false);
	TestTrue(TEXT("Other group can start later"), Test.Start(BarrierOther));
	CheckBarrierPresentation(*this, Cross, true);
	Test.Combat->NotifyEnemyDefeated(Other);
	CheckBarrierPresentation(*this, Cross, false);
	TestTrue(TEXT("Legacy room starts without group expansion"), Test.Start(BarrierLegacy));
	TestTrue(TEXT("Legacy group query falls back to room state"), Test.Combat->IsGroupExitBlocked(BarrierLegacy));
	CheckBarrierPresentation(*this, LegacyBarrier, true);
	CheckBarrierPresentation(*this, Shared, true);
	Test.Combat->NotifyEnemyDefeated(Legacy);
	CheckBarrierPresentation(*this, Shared, false);
	return true;
}

#endif
