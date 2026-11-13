#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StateTreeComponent.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyPoolDefinition.h"
#include "Enemy/EnemyPoolSubsystem.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Perception/AIPerceptionSystem.h"
#include "Perception/AISense_Sight.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomVolume.h"
#include "Shooter/ShooterCharacter.h"
#include "UObject/UnrealType.h"
#include "Weapon/RangedWeaponBase.h"

namespace
{
struct FF6PoolWorld
{
	UWorld* World = nullptr;
	UGameInstance* GameInstance = nullptr;
	UEnemyPoolSubsystem* Pool = nullptr;
	UEnemyRoomSubsystem* EnemyRooms = nullptr;
	UClass* GunClass = nullptr;
	FEnemyPoolLeaseContext LeaseContext;
	float Step = 1.0f / 60.0f;

	explicit FF6PoolWorld(float InStep) : Step(InStep)
	{
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("F6PoolIntegration")));
		World->AddToRoot();
		FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
		Context.SetCurrentWorld(World);
		Context.RunAsDedicated = true;
		GameInstance = NewObject<UGameInstance>(GEngine);
		World->SetGameInstance(GameInstance);
		GameInstance->Init();
		World->CreateAISystem();
		FURL URL;
		URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
		World->SetGameMode(URL);
		World->InitializeActorsForPlay(URL);
		World->BeginPlay();
		LeaseContext.RoomTag = FGameplayTag::RequestGameplayTag(TEXT("Room.Level01.1"));
		LeaseContext.GameplayGeneration = 61;
		LeaseContext.CombatPhaseIndex = 0;
		LeaseContext.WaveIndex = 0;
		Pool = World->GetSubsystem<UEnemyPoolSubsystem>();
		EnemyRooms = World->GetSubsystem<UEnemyRoomSubsystem>();
	}

	~FF6PoolWorld()
	{
		GEngine->ShutdownWorldNetDriver(World);
		World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(true);
		GameInstance->Shutdown();
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}

	void Tick(bool bTimeOnly = false, bool bInterruptFrame = false) const
	{
		++GFrameCounter;
		// 큰 dt는 중단 프레임에 주입하고 정상 관측 프레임으로 복귀한다.
		// 계속 2.5Hz로 실행하면 0.25초 관측 수명보다 매 프레임이 길어 복구 자체를 검사할 수 없다.
		const float DeltaTime = bTimeOnly || bInterruptFrame ? Step : FMath::Min(Step, 1.0f / 15.0f);
		World->Tick(bTimeOnly ? LEVELTICK_TimeOnly : LEVELTICK_All, DeltaTime);
		if (!bTimeOnly)
		{
			if (UAIPerceptionSystem* Perception = UAIPerceptionSystem::GetCurrent(World))
			{
				Perception->Tick(DeltaTime);
			}
		}
	}

	bool Initialize(FAutomationTestBase& Test)
	{
		URoomCombatSubsystem* Rooms = World->GetSubsystem<URoomCombatSubsystem>();
		URoomCombatDefinition* Definition = NewObject<URoomCombatDefinition>(World);
		FRoomCombatRoomDefinition& Entry = Definition->RoomDefinitions.AddDefaulted_GetRef();
		Entry.RoomTag = LeaseContext.RoomTag;
		Entry.CombatPhases.AddDefaulted();
		Rooms->SetCombatDefinitionForTesting(Definition);
		ARoomVolume* Room = World->SpawnActor<ARoomVolume>();
		if (!Test.TestNotNull(TEXT("Pool integration has a real home volume"), Room))
		{
			return false;
		}
		CastChecked<UBoxComponent>(Room->GetRootComponent())->SetBoxExtent(FVector(2000, 2000, 1000));
		if (!Test.TestTrue(TEXT("Actual home registers"), Rooms->RegisterRoom(Room, LeaseContext.RoomTag)))
		{
			return false;
		}
		GunClass = LoadClass<AEnemyBase>(nullptr,
			TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"));
		if (!Test.TestNotNull(TEXT("Saved Gun Blueprint loads"), GunClass))
		{
			return false;
		}
		UEnemyPoolDefinition* PoolDefinition = NewObject<UEnemyPoolDefinition>(World);
		FEnemyPoolEntry& Gun = PoolDefinition->Entries.AddDefaulted_GetRef();
		Gun.EnemyClass = GunClass;
		Gun.PrewarmCount = 1;
		Gun.MaxCount = 1;
		return Test.TestTrue(TEXT("Production pool prewarms the saved Blueprint"), Pool->PrewarmPool(PoolDefinition));
	}

	AEnemyBase* Lease()
	{
		return Pool->LeaseEnemy(GunClass, FTransform(FVector(0, 0, 200)), LeaseContext);
	}

	void Activate(AEnemyBase& Enemy) const
	{
		if (Enemy.GetEnemyPoolState() == EEnemyPoolState::SpawnPresentation)
		{
			Enemy.CompletePoolSpawnPresentation(Enemy.GetPoolGameplayGeneration(), Enemy.GetPoolLeaseSerial());
		}
	}

	bool Reserve(AEnemyBase& Enemy, uint64 Request) const
	{
		const FVector Start = Enemy.GetCapsuleComponent()->GetComponentLocation();
		const FVector Destination = Start + FVector(200, 200, 0);
		return EnemyRooms->TryReserveCombatDestination(&Enemy, Request, Destination, {}, {Start, Destination}, 5.0f)
			&& Enemy.AdoptCombatReservation(Request);
	}
};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyF6PoolIntegrationTest,
	"Outlier.Enemy.F6.PoolLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyF6PoolIntegrationTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Rate : {TEXT("15Hz"), TEXT("30Hz"), TEXT("60Hz"), TEXT("LargeDt200ms"), TEXT("LargeDt400ms")})
	{
		Names.Add(Rate);
		Commands.Add(Rate);
	}
}

bool FEnemyF6PoolIntegrationTest::RunTest(const FString& Parameters)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	const float Step = Parameters == TEXT("15Hz") ? 1.0f / 15.0f
		: Parameters == TEXT("30Hz") ? 1.0f / 30.0f
		: Parameters == TEXT("LargeDt200ms") ? 0.2f
		: Parameters == TEXT("LargeDt400ms") ? 0.4f : 1.0f / 60.0f;
	FF6PoolWorld Fixture(Step);
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	AEnemyBase* First = Fixture.Lease();
	if (!TestNotNull(TEXT("First actual pool lease exists"), First))
	{
		return false;
	}
	Fixture.Activate(*First);
	AEnemyAIController* FirstAI = Cast<AEnemyAIController>(First->GetController());
	if (!TestNotNull(TEXT("Activation creates the real controller"), FirstAI))
	{
		return false;
	}
	FirstAI->SetEnemyPerceptionEnabled(false);
	First->GetStateTreeComponent()->StopLogic(TEXT("F6 old ownership fixture"));
	First->EnterCombatFromRoom(FVector(5000, 0, 200), false);
	UClass* OwnershipClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	if (!TestNotNull(TEXT("Reservation player Blueprint loads"), OwnershipClass))
	{
		return false;
	}
	FActorSpawnParameters OwnershipSpawn;
	OwnershipSpawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AShooterCharacter* OwnershipTarget = Fixture.World->SpawnActor<AShooterCharacter>(OwnershipClass,
		FVector(900, 900, 200), FRotator::ZeroRotator, OwnershipSpawn);
	if (!TestNotNull(TEXT("Reservation fixture has a valid player"), OwnershipTarget))
	{
		return false;
	}
	OwnershipTarget->GetCharacterMovement()->DisableMovement();
	// 예약 발급에는 실제 추적 대상이 필요하다. 소유권 준비에만 캐시를 쓰고,
	// 마지막 재대여의 자연 Sight 검증은 이 대상을 제거한 새 플레이어로 수행한다.
	FirstAI->SetCachedVisibleTargetForTesting(OwnershipTarget);
	First->SetPlayerCurrentlyVisible(true);
	First->ObserveTrackedTarget(OwnershipTarget);
	First->EnsureCombatTacticalEpisode(OwnershipTarget, OwnershipTarget->GetActorLocation());
	const uint64 OldReservation = First->BeginCombatReservationRequest();
	if (!TestTrue(TEXT("First lease owns a live reservation"), OldReservation != 0 && Fixture.Reserve(*First, OldReservation)))
	{
		return false;
	}
	FirstAI->SetCachedVisibleTargetForTesting(nullptr);
	First->ClearTargetTracking();
	First->SetPlayerCurrentlyVisible(false);
	First->UpdateLastKnownPlayerLocation(FVector(5000, 0, 200));
	if (!TestTrue(TEXT("First lease starts a real search episode"), First->BeginSearchEpisode()))
	{
		return false;
	}
	First->EnterSearchLocalGuard();
	const uint64 OldSearch = First->GetSearchEpisode().Serial;
	const double GuardDeadline = First->GetSearchEpisode().GuardEndsAt;
	while (Fixture.World->GetTimeSeconds() < GuardDeadline)
	{
		Fixture.Tick(true);
	}
	if (!TestTrue(TEXT("Expired guard commits return through the production boundary"), First->TryCompleteGeneralSearch(OldSearch)))
	{
		return false;
	}
	FVector ReturnDestination;
	if (!TestTrue(TEXT("First lease selects a real return request"), First->TrySelectReturnHomeDestination(ReturnDestination)))
	{
		return false;
	}
	const uint64 OldReturn = First->GetReturnHomeRequestSerial();
	const int32 OldLease = First->GetPoolLeaseSerial();
	const int32 OldGeneration = First->GetPoolGameplayGeneration();
	First->SetPoolPresentationAutoCompleteForTesting(false);
	// 실제 반환/재대여 사이에 다음 틱 귀환 콜백을 남긴다. 필드만 바꾼 가짜 lease로는
	// Controller 파괴·타이머 정리·예약 반환이 함께 실행되는 경계를 검증할 수 없다.
	if (!TestTrue(TEXT("Actual return accepts the current lease"), Fixture.Pool->ReturnEnemy(First, OldGeneration, OldLease)))
	{
		return false;
	}
	++Fixture.LeaseContext.GameplayGeneration;
	AEnemyBase* Second = Fixture.Lease();
	if (!TestTrue(TEXT("Pool reuses the exact saved Blueprint UObject"), Second == First))
	{
		return false;
	}
	TestTrue(TEXT("Second lease has a new serial"), Second->GetPoolLeaseSerial() != OldLease);
	Second->CompletePoolSpawnPresentation(OldGeneration, OldLease);
	TestEqual(TEXT("Stale spawn completion cannot activate the second lease"), Second->GetEnemyPoolState(), EEnemyPoolState::SpawnPresentation);
	Fixture.Activate(*Second);
	AEnemyAIController* SecondAI = Cast<AEnemyAIController>(Second->GetController());
	if (!TestNotNull(TEXT("Reused actor has a newly owned AI"), SecondAI))
	{
		return false;
	}
	SecondAI->SetEnemyPerceptionEnabled(false);
	Second->GetStateTreeComponent()->StopLogic(TEXT("F6 new ownership fixture"));
	Second->EnterCombatFromRoom(FVector(700, 700, 200), false);
	SecondAI->SetCachedVisibleTargetForTesting(OwnershipTarget);
	Second->SetPlayerCurrentlyVisible(true);
	Second->ObserveTrackedTarget(OwnershipTarget);
	Second->EnsureCombatTacticalEpisode(OwnershipTarget, OwnershipTarget->GetActorLocation());
	const uint64 NewReservation = Second->BeginCombatReservationRequest();
	if (!TestTrue(TEXT("New lease owns a different reservation"), NewReservation != 0 && NewReservation != OldReservation
		&& Fixture.Reserve(*Second, NewReservation)))
	{
		return false;
	}
	Second->ReleaseCombatReservation(OldReservation);
	TestTrue(TEXT("Old Exit cannot release the current lease reservation"), Second->IsCombatReservationRequestCurrent(NewReservation));
	TestTrue(TEXT("New reservation remains adopted"), Second->RefreshCombatReservation(NewReservation));
	TestFalse(TEXT("Previous return result is rejected"), Second->IsReturnHomeRequestCurrent(OldReturn));
	TestFalse(TEXT("Previous search completion is rejected"), Second->TryCompleteGeneralSearch(OldSearch));
	TestFalse(TEXT("Previous lease cannot return the reused actor"), Fixture.Pool->ReturnEnemy(Second, OldGeneration, OldLease));
	Fixture.Tick(true);
	TestEqual(TEXT("Deferred old guard does not overwrite the current combat intent"), Second->GetCombatState(), EEnemyCombatState::Combat);
	TestEqual(TEXT("Rejected old return preserves the leased bucket"), Fixture.Pool->GetLeasedCount(Fixture.GunClass), 1);

	// 정리만 성공하고 재사용 AI가 죽어 있는 상태를 놓치지 않도록, 한 번 더 정상 대여한 뒤
	// 자극 주입 없이 실제 Sight → 인지 → 저장 Tree → 탄약 소비까지 다시 실행한다.
	if (!TestTrue(TEXT("Second real return succeeds"), Fixture.Pool->ReturnEnemy(Second,
		Second->GetPoolGameplayGeneration(), Second->GetPoolLeaseSerial())))
	{
		return false;
	}
	++Fixture.LeaseContext.GameplayGeneration;
	AEnemyBase* Third = Fixture.Lease();
	if (!TestTrue(TEXT("Third lease still reuses the same actor"), Third == First))
	{
		return false;
	}
	OwnershipTarget->Destroy();
	Fixture.Activate(*Third);
	// 이 월드는 Arena 스트리밍을 구동하지 않으므로 최초 방 전투 개시 권한은 검증 대상이 아니다.
	// 실제 증원처럼 방 전투만 열고 공유 관측은 비운다. Pawn은 여전히 NonCombat이며,
	// 아래 새 플레이어의 실제 Sight가 들어와야만 직접 합류·추적·첫 공격을 시작한다.
	Fixture.EnemyRooms->SetActiveRoomTargetForTesting(Fixture.LeaseContext.RoomTag, FVector::ZeroVector, false);
	TestEqual(TEXT("Room combat without shared sight does not pre-commit the reused pawn"),
		Third->GetCombatState(), EEnemyCombatState::NonCombat);
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	if (!TestNotNull(TEXT("Real Shooter Blueprint loads"), ShooterClass))
	{
		return false;
	}
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AShooterCharacter* Shooter = Fixture.World->SpawnActor<AShooterCharacter>(ShooterClass,
		FVector(600, 0, 200), FRotator(0, 180, 0), Spawn);
	ARangedWeaponBase* Weapon = Third->GetCurrentWeapon();
	if (!TestNotNull(TEXT("Real Shooter spawns"), Shooter) || !TestNotNull(TEXT("Reused Gun still owns its real weapon"), Weapon))
	{
		return false;
	}
	Shooter->SetCanBeDamaged(false);
	Shooter->GetCharacterMovement()->DisableMovement();
	Third->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	// 실제 Gun의 기본 무한탄은 발사 후에도 탄약이 줄지 않는다. 기존 공격 회귀 테스트처럼
	// 계측만 유한탄으로 바꾸고, 첫 발사·연사·전조·회전은 저장된 Tree와 무기에 맡긴다.
	FBoolProperty* InfiniteAmmo = FindFProperty<FBoolProperty>(ARangedWeaponBase::StaticClass(), TEXT("bInfiniteAmmo"));
	if (!TestNotNull(TEXT("Actual weapon exposes its finite-ammo measurement switch"), InfiniteAmmo))
	{
		return false;
	}
	InfiniteAmmo->SetPropertyValue_InContainer(Weapon, false);
	const int32 InitialAmmo = Weapon->GetCurrentAmmo();
	TestTrue(TEXT("Sight source registers without injecting a stimulus"),
		UAIPerceptionSystem::RegisterPerceptionStimuliSource(Fixture.World, UAISense_Sight::StaticClass(), Shooter));
	const double ReacquireDeadline = Fixture.World->GetTimeSeconds() + 10.0;
	if (Step > 0.1f)
	{
		Fixture.Tick(false, true);
	}
	bool bSawNaturalSight = false;
	bool bSawDirectCombat = false;
	bool bSawBattle = false;
	while (Fixture.World->GetTimeSeconds() < ReacquireDeadline && Weapon->GetCurrentAmmo() >= InitialAmmo)
	{
		Fixture.Tick();
		bSawNaturalSight |= Third->IsPlayerCurrentlyVisible();
		bSawDirectCombat |= Third->IsPlayerCurrentlyVisible() && Third->GetTrackedTarget() == Shooter
			&& Third->GetCombatState() == EEnemyCombatState::Combat;
		bSawBattle |= Third->GetStateTreeComponent()->GetActiveStateNames().Contains(TEXT("Battle"));
	}
	const UStateTreeComponent* ReusedTree = Third->GetStateTreeComponent();
	const AEnemyAIController* ReusedAI = Cast<AEnemyAIController>(Third->GetController());
	AddInfo(FString::Printf(TEXT("[F6PoolReacquire] Time=%.3f State=%s Actor=%s Visible=%d SawSight=%d Tracked=%s Preferred=%s TreeTick=%d TickInterval=%.4f Registered=%d Active=%s Clue=%.3f"),
		Fixture.World->GetTimeSeconds(), *UEnum::GetValueAsString(Third->GetCombatState()), *Third->GetActorLocation().ToString(),
		Third->IsPlayerCurrentlyVisible(), bSawNaturalSight, *GetNameSafe(Third->GetTrackedTarget()),
		*GetNameSafe(ReusedAI ? const_cast<AEnemyAIController*>(ReusedAI)->GetPreferredVisibleTarget() : nullptr),
		ReusedTree->IsComponentTickEnabled(), ReusedTree->GetComponentTickInterval(), ReusedTree->IsRegistered(),
		*FString::JoinBy(ReusedTree->GetActiveStateNames(), TEXT(" > "), [](FName Name) { return Name.ToString(); }),
		Third->GetLastAlertClueAt()));
	TestTrue(TEXT("Reused AI receives natural visibility before combat preparation"), bSawNaturalSight);
	TestTrue(TEXT("Reused AI reacquires by natural direct Sight"), bSawDirectCombat);
	TestTrue(TEXT("Saved common and linked Battle Tree resume after reuse"), bSawBattle);
	TestTrue(TEXT("Actual weapon consumes ammo after fresh preparation and aim"), Weapon->GetCurrentAmmo() < InitialAmmo);
	TestEqual(TEXT("Three lifetimes never allocate a second actor"), Fixture.Pool->GetTotalCount(Fixture.GunClass), 1);
	AddInfo(FString::Printf(TEXT("[F6Pool] Rate=%s Step=%.4f Actor=%s Lease=%d->%d->%d Direct=%d Battle=%d Ammo=%d->%d"),
		*Parameters, Step, *Third->GetName(), OldLease, OldLease + 1, Third->GetPoolLeaseSerial(),
		bSawDirectCombat, bSawBattle, InitialAmmo, Weapon->GetCurrentAmmo()));
	return !HasAnyErrors();
}

#endif
