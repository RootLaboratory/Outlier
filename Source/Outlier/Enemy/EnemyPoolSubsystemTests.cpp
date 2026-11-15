#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/AutoTurret.h"
#include "Enemy/EnemyBase.h"
#include "Drone/Partner/HackableComponent.h"
#include "Drone/Partner/HackGameplayTags.h"
#include "Drone/Partner/PartnerHackComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerPlayerController.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Misc/ScopeExit.h"
#include "Enemy/EnemyPoolDefinition.h"
#include "Enemy/EnemyPoolSubsystem.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "Enemy/EnemyStateTreeTacticalTasks.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StateTreeComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "Misc/AutomationTest.h"
#include "Network/OutlierArenaSubsystem.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif
#include "Room/RoomTagComponent.h"
#include "Room/RoomCombatSpawnPoint.h"
#include "StateTree.h"
#include "TimerManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyPoolRuntimeTest,
	"Outlier.Enemy.PoolRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyPoolHackStateTest, "Outlier.Enemy.PoolHackState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyPoolHackStateTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("PoolHackState")), GetTransientPackage());
	if (!TestNotNull(TEXT("Pool hack world"), World)) return false;
	World->AddToRoot();
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	World->SetGameMode(FURL());
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	UEnemyPoolSubsystem* Pool = World->GetSubsystem<UEnemyPoolSubsystem>();
	if (!TestNotNull(TEXT("Pool subsystem"), Pool)) return false;
	FActorSpawnParameters PartnerSpawn;
	PartnerSpawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APartnerCharacter* Partner = World->SpawnActor<APartnerCharacter>(APartnerCharacter::StaticClass(), FTransform::Identity, PartnerSpawn);
	APartnerPlayerController* Controller = World->SpawnActor<APartnerPlayerController>();
	if (!TestNotNull(TEXT("Partner"), Partner) || !TestNotNull(TEXT("Partner controller"), Controller)) return false;
	Partner->GetOutlierAbilitySystemComponent()->InitializeForPawn(Partner);
	Partner->GetOutlierAbilitySystemComponent()->InitializeVitalityToSelf(100.f);
	FOutlierPartnerAbilityConfig AbilityConfig;
	AbilityConfig.HackCooldown = 30.f;
	if (!TestTrue(TEXT("Partner hack abilities configure"), Partner->GetOutlierAbilitySystemComponent()->ConfigurePartnerAbilities(AbilityConfig))) return false;
	Controller->Possess(Partner);
	UPartnerHackComponent* Requests = NewObject<UPartnerHackComponent>(Partner);
	Requests->RefreshCharacterRefsFromPlayerState();
	const FGameplayTag Possessable = HackGameplayTags::Target::Possessable();
	const FGameplayTag NonPossessable = HackGameplayTags::Target::NonPossessable();
	const FGameplayTag Locked = OutlierGameplayTags::State::Locked();
	const FGameplayTag Hacked = OutlierGameplayTags::State::HackedOnce();
	const FGameplayTag Multiple = HackGameplayTags::Use::Multiple();

	// 초기 인스턴스 설정은 deferred 생성 중 주입한다. 실제 풀의 기준 캡처/복원은 우회하지 않는다.
	for (int32 Variant = 0; Variant < 3; ++Variant)
	{
		Pool->DestroyPool();
		FGameplayTagContainer InitialTags;
		InitialTags.AddTag(Variant == 1 ? NonPossessable : Possessable);
		if (Variant == 2) InitialTags.AddTag(Locked);
		InitialTags.AddTag(Multiple);
		InitialTags.AddTag(Hacked);
		FGameplayTagContainer Expected = InitialTags;
		Expected.RemoveTag(Hacked);
		const FDelegateHandle SpawnHandle = World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda(
			[InitialTags](AActor* Actor)
			{
				if (AEnemyBase* Enemy = Cast<AEnemyBase>(Actor)) Enemy->GetHackableComponent()->HackTags = InitialTags;
			}));
		UEnemyPoolDefinition* Definition = NewObject<UEnemyPoolDefinition>(World);
		FEnemyPoolEntry& Entry = Definition->Entries.AddDefaulted_GetRef();
		Entry.EnemyClass = AEnemyBase::StaticClass();
		Entry.PrewarmCount = Entry.MaxCount = 1;
		const bool bPrewarmed = Pool->PrewarmPool(Definition);
		World->RemoveOnActorSpawnedHandler(SpawnHandle);
		if (!TestTrue(TEXT("Prewarm"), bPrewarmed)) return false;
		AEnemyBase* PreviousEnemy = nullptr;
		for (int32 Cycle = 0; Cycle < 3; ++Cycle)
		{
			FEnemyPoolLeaseContext Context;
			Context.GameplayGeneration = 50 + Cycle;
			AEnemyBase* Enemy = Pool->LeaseEnemy(AEnemyBase::StaticClass(), FTransform::Identity, Context);
			if (!TestNotNull(TEXT("Leased enemy"), Enemy)) return false;
			if (PreviousEnemy) TestTrue(TEXT("Same Actor reused"), PreviousEnemy == Enemy);
			PreviousEnemy = Enemy;
			UHackableComponent* Hackable = Enemy->GetHackableComponent();
			TestTrue(TEXT("New lease restores exact authored tags except HackedOnce"), Hackable->HackTags == Expected);
			FHackQueryContext Query;
			Query.BlockedTags.AddTag(Locked);
			TestEqual(TEXT("Only originally locked variant is blocked"), Hackable->CanBeHackTarget(Query), Variant != 2);
			const int32 Lease = Enemy->GetPoolLeaseSerial();
			if (Variant == 0)
			{
				Enemy->GetOutlierAbilitySystemComponent()->InitializeVitalityToSelf(100.f);
				// 실제 Enemy 시작 → Partner 성공 처리 → 빙의 전환 대기까지 매 대여에서 다시 통과한다.
				FHackQueryContext Start;
				Start.InstigatorActor = Partner;
				Start.RequestId = 6;
				Requests->SetActiveHackableComponent(Hackable);
				Requests->ActiveHackRequestId = Start.RequestId;
				Enemy->HandleHackStarted(Start);
				TestTrue(TEXT("Reused enemy accepts a real possession hack start"), Enemy->IsPossessionInProgress());
				FHackResultContext Success;
				Success.TargetActor = Enemy;
				Success.InstigatorActor = Partner;
				Success.RequestId = Start.RequestId;
				Success.Result = EHackResult::Success;
				Requests->CompleteActiveHack(Success, false);
				TestTrue(TEXT("Real successful hack records its history"), Hackable->HasHackTag(Hacked));
				TestTrue(TEXT("Hack success enters the controller transition"), Controller->ShouldDeferPartnerHackRecharge(Partner));
				Controller->ServerNotifyHackTransitionCovered_Implementation(Enemy, Context.GameplayGeneration, Lease - 1);
				TestTrue(TEXT("An old covered RPC cannot commit the new transition"), Controller->GetPawn() == Partner);
				TestTrue(TEXT("An old covered RPC does not cancel the new transition"), Controller->ShouldDeferPartnerHackRecharge(Partner));
				// 첫 회는 전환 대기 중 반환하고, 다음 회부터는 실제 빙의 완료 뒤 반환한다.
				if (Cycle > 0)
				{
					Controller->CommitPendingEnemyPossession(Enemy);
					TestTrue(TEXT("Real hack commit possesses the reused enemy"), Controller->GetPawn() == Enemy);
					Controller->ServerNotifyHackPossessionRevealFinished_Implementation(Enemy, Context.GameplayGeneration, Lease - 1);
					TestFalse(TEXT("An old reveal RPC cannot start the new effect"), Enemy->IsHackEffectActive());
					Controller->ServerNotifyHackPossessionRevealFinished_Implementation(Enemy, Context.GameplayGeneration, Lease);
					TestTrue(TEXT("Current reveal starts the new lifetime"), Enemy->IsHackEffectActive());
				}
			}
			Hackable->MarkAsHackedOnce();
			TestFalse(TEXT("One-use query rejects a second hack in the same lease"), Hackable->CanBeHackTarget(Query));
			Query.HackMultiUseTags.AddTag(Multiple);
			TestEqual(TEXT("Authored multiple-use policy survives"), Hackable->CanBeHackTarget(Query), Variant != 2);
			Hackable->HackTags.RemoveTag(Possessable);
			Hackable->HackTags.AddTag(NonPossessable);
			Hackable->HackTags.AddTag(Locked);
			Hackable->InitializePoolHackState();
			TestTrue(TEXT("Repeated capture does not erase runtime history"), Hackable->HasHackTag(Hacked));

			// 실전 요청 경계와 같은 수명 스냅샷을 잡고, 반환 뒤 늦은 결과를 제출한다.
			Requests->SetActiveHackableComponent(Hackable);
			Requests->ActiveHackRequestId = 7;
			bool bUnavailable = false;
			const FDelegateHandle Unavailable = Hackable->OnHackTargetUnavailable.AddLambda([&](UHackableComponent*)
			{
				bUnavailable = true;
				TestFalse(TEXT("Returning enemy immediately rejects hacks"), Hackable->CanBeHackTarget(FHackQueryContext()));
				TestFalse(TEXT("Nested return is rejected"), Pool->ReturnEnemy(Enemy, Context.GameplayGeneration, Lease));
				TestNull(TEXT("Returning actor cannot be leased from its full bucket"),
					Pool->LeaseEnemy(AEnemyBase::StaticClass(), FTransform::Identity, Context));
			});
			TestTrue(TEXT("Real pool return"), Pool->ReturnEnemy(Enemy, Context.GameplayGeneration, Lease));
			Hackable->OnHackTargetUnavailable.Remove(Unavailable);
			TestTrue(TEXT("Return publishes unavailable"), bUnavailable);
			TestTrue(TEXT("Return restores baseline after callbacks"), Hackable->HackTags == Expected);
			TestFalse(TEXT("Return clears the hack effect"), Enemy->IsHackEffectActive());
			TestTrue(TEXT("Return restores Partner control"), Controller->GetPawn() == Partner);
			TestFalse(TEXT("Return clears pending possession"), Enemy->IsPossessionInProgress());
			TestFalse(TEXT("Idle cannot be hacked despite clean tags"), Hackable->CanBeHackTarget(FHackQueryContext()));
			TestFalse(TEXT("Duplicate return is rejected"), Pool->ReturnEnemy(Enemy, Context.GameplayGeneration, Lease));
			FHackResultContext StaleResult;
			StaleResult.RequestId = 7;
			StaleResult.TargetActor = Enemy;
			StaleResult.Result = EHackResult::Success;
			Requests->CompleteActiveHack(StaleResult, false);
			TestNull(TEXT("Stale request releases its target"), Requests->ActiveHackableComponent.Get());
			TestTrue(TEXT("Stale success cannot rewrite returned tags"), Hackable->HackTags == Expected);
			Enemy->SetPoolPresentationAutoCompleteForTesting(false);
			AEnemyBase* Presenting = Pool->LeaseEnemy(AEnemyBase::StaticClass(), FTransform::Identity, Context);
			TestTrue(TEXT("Presentation uses the same Actor"), Presenting == Enemy);
			TestFalse(TEXT("Spawn presentation cannot be hacked"), Hackable->CanBeHackTarget(FHackQueryContext()));
			Requests->ServerHeldPoolEnemy = Enemy;
			Requests->ServerHoldGeneration = Context.GameplayGeneration;
			Requests->ServerHoldLease = Lease;
			TestFalse(TEXT("Old hold cannot be maintained for the new lease"), Requests->IsServerHoldLifetimeCurrent());
			Requests->SetActiveHackableComponent(Hackable);
			Requests->ActiveHackRequestId = 8;
			Requests->CompleteActiveHack(StaleResult, false);
			TestEqual(TEXT("Old result cannot cancel a new request"), Requests->ActiveHackRequestId, uint32(8));
			Requests->SetActiveHackableComponent(Hackable);
			Requests->ActiveHackRequestId = 7;
			Requests->ActiveHackGeneration = Context.GameplayGeneration;
			Requests->ActiveHackLease = Lease;
			Requests->CompleteActiveHack(StaleResult, false);
			TestTrue(TEXT("Old success cannot write history to the new lease"), Hackable->HackTags == Expected);
			TestFalse(TEXT("Old result cannot start new possession"), Enemy->IsPossessionInProgress());
			TestFalse(TEXT("Old lease cannot return presentation"), Pool->ReturnEnemy(Enemy, Context.GameplayGeneration, Lease));
			Enemy->CompletePoolSpawnPresentation(Context.GameplayGeneration, Enemy->GetPoolLeaseSerial());
			TestTrue(TEXT("Return presentation lease"), Pool->ReturnEnemy(Enemy, Context.GameplayGeneration, Enemy->GetPoolLeaseSerial()));
			Enemy->SetPoolPresentationAutoCompleteForTesting(true);
		}
	}
	return true;
}

bool FEnemyPoolRuntimeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FName WorldName = MakeUniqueObjectName(
		nullptr,
		UWorld::StaticClass(),
		NAME_None,
		EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Enemy pool runtime world is created"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}

	World->AddToRoot();
	WorldContext.SetCurrentWorld(World);
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	TestTrue(TEXT("Enemy pool runtime world creates an authority game mode"),
		World->SetGameMode(FURL()));
	World->InitializeActorsForPlay(FURL());
	auto CleanupWorld = [World]()
	{
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};

	UEnemyPoolSubsystem* Pool = World->GetSubsystem<UEnemyPoolSubsystem>();
	if (!TestNotNull(TEXT("Enemy pool subsystem is created"), Pool))
	{
		CleanupWorld();
		return false;
	}

	UEnemyPoolDefinition* Definition = NewObject<UEnemyPoolDefinition>(World);
	FEnemyPoolEntry& Entry = Definition->Entries.AddDefaulted_GetRef();
	Entry.EnemyClass = AEnemyBase::StaticClass();
	Entry.PrewarmCount = 1;
	Entry.MaxCount = 2;
	TestTrue(TEXT("Configured pool prewarms"), Pool->PrewarmPool(Definition));
	TestEqual(TEXT("Prewarm creates one Enemy"), Pool->GetTotalCount(AEnemyBase::StaticClass()), 1);
	TestEqual(TEXT("Prewarmed Enemy is idle"), Pool->GetIdleCount(AEnemyBase::StaticClass()), 1);

	const FGameplayTag RoomTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")));
	const FVector SharedTargetLocation(700.0, 800.0, 900.0);
	UEnemyRoomSubsystem* RoomSubsystem = World->GetSubsystem<UEnemyRoomSubsystem>();
	if (TestNotNull(TEXT("Enemy room subsystem is created"), RoomSubsystem))
	{
		RoomSubsystem->SetActiveRoomTargetForTesting(RoomTag, SharedTargetLocation);
		TestTrue(TEXT("Room target fixture marks the room in combat"),
			RoomSubsystem->IsRoomInCombat(RoomTag));
	}
	FEnemyPoolLeaseContext FirstContext;
	FirstContext.RoomTag = RoomTag;
	FirstContext.CombatPhaseIndex = 1;
	FirstContext.WaveIndex = 2;
	FirstContext.GameplayGeneration = 10;
	AEnemyBase* FirstLease = Pool->LeaseEnemy(
		AEnemyBase::StaticClass(),
		FTransform(FVector(100.0, 200.0, 300.0)),
		FirstContext);
	if (!TestNotNull(TEXT("Prewarmed Enemy can be leased"), FirstLease))
	{
		CleanupWorld();
		return false;
	}

	TestEqual(TEXT("Default presentation enters combat"),
		FirstLease->GetEnemyPoolState(), EEnemyPoolState::CombatActive);
	TestTrue(TEXT("Combat-active Enemy accepts damage"), FirstLease->CanBeDamaged());
	TestTrue(TEXT("Combat-active Enemy enables collision"), FirstLease->GetActorEnableCollision());
	TestEqual(TEXT("Lease applies runtime RoomTag"), FirstLease->GetDefaultRoomTag(), RoomTag);
	TestEqual(TEXT("Reinforcement joins the active room without direct perception"),
		FirstLease->GetCombatState(), EEnemyCombatState::Combat);
	TestTrue(TEXT("Reinforcement inherits the active shared target"),
		FirstLease->HasSharedTargetContact());
	TestEqual(TEXT("Reinforcement remembers the shared target location"),
		FirstLease->GetSharedTargetLocation(), SharedTargetLocation);
	TestFalse(TEXT("Shared contact does not invent direct sight"), FirstLease->IsPlayerCurrentlyVisible());
	const ECollisionEnabled::Type CapsuleCollision = FirstLease->GetCapsuleComponent()->GetCollisionEnabled();
	const ECollisionEnabled::Type MeshCollision = FirstLease->GetMesh()->GetCollisionEnabled();
	const ECollisionEnabled::Type CoreCollision = FirstLease->GetCoreHitboxComponent()->GetCollisionEnabled();
	const bool bMeshVisible = FirstLease->GetMesh()->IsVisible();
	const bool bMeshHiddenInGame = FirstLease->GetMesh()->bHiddenInGame;
	const bool bCoreVisible = FirstLease->GetCoreHitboxComponent()->IsVisible();
	FirstLease->SetNextFlightPathRequestTime(123.0);
	FirstLease->SetNextEnvQueryRequestTime(124.0);
	TestEqual(TEXT("EQS request throttle survives transient Task replacement"),
		FirstLease->GetNextEnvQueryRequestTime(), 124.0);
	TestEqual(TEXT("Flight request throttle belongs to Pawn, not transient Task"),
		FirstLease->GetNextFlightPathRequestTime(), 123.0);
	FirstLease->SimulateDeathPresentationForPoolTesting();
	TestFalse(TEXT("Death presentation hides the source mesh"), FirstLease->GetMesh()->IsVisible());
	TestEqual(TEXT("Death presentation disables the capsule"),
		FirstLease->GetCapsuleComponent()->GetCollisionEnabled(), ECollisionEnabled::NoCollision);

	const int32 FirstLeaseSerial = FirstLease->GetPoolLeaseSerial();
	TestTrue(TEXT("Active Enemy returns to the pool"),
		Pool->ReturnEnemy(FirstLease, FirstContext.GameplayGeneration, FirstLeaseSerial));
	TestEqual(TEXT("Returned Enemy is idle"), FirstLease->GetEnemyPoolState(), EEnemyPoolState::Idle);
	TestEqual(TEXT("Pool return clears previous flight request throttle"),
		FirstLease->GetNextFlightPathRequestTime(), 0.0);
	TestEqual(TEXT("Pool return clears previous EQS request throttle"),
		FirstLease->GetNextEnvQueryRequestTime(), 0.0);
	TestTrue(TEXT("Returned Enemy is hidden"), FirstLease->IsHidden());
	TestFalse(TEXT("Returned Enemy cannot be damaged"), FirstLease->CanBeDamaged());
	TestFalse(TEXT("Returned Enemy collision is disabled"), FirstLease->GetActorEnableCollision());
	TestFalse(TEXT("Returned Enemy clears RoomTag"), FirstLease->GetDefaultRoomTag().IsValid());

	FirstLease->SetPoolPresentationAutoCompleteForTesting(false);
	// 현재 공유가 끊겨도 재사용된 증원은 방의 마지막 전투 위치를 받아 수색에 합류한다.
	if (RoomSubsystem)
	{
		RoomSubsystem->SetActiveRoomTargetForTesting(RoomTag, SharedTargetLocation, false);
	}
	FEnemyPoolLeaseContext SecondContext = FirstContext;
	SecondContext.GameplayGeneration = 11;
	SecondContext.CombatPhaseIndex = 3;
	SecondContext.WaveIndex = 4;
	AEnemyBase* SecondLease = Pool->LeaseEnemy(
		AEnemyBase::StaticClass(),
		FTransform(FVector(400.0, 500.0, 600.0)),
		SecondContext);
	TestTrue(TEXT("The same Actor is reused"), SecondLease == FirstLease);
	TestEqual(TEXT("Enemy waits for the explicit spawn callback"),
		SecondLease->GetEnemyPoolState(), EEnemyPoolState::SpawnPresentation);
	TestEqual(TEXT("Spawn presentation does not enter Alert before activation"),
		SecondLease->GetCombatState(), EEnemyCombatState::NonCombat);
	TestFalse(TEXT("Spawn presentation blocks damage"), SecondLease->CanBeDamaged());
	TestFalse(TEXT("Spawn presentation blocks collision"), SecondLease->GetActorEnableCollision());
	const UCapsuleComponent* PresentingCapsule = SecondLease->GetCapsuleComponent();
	const FVector PresentingCenter = PresentingCapsule->GetComponentLocation();
	const float PresentingRadius = PresentingCapsule->GetScaledCapsuleRadius();
	const float PresentingHalfHeight = PresentingCapsule->GetScaledCapsuleHalfHeight();
	TestTrue(TEXT("Collision-disabled presentation still occupies spawn space"),
		Pool->IsSpawnPresentationSpaceOccupied(PresentingCenter, FQuat::Identity,
			PresentingRadius, PresentingHalfHeight));
	TestFalse(TEXT("Separated capsule remains available"),
		Pool->IsSpawnPresentationSpaceOccupied(PresentingCenter + FVector(4.0f * PresentingRadius, 0.0, 0.0),
			FQuat::Identity, PresentingRadius, PresentingHalfHeight));
	TestFalse(TEXT("Capsules separated vertically do not reserve each other"),
		Pool->IsSpawnPresentationSpaceOccupied(PresentingCenter + FVector(0.0, 0.0, 4.0f * PresentingHalfHeight),
			FQuat::Identity, PresentingRadius, PresentingHalfHeight));
	ARoomCombatSpawnPoint* OverlappingPoint = World->SpawnActor<ARoomCombatSpawnPoint>();
	if (TestNotNull(TEXT("Overlapping spawn point exists"), OverlappingPoint))
	{
		OverlappingPoint->SetSpawnSearchForTesting(0.0f, 0.0f);
		OverlappingPoint->SetActorLocation(PresentingCenter);
		FTransform SpawnTransform;
		TestFalse(TEXT("No alternate location leaves spawn request pending"),
			OverlappingPoint->FindSpawnTransform(AEnemyBase::StaticClass(), 42, SpawnTransform));
		OverlappingPoint->SetSpawnSearchForTesting(300.0f, 0.0f);
		const bool bFoundAlternative = OverlappingPoint->FindSpawnTransform(
			AEnemyBase::StaticClass(), 42, SpawnTransform);
		TestTrue(TEXT("Spawn point can select an alternative location"), bFoundAlternative);
		if (bFoundAlternative)
		{
			TestFalse(TEXT("Alternative does not overlap presenting enemy"),
				Pool->IsSpawnPresentationSpaceOccupied(SpawnTransform.GetLocation(), SpawnTransform.GetRotation(),
				PresentingRadius, PresentingHalfHeight));
		}
	}
	TestFalse(TEXT("Previous Dead state is not retained"), SecondLease->IsDead());
	// 연출 대기 중에는 Actor 충돌이 꺼져 GetCollisionEnabled가 NoCollision을 반환한다.
	// 컴포넌트 충돌 복원은 현재 대여가 전투 상태로 활성화된 뒤 확인한다.
	TestEqual(TEXT("Reused Enemy restores source mesh visibility"),
		SecondLease->GetMesh()->IsVisible(), bMeshVisible);
	TestEqual(TEXT("Reused Enemy restores child visibility"),
		SecondLease->GetCoreHitboxComponent()->IsVisible(), bCoreVisible);
	TestEqual(TEXT("Reused Enemy restores source mesh hidden state"),
		static_cast<bool>(SecondLease->GetMesh()->bHiddenInGame), bMeshHiddenInGame);

	const int32 SecondLeaseSerial = SecondLease->GetPoolLeaseSerial();
	TestFalse(TEXT("An old lease cannot return a reused Actor"),
		Pool->ReturnEnemy(SecondLease, FirstContext.GameplayGeneration, FirstLeaseSerial));
	TestEqual(TEXT("Rejected return keeps the current lease registered"),
		Pool->GetLeasedCount(AEnemyBase::StaticClass()), 1);
	SecondLease->CompletePoolSpawnPresentation(FirstContext.GameplayGeneration, FirstLeaseSerial);
	TestEqual(TEXT("A stale spawn callback cannot activate a new lease"),
		SecondLease->GetEnemyPoolState(), EEnemyPoolState::SpawnPresentation);
	SecondLease->CompletePoolSpawnPresentation(SecondContext.GameplayGeneration, SecondLeaseSerial);
	TestFalse(TEXT("Completed presentation no longer needs a virtual reservation"),
		Pool->IsSpawnPresentationSpaceOccupied(PresentingCenter, FQuat::Identity,
			PresentingRadius, PresentingHalfHeight));
	TestEqual(TEXT("The current callback activates combat"),
		SecondLease->GetEnemyPoolState(), EEnemyPoolState::CombatActive);
	TestEqual(TEXT("Reused Enemy restores capsule collision"),
		SecondLease->GetCapsuleComponent()->GetCollisionEnabled(), CapsuleCollision);
	TestEqual(TEXT("Reused Enemy restores mesh collision"),
		SecondLease->GetMesh()->GetCollisionEnabled(), MeshCollision);
	TestEqual(TEXT("Reused Enemy restores core collision"),
		SecondLease->GetCoreHitboxComponent()->GetCollisionEnabled(), CoreCollision);
	TestEqual(TEXT("Reused reinforcement joins combat using room history"),
		SecondLease->GetCombatState(), EEnemyCombatState::Combat);
	TestFalse(TEXT("Lost sight is not restored as shared contact"),
		SecondLease->HasSharedTargetContact());
	TestEqual(TEXT("Room history supplies the last known target location"),
		SecondLease->GetLastKnownPlayerLocation(), SharedTargetLocation);
	TestFalse(TEXT("Room history does not invent direct sight"), SecondLease->IsPlayerCurrentlyVisible());

	UOutlierAbilitySystemComponent* ASC = SecondLease->GetOutlierAbilitySystemComponent();
	if (TestNotNull(TEXT("Pooled Enemy has an ASC"), ASC))
	{
		TestTrue(TEXT("The death fixture applies the Dead GAS state"),
			ASC->ApplyDeadStateToSelf());
		SecondLease->BeginDeathForPoolTesting();
		TestEqual(TEXT("Pool death waits in presentation state"),
			SecondLease->GetEnemyPoolState(), EEnemyPoolState::DeathPresentation);
		SecondLease->CompletePoolDeathPresentation(
			SecondContext.GameplayGeneration,
			SecondLeaseSerial);
		TestEqual(TEXT("Death presentation returns instead of destroying"),
			SecondLease->GetEnemyPoolState(), EEnemyPoolState::Idle);
		TestFalse(TEXT("Returned pooled Enemy is not destroyed"), SecondLease->IsActorBeingDestroyed());
	}

	FirstLease->SetPoolPresentationAutoCompleteForTesting(true);
	AEnemyBase* MaxLeaseA = Pool->LeaseEnemy(
		AEnemyBase::StaticClass(), FTransform::Identity, FirstContext);
	AEnemyBase* MaxLeaseB = Pool->LeaseEnemy(
		AEnemyBase::StaticClass(), FTransform::Identity, FirstContext);
	AEnemyBase* ExhaustedLease = Pool->LeaseEnemy(
		AEnemyBase::StaticClass(), FTransform::Identity, FirstContext);
	TestNotNull(TEXT("Pool reuses the returned Enemy"), MaxLeaseA);
	TestNotNull(TEXT("Pool expands up to MaxCount"), MaxLeaseB);
	TestNull(TEXT("Pool refuses to exceed MaxCount"), ExhaustedLease);
	TestEqual(TEXT("Pool total stops at MaxCount"), Pool->GetTotalCount(AEnemyBase::StaticClass()), 2);
	TestTrue(TEXT("Prewarming during combat does not require new Actors"), Pool->PrewarmPool(Definition));
	TestEqual(TEXT("Prewarm includes leased Actors in its target count"),
		Pool->GetTotalCount(AEnemyBase::StaticClass()), 2);
	if (MaxLeaseA)
	{
		TestFalse(TEXT("A repeated lease clears the previous Dead state"), MaxLeaseA->IsDead());
		MaxLeaseA->CompletePoolDeathPresentation(
			SecondContext.GameplayGeneration,
			SecondLeaseSerial);
		TestEqual(TEXT("A stale death callback cannot return a newer lease"),
			MaxLeaseA->GetEnemyPoolState(), EEnemyPoolState::CombatActive);
	}

	if (MaxLeaseA)
	{
		// 빈 컬렉션으로 물리 파편 없이 요청/타이머 수명만 검증한다.
		// 앞선 Pool 검사는 임의 세대를 사용하므로 presentation의 월드 세대도 같은 실행으로 맞춘다.
		World->GetSubsystem<UOutlierArenaSubsystem>()->GameplayGeneration = MaxLeaseA->GetPoolGameplayGeneration();
		UGeometryCollection* Collection = NewObject<UGeometryCollection>(World);
		MaxLeaseA->SetDeathDebrisForPoolTesting(Collection, 0.5f);
		TestTrue(TEXT("Living Enemy returns without a death event"), Pool->ReturnEnemy(
			MaxLeaseA, MaxLeaseA->GetPoolGameplayGeneration(), MaxLeaseA->GetPoolLeaseSerial()));
		const FTimerHandle LivingDebrisTimer = MaxLeaseA->GetDeathDebrisTimerForPoolTesting();
		TestTrue(TEXT("Living pool return preserves delayed debris after clearing Enemy timers"),
			World->GetTimerManager().IsTimerActive(LivingDebrisTimer));
		TestFalse(TEXT("Cosmetic pool return does not mark a living Enemy dead"), MaxLeaseA->IsDead());
		AEnemyBase* ReusedEnemy = Pool->LeaseEnemy(AEnemyBase::StaticClass(),
			FTransform(FVector(3000.0, 0.0, 100.0)), FirstContext);
		TestEqual(TEXT("Returned fixture is reused"), ReusedEnemy, MaxLeaseA);
		if (ReusedEnemy)
		{
			TestTrue(TEXT("Releasing the old timer handle does not cancel its presentation"),
				World->GetTimerManager().IsTimerActive(LivingDebrisTimer));
			ReusedEnemy->SetPoolPresentationAutoCompleteForTesting(false);
			ReusedEnemy->BeginDeathForPoolTesting();
			const FTimerHandle DeathDebrisTimer = ReusedEnemy->GetDeathDebrisTimerForPoolTesting();
			TestTrue(TEXT("New death schedules a separate debris timer"),
				World->GetTimerManager().IsTimerActive(DeathDebrisTimer) && DeathDebrisTimer != LivingDebrisTimer);
			TestTrue(TEXT("New death does not overwrite the old lease's delayed debris"),
				World->GetTimerManager().IsTimerActive(LivingDebrisTimer));
			ReusedEnemy->CompletePoolDeathPresentation(
				ReusedEnemy->GetPoolGameplayGeneration(), ReusedEnemy->GetPoolLeaseSerial());
			TestTrue(TEXT("Death pool return preserves its pending debris"),
				World->GetTimerManager().IsTimerActive(DeathDebrisTimer));
			TestTrue(TEXT("Death pool return does not request duplicate debris"),
				ReusedEnemy->GetDeathDebrisTimerForPoolTesting() == DeathDebrisTimer);
		}
	}

	Pool->DestroyPool();
	TestEqual(TEXT("DestroyPool clears all buckets"), Pool->GetTotalCount(AEnemyBase::StaticClass()), 0);
	TestTrue(TEXT("DestroyPool invalidates the old lease"),
		!MaxLeaseA || MaxLeaseA->IsActorBeingDestroyed());

	UEnemyPoolDefinition* TurretDefinition = NewObject<UEnemyPoolDefinition>(World);
	FEnemyPoolEntry& TurretEntry = TurretDefinition->Entries.AddDefaulted_GetRef();
	TurretEntry.EnemyClass = AAutoTurret::StaticClass();
	TurretEntry.PrewarmCount = 1;
	TurretEntry.MaxCount = 1;
	AddExpectedError(TEXT("Prewarm rejected because AutoTurret is placed-only"),
		EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Runtime prewarm rejects placed-only AutoTurrets"),
		Pool->PrewarmPool(TurretDefinition));
	TestEqual(TEXT("Rejected AutoTurret prewarm creates no Actors"),
		Pool->GetTotalCount(AAutoTurret::StaticClass()), 0);

	// 실제 사격형 BP가 방의 전투 정보 유무에 따라 NonBattle 또는 Battle을 선택하는지 확인한다.
	// Alive 아래 전투 상태 선택이 실패해도 Death가 일반 fallback이 되면 안 된다.
	const UStateTree* GunCommonTree = LoadObject<UStateTree>(nullptr,
		TEXT("/Game/Blueprints/AI/VECDrone/StateTree/ST_VECDrone_Common.ST_VECDrone_Common"));
	if (TestNotNull(TEXT("Gun common StateTree loads"), GunCommonTree))
	{
		bool bFoundDeath = false;
		for (const FCompactStateTreeState& State : GunCommonTree->GetStates())
		{
			if (State.Name == FName(TEXT("Death")))
			{
				bFoundDeath = true;
				TestTrue(TEXT("Death requires the died event, not a failed combat selection"),
					State.RequiredEventToEnter.Tag == FGameplayTag::RequestGameplayTag(
						TEXT("Enemy.Event.Died")));
			}
		}
		TestTrue(TEXT("Gun common StateTree contains Death"), bFoundDeath);
	}
	const UStateTree* GunBattleTree = LoadObject<UStateTree>(nullptr,
		TEXT("/Game/Blueprints/AI/VECDrone/StateTree/ST_VECDroneBattle_Gun.ST_VECDroneBattle_Gun"));
	if (TestNotNull(TEXT("Gun battle StateTree loads"), GunBattleTree))
	{
		bool bFoundSearch = false;
		for (const FCompactStateTreeState& State : GunBattleTree->GetStates())
		{
			if (State.Name == FName(TEXT("Search")))
			{
				bFoundSearch = true;
				// 새 구조는 공유 추적과 시야 상실 수색을 Search Session으로 통합한다.
				bool bHasSession = false;
				for (int32 Index = 0; Index < State.TasksNum; ++Index)
				{
					const auto* Task = GunBattleTree->GetNode(State.TasksBegin + Index).GetPtr<const FEnemySearchSessionTask>();
					bool bValidSession = Task && Task->bTaskEnabled;
#if WITH_EDITORONLY_DATA
					// 완료 참여 설정은 에디터 전용이므로 패키징에서는 활성 여부만 검사한다.
					bValidSession = bValidSession && !Task->bConsideredForCompletion;
#endif
					bHasSession |= bValidSession;
				}
				TestTrue(TEXT("Search owns an enabled search session with valid completion settings"), bHasSession);
			}
		}
		TestTrue(TEXT("Gun battle StateTree contains Search"), bFoundSearch);
	}

	UClass* GunClass = LoadClass<AEnemyBase>(nullptr,
		TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"));
	if (TestNotNull(TEXT("Gun reinforcement BP loads"), GunClass))
	{
		UEnemyPoolDefinition* GunDefinition = NewObject<UEnemyPoolDefinition>(World);
		FEnemyPoolEntry& GunEntry = GunDefinition->Entries.AddDefaulted_GetRef();
		GunEntry.EnemyClass = GunClass;
		GunEntry.PrewarmCount = 1;
		GunEntry.MaxCount = 1;
		if (TestTrue(TEXT("Gun reinforcement prewarms"), Pool->PrewarmPool(GunDefinition)))
		{
			// 전투/공유 정보가 없는 방에서는 증원도 기존 비전투 정책으로 시작한다.
			RoomSubsystem->NotifyRoomCombatEnded(RoomTag);
			AEnemyBase* GunLease = Pool->LeaseEnemy(GunClass,
				FTransform(FVector(100.0, 200.0, 300.0)), FirstContext);
			if (TestNotNull(TEXT("Gun reinforcement leases"), GunLease))
			{
				if (GunLease->GetEnemyPoolState() == EEnemyPoolState::SpawnPresentation)
				{
					GunLease->CompletePoolSpawnPresentation(
						FirstContext.GameplayGeneration, GunLease->GetPoolLeaseSerial());
				}
				UStateTreeComponent* GunStateTree = GunLease->GetStateTreeComponent();
				if (TestNotNull(TEXT("Gun reinforcement has a StateTree"), GunStateTree))
				{
					const TArray<FName> ActiveStates = GunStateTree->GetActiveStateNames();
					TestEqual(TEXT("Gun reinforcement starts its StateTree"),
						GunStateTree->GetStateTreeRunStatus(), EStateTreeRunStatus::Running);
					TestTrue(TEXT("Gun reinforcement selects NonBattle immediately"),
						ActiveStates.Contains(FName(TEXT("NonBattle"))));
					TestFalse(TEXT("Gun reinforcement is not alerted in an inactive room"),
						ActiveStates.Contains(FName(TEXT("Alert"))));
				}
				TestTrue(TEXT("Gun reinforcement returns before a shared-target lease"),
					Pool->ReturnEnemy(GunLease, FirstContext.GameplayGeneration,
						GunLease->GetPoolLeaseSerial()));
				RoomSubsystem->SetActiveRoomTargetForTesting(RoomTag, SharedTargetLocation, true);
				AEnemyBase* SharedGunLease = Pool->LeaseEnemy(GunClass,
					FTransform(FVector(100.0, 200.0, 300.0)), FirstContext);
				if (TestNotNull(TEXT("Gun reinforcement leases with a shared target"), SharedGunLease))
				{
					if (SharedGunLease->GetEnemyPoolState() == EEnemyPoolState::SpawnPresentation)
					{
						SharedGunLease->CompletePoolSpawnPresentation(
							FirstContext.GameplayGeneration, SharedGunLease->GetPoolLeaseSerial());
					}
					TestTrue(TEXT("Shared target reaches the gun reinforcement without perception"),
						SharedGunLease->HasSharedTargetContact());
					if (UStateTreeComponent* SharedStateTree = SharedGunLease->GetStateTreeComponent())
					{
						const TArray<FName> SharedInitialStates = SharedStateTree->GetActiveStateNames();
						TestTrue(TEXT("Shared-target gun starts in Battle"),
							SharedInitialStates.Contains(FName(TEXT("Battle"))));
						TestEqual(TEXT("Shared target joins combat without an Alert perception step"),
							SharedGunLease->GetCombatState(), EEnemyCombatState::Combat);
						// 별도 월드 틱이 없는 fixture에서 다음 틱 예약 이벤트를 직접 전달한다.
						SharedGunLease->SendEnemyStateTreeEvent(FGameplayTag::RequestGameplayTag(
							TEXT("Enemy.Event.Combat.Entered")));
						SharedStateTree->TickComponent(0.016f, LEVELTICK_All, nullptr);
						const TArray<FName> BattleStates = SharedStateTree->GetActiveStateNames();
						TestTrue(TEXT("Shared-target gun transitions into Battle"),
							BattleStates.Contains(FName(TEXT("Battle"))));
						TestTrue(TEXT("Shared-target gun enters the unified Search branch"),
							BattleStates.Contains(FName(TEXT("Search"))));
						TestFalse(TEXT("Shared-target gun never returns to NonBattle"),
							BattleStates.Contains(FName(TEXT("NonBattle"))));

						// Perception의 시야 플래그가 TargetActor 갱신보다 앞서면 Battle의
						// 직접 교전/공유 추적 조건 사이에 한 프레임 공백이 생길 수 있다.
						SharedGunLease->SetPlayerCurrentlyVisible(true);
						SharedStateTree->TickComponent(0.016f, LEVELTICK_All, nullptr);
						TestFalse(TEXT("Living gun does not fall into Death during a target-selection gap"),
							SharedStateTree->GetActiveStateNames().Contains(FName(TEXT("Death"))));
						TestTrue(TEXT("Gun keeps Search without a visible target Actor"),
							SharedStateTree->GetActiveStateNames().Contains(FName(TEXT("Search"))));
						SharedGunLease->SetPlayerCurrentlyVisible(false);
						SharedStateTree->TickComponent(0.016f, LEVELTICK_All, nullptr);

						// 사망이 아닌 표적 공유 소실은 전투 수색으로 이어져야 한다.
						SharedGunLease->ClearSharedTargetContact();
						for (int32 TickIndex = 0; TickIndex < 4; ++TickIndex)
						{
							World->GetTimerManager().Tick(0.016f);
							SharedStateTree->TickComponent(0.016f, LEVELTICK_All, nullptr);
						}
						TestFalse(TEXT("Living gun never selects Death after losing a shared target"),
							SharedStateTree->GetActiveStateNames().Contains(FName(TEXT("Death"))));
						TestTrue(TEXT("Living gun stays in Battle after losing a shared target"),
							SharedStateTree->GetActiveStateNames().Contains(FName(TEXT("Battle"))));

						// 공유 표적 소실과 가시성 신호가 엇갈린 프레임에서 트리가
						// 다시 선택되더라도 사망 분기로 빠지지 않아야 한다.
						SharedGunLease->SetPlayerCurrentlyVisible(true);
						SharedStateTree->StopLogic(TEXT("Target-selection gap regression"));
						SharedStateTree->StartLogic();
						SharedStateTree->TickComponent(0.016f, LEVELTICK_All, nullptr);
						TestEqual(TEXT("Gun StateTree keeps running with visibility but no target Actor"),
							SharedStateTree->GetStateTreeRunStatus(), EStateTreeRunStatus::Running);
						TestFalse(TEXT("Living gun does not select Death after target-selection restart"),
							SharedStateTree->GetActiveStateNames().Contains(FName(TEXT("Death"))));
						TestTrue(TEXT("Gun searches when the visible target Actor is missing"),
							SharedStateTree->GetActiveStateNames().Contains(FName(TEXT("Search"))));
					}
				}
			}
		}
		Pool->DestroyPool();
	}

	CleanupWorld();
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyPoolDefinitionValidationTest,
	"Outlier.Enemy.PoolDefinition.Validation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyPoolDefinitionValidationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEnemyPoolDefinition* Definition = NewObject<UEnemyPoolDefinition>();
	{
		FDataValidationContext ValidationContext;
		TestEqual(TEXT("An empty pool definition is valid"),
			Definition->IsDataValid(ValidationContext), EDataValidationResult::Valid);
	}

	FEnemyPoolEntry& ValidEntry = Definition->Entries.AddDefaulted_GetRef();
	ValidEntry.EnemyClass = AEnemyBase::StaticClass();
	ValidEntry.PrewarmCount = 2;
	ValidEntry.MaxCount = 3;
	{
		FDataValidationContext ValidationContext;
		TestEqual(TEXT("A configured pool entry is valid"),
			Definition->IsDataValid(ValidationContext), EDataValidationResult::Valid);
	}

	FEnemyPoolEntry& DuplicateEntry = Definition->Entries.AddDefaulted_GetRef();
	DuplicateEntry.EnemyClass = AEnemyBase::StaticClass();
	DuplicateEntry.PrewarmCount = 4;
	DuplicateEntry.MaxCount = 3;
	{
		FDataValidationContext ValidationContext;
		TestEqual(TEXT("Duplicate classes and PrewarmCount above MaxCount are rejected"),
			Definition->IsDataValid(ValidationContext), EDataValidationResult::Invalid);
	}

	Definition->Entries.Reset();
	FEnemyPoolEntry& TurretEntry = Definition->Entries.AddDefaulted_GetRef();
	TurretEntry.EnemyClass = AAutoTurret::StaticClass();
	TurretEntry.PrewarmCount = 1;
	TurretEntry.MaxCount = 1;
	{
		FDataValidationContext ValidationContext;
		TestEqual(TEXT("A placed-only AutoTurret class is rejected"),
			Definition->IsDataValid(ValidationContext), EDataValidationResult::Invalid);
	}

	Definition->Entries.Reset();
	Definition->Entries.AddDefaulted();
	{
		FDataValidationContext ValidationContext;
		TestEqual(TEXT("A missing EnemyClass is rejected"),
			Definition->IsDataValid(ValidationContext), EDataValidationResult::Invalid);
	}

	return true;
}
#endif

#endif
