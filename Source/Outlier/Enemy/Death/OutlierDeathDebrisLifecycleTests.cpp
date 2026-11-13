#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Enemy/Death/OutlierDeathDebrisActor.h"
#include "Enemy/EnemyBase.h"
#include "Components/CapsuleComponent.h"
#include "CoreGlobals.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "Engine/NetConnection.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "Misc/AutomationTest.h"
#include "Network/OutlierArenaSubsystem.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

namespace
{
struct FScopedDeathDebrisTestWorld
{
	UWorld* World = nullptr;
	~FScopedDeathDebrisTestWorld()
	{
		if (!World) return;
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierDeathDebrisLifecycleTest,
	"Outlier.Enemy.DeathDebris.GenerationLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierDeathDebrisLifecycleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FScopedDeathDebrisTestWorld Fixture;
	Fixture.World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("DeathDebrisLifecycle")));
	UWorld* World = Fixture.World;
	if (!TestNotNull(TEXT("Death debris world exists"), World)) return false;
	World->AddToRoot();
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	if (!TestTrue(TEXT("Debris fixture creates its authority game mode"), World->SetGameMode(FURL()))) return false;
	World->InitializeActorsForPlay(FURL());
	UOutlierArenaSubsystem* Arena = World->GetSubsystem<UOutlierArenaSubsystem>();
	FObjectPropertyBase* LoadedLevelProperty = FindFProperty<FObjectPropertyBase>(
		ULevelStreaming::StaticClass(), TEXT("LoadedLevel"));
	if (!TestNotNull(TEXT("Arena subsystem exists"), Arena)
		|| !TestNotNull(TEXT("Streaming fixture can supply loaded levels"), LoadedLevelProperty)) return false;

	// 스트리밍 I/O만 격리한다. 파편 정리는 실제 예약/확정과 Enemy 사망 진입점을 사용한다.
	ULevelStreamingDynamic* Base = NewObject<ULevelStreamingDynamic>(World);
	ULevelStreamingDynamic* Gameplay = NewObject<ULevelStreamingDynamic>(World);
	ULevel* GameplayLevel = NewObject<ULevel>(World);
	GameplayLevel->bIsVisible = true;
	LoadedLevelProperty->SetObjectPropertyValue_InContainer(Base, World->PersistentLevel);
	LoadedLevelProperty->SetObjectPropertyValue_InContainer(Gameplay, GameplayLevel);
	Arena->Arena.StreamingLevel = Base;
	Arena->GameplayStreamingLevels = { Gameplay };
	Arena->GameplaySublevels = { TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Tests/DebrisGameplay.DebrisGameplay"))) };
	Arena->bGameplayLevelsResolved = true;
	Arena->bInitialGameplayLevelsRequested = true;
	UGeometryCollection* Collection = NewObject<UGeometryCollection>(World);
	AEnemyBase* Source = World->SpawnActor<AEnemyBase>();
	AActor* Unrelated = World->SpawnActor<AActor>();
	AOutlierDeathDebrisActor* IndefiniteDebris = World->SpawnActor<AOutlierDeathDebrisActor>();
	if (!TestNotNull(TEXT("Death source exists"), Source)
		|| !TestNotNull(TEXT("Unrelated actor exists"), Unrelated)
		|| !TestNotNull(TEXT("Indefinite debris exists"), IndefiniteDebris)) return false;

	// 실제 Chaos 분해/표시는 별도 멀티플레이 검증 대상이다. 여기서는 동일한 파편 Actor의 수명을 검증한다.
	IndefiniteDebris->SetLifeSpan(0.0f);
	TestTrue(TEXT("Initial generation accepts its debris actor"), Arena->RegisterDeathDebrisActor(IndefiniteDebris, 0));
	Source->SetDeathDebrisForPoolTesting(Collection, 0.5f);
	Source->BeginDeathForPoolTesting();
	const FTimerHandle DeathTimer = Source->GetDeathDebrisTimerForPoolTesting();
	TestTrue(TEXT("Actual death producer schedules initial-generation debris"), World->GetTimerManager().IsTimerActive(DeathTimer));
	int32 CallbackExecutions = 0;
	const FTimerHandle IndependentTimer = Arena->ScheduleDeathDebris(0, 0.5f,
		FTimerDelegate::CreateLambda([&]() { ++CallbackExecutions; }));
	TestTrue(TEXT("Same-generation callback is registered"), World->GetTimerManager().IsTimerActive(IndependentTimer));
	AOutlierDeathDebrisActor* ExpiredDebris = World->SpawnActor<AOutlierDeathDebrisActor>();
	if (TestNotNull(TEXT("Naturally removed debris exists"), ExpiredDebris))
	{
		TestTrue(TEXT("Natural removal fixture is registered"), Arena->RegisterDeathDebrisActor(ExpiredDebris, 0));
		ExpiredDebris->DispatchBeginPlay();
		ExpiredDebris->Destroy();
		TestFalse(TEXT("Actor EndPlay unregisters debris immediately"), Arena->DeathDebrisActors.Contains(ExpiredDebris));
	}
	const uint32 Cancelled = Arena->ReserveGameplayGeneration();
	TestTrue(TEXT("Ready world can reserve a generation"), Cancelled != 0);
	Arena->CancelGameplayGeneration(Cancelled);
	TestTrue(TEXT("Reservation cancellation preserves old debris"), IsValid(IndefiniteDebris));
	TestTrue(TEXT("Reservation cancellation preserves old request"), World->GetTimerManager().IsTimerActive(DeathTimer));
	TestFalse(TEXT("Cancelled generation cannot commit"), Arena->CommitGameplayGeneration(Cancelled));

	const uint32 FirstGeneration = Arena->ReserveGameplayGeneration();
	TestTrue(TEXT("New generation commits through the production transition"), Arena->CommitGameplayGeneration(FirstGeneration));
	TestFalse(TEXT("Generation commit cancels actual death request"), World->GetTimerManager().IsTimerActive(DeathTimer));
	TestFalse(TEXT("Generation commit cancels independent delayed request"), World->GetTimerManager().IsTimerActive(IndependentTimer));
	TestTrue(TEXT("Generation commit destroys lifetime-zero debris"), IndefiniteDebris->IsActorBeingDestroyed());
	TestTrue(TEXT("Commit releases pending request registrations"), Arena->PendingDeathDebris.IsEmpty());
	TestTrue(TEXT("Commit releases debris actor registrations"), Arena->DeathDebrisActors.IsEmpty());
	TestFalse(TEXT("Unrelated actors are preserved"), Unrelated->IsActorBeingDestroyed());
	TestTrue(TEXT("Initial generation is now stale"), !Arena->IsDeathDebrisGenerationCurrent(0));
	TestFalse(TEXT("Duplicate commit cannot re-enter cleanup"), Arena->CommitGameplayGeneration(FirstGeneration));

	// 이전 RPC를 확정 뒤에 다시 전달해도 새 요청과 원본 충돌 상태를 건드리지 않아야 한다.
	Source->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	const uint32 SerialBeforeStaleRPC = Source->DeathDebrisPresentationSerial;
	Source->MulticastPlayDeathDebris_Implementation(0, 0, Source->GetActorTransform(), FVector::ZeroVector);
	TestFalse(TEXT("Late multicast cannot re-arm the old timer"), World->GetTimerManager().IsTimerActive(Source->GetDeathDebrisTimerForPoolTesting()));
	TestEqual(TEXT("Late multicast cannot disable the current source collision"),
		Source->GetCapsuleComponent()->GetCollisionEnabled(), ECollisionEnabled::QueryAndPhysics);
	TestEqual(TEXT("Late multicast cannot replace the source presentation serial"), Source->DeathDebrisPresentationSerial, SerialBeforeStaleRPC);
	const FTimerHandle StaleTimer = Arena->ScheduleDeathDebris(0, 0.5f,
		FTimerDelegate::CreateLambda([&]() { ++CallbackExecutions; }));
	TestFalse(TEXT("Stale callback cannot be scheduled after commit"), StaleTimer.IsValid());
	AOutlierDeathDebrisActor* StaleDebris = World->SpawnActor<AOutlierDeathDebrisActor>();
	TestFalse(TEXT("Stale spawned debris cannot join the new generation"), Arena->RegisterDeathDebrisActor(StaleDebris, 0));
	if (StaleDebris) StaleDebris->Destroy();

	for (int32 Transition = 0; Transition < 3; ++Transition)
	{
		const uint32 CurrentGeneration = Arena->GetGameplayGeneration();
		AEnemyBase* NewSource = World->SpawnActor<AEnemyBase>();
		AOutlierDeathDebrisActor* CurrentDebris = World->SpawnActor<AOutlierDeathDebrisActor>();
		if (!TestNotNull(TEXT("Fresh death source exists"), NewSource)
			|| !TestNotNull(TEXT("Fresh debris actor exists"), CurrentDebris)) return false;
		TestEqual(TEXT("Fresh placed source captures the current generation"), uint32(NewSource->GetCombatGameplayGeneration()), CurrentGeneration);
		NewSource->SetDeathDebrisForPoolTesting(Collection, 0.5f);
		NewSource->BeginDeathForPoolTesting();
		const FTimerHandle NewDeathTimer = NewSource->GetDeathDebrisTimerForPoolTesting();
		TestTrue(TEXT("Fresh generation permits normal death requests"), World->GetTimerManager().IsTimerActive(NewDeathTimer));
		TestTrue(TEXT("Fresh generation permits its debris"), Arena->RegisterDeathDebrisActor(CurrentDebris, CurrentGeneration));
		Arena->PendingGameplayReload.Reset();
		const uint32 Next = Arena->ReserveGameplayGeneration();
		TestTrue(TEXT("Repeated transition reserves a fresh token"), Next != 0 && Next != CurrentGeneration);
		TestTrue(TEXT("Repeated transition commits"), Arena->CommitGameplayGeneration(Next));
		TestFalse(TEXT("Repeated transition cancels fresh request"), World->GetTimerManager().IsTimerActive(NewDeathTimer));
		TestTrue(TEXT("Repeated transition destroys only tracked debris"), CurrentDebris->IsActorBeingDestroyed());
		TestFalse(TEXT("Unrelated actor survives repeated transitions"), Unrelated->IsActorBeingDestroyed());
	}

	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	++GFrameCounter;
	World->GetTimerManager().Tick(1.0f);
	TestEqual(TEXT("Cancelled callback never executes after transition"), CallbackExecutions, 0);
	Arena->PendingGameplayReload.Reset();
	const uint32 CurrentGeneration = Arena->GetGameplayGeneration();
	AEnemyBase* ReusedSource = World->SpawnActor<AEnemyBase>();
	if (TestNotNull(TEXT("Reused source exists for delayed old-lease RPC"), ReusedSource))
	{
		// 대여 상태 복제만 격리해 이전 lease의 RPC가 새 원본 상태를 바꾸지 않는 실제 수신 경로를 검사한다.
		ReusedSource->PoolState = EEnemyPoolState::CombatActive;
		ReusedSource->PoolGameplayGeneration = int32(CurrentGeneration);
		ReusedSource->PoolLeaseSerial = 2;
		ReusedSource->SetDeathDebrisForPoolTesting(Collection, 0.5f);
		ReusedSource->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		const uint32 SerialBeforeOldLeaseRPC = ReusedSource->DeathDebrisPresentationSerial;
		ReusedSource->MulticastPlayDeathDebris_Implementation(CurrentGeneration, 1, ReusedSource->GetActorTransform(), FVector::ZeroVector);
		TestTrue(TEXT("Same-generation old lease still schedules its cosmetic debris"),
			World->GetTimerManager().IsTimerActive(ReusedSource->GetDeathDebrisTimerForPoolTesting()));
		TestEqual(TEXT("Old lease RPC preserves new source collision"), ReusedSource->GetCapsuleComponent()->GetCollisionEnabled(), ECollisionEnabled::QueryAndPhysics);
		TestEqual(TEXT("Old lease RPC preserves new source serial"), ReusedSource->DeathDebrisPresentationSerial, SerialBeforeOldLeaseRPC);
		Arena->ClearDeathDebris();
	}
	const FTimerHandle CompletedTimer = Arena->ScheduleDeathDebris(CurrentGeneration, 0.01f,
		FTimerDelegate::CreateLambda([&]() { ++CallbackExecutions; }));
	TestTrue(TEXT("New generation schedules a normal callback"), CompletedTimer.IsValid());
	++GFrameCounter;
	World->GetTimerManager().Tick(0.02f);
	++GFrameCounter;
	World->GetTimerManager().Tick(0.02f);
	TestEqual(TEXT("New generation callback executes exactly once"), CallbackExecutions, 1);
	TestFalse(TEXT("Completed callback releases its timer"), World->GetTimerManager().IsTimerActive(CompletedTimer));
	TestTrue(TEXT("Completed callback releases its registration"), Arena->PendingDeathDebris.IsEmpty());
	// 리로드가 끝난 뒤 새로 접속한 클라이언트도 서버가 보낸 현재 세대의 파편을 수락해야 한다.
	// 소켓/Travel만 격리하고 실제 로딩 RPC 수신 경로를 실행한다.
	UClass* DriverClass = LoadClass<UNetDriver>(nullptr, TEXT("/Script/OnlineSubsystemUtils.IpNetDriver"));
	UClass* ConnectionClass = LoadClass<UNetConnection>(nullptr, TEXT("/Script/OnlineSubsystemUtils.IpConnection"));
	if (TestNotNull(TEXT("Client driver class exists"), DriverClass)
		&& TestNotNull(TEXT("Client connection class exists"), ConnectionClass))
	{
		AFirstPersonPlayerController* Controller = World->SpawnActor<AFirstPersonPlayerController>();
		UNetDriver* Driver = NewObject<UNetDriver>(World, DriverClass);
		Driver->ServerConnection = NewObject<UNetConnection>(Driver, ConnectionClass);
		World->SetNetDriver(Driver);
		TestEqual(TEXT("Reconnect fixture is a client"), World->GetNetMode(), NM_Client);
		Arena->GameplayGeneration = 0;
		if (TestNotNull(TEXT("Reconnect controller exists"), Controller))
		{
			Controller->ClientArenaLoad_Implementation(FVector::ZeroVector, 0, CurrentGeneration);
			TestTrue(TEXT("Normal reconnect adopts authoritative debris generation"),
				Arena->IsDeathDebrisGenerationCurrent(CurrentGeneration));
			Controller->ClientPrepareForArenaExit_Implementation();
		}
		World->SetNetDriver(nullptr);
		Driver->ServerConnection = nullptr;
	}
	return true;
}

#endif
