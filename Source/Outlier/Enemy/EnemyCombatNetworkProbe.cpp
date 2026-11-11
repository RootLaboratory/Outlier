#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyCombatNavigation.h"
#include "Enemy/EnemyCombatTestHelpers.h"
#include "Enemy/EnemyStateTreeComponent.h"
#include "Enemy/EnemyStateTreeTacticalTasks.h"
#include "Enemy/EnemyPoolDefinition.h"
#include "Enemy/EnemyPoolSubsystem.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "Enemy/AutoTurret.h"
#include "Enemy/VECDrone.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomTagComponent.h"
#include "Room/RoomVolume.h"
#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterInputConfig.h"
#include "Shooter/Anim/FootstepNoiseTestProbe.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "OutlierPlayerState.h"
#include "Network/OutlierArenaSubsystem.h"
#include "Weapon/RangedWeaponBase.h"
#include "Weapon/WallShotProbe.h"
#include "Camera/CameraActor.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/NetDriver.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "HighResScreenshot.h"
#include "InputActionValue.h"
#include "EnhancedPlayerInput.h"
#include "InputAction.h"
#include "UObject/UnrealType.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"

namespace EnemyCombatNetworkProbe
{
// 개발 실행 인자가 있는 프로세스만 관측한다. 제품 Tick/RPC나 강제 포즈 갱신을 추가하지 않는다.
struct FProbe
{
	FString Role, Output, Scenario;
	float Duration = 30.0f;
	int32 CaptureLimit = 24;
	double WallStarted = FPlatformTime::Seconds();
	double ConnectedAt = -1.0, StartedAt = -1.0, LastSample = -1.0, LastFrame = -1.0;
	TWeakObjectPtr<UWorld> ActiveWorld;
	TWeakObjectPtr<AShooterCharacter> Target;
	TWeakObjectPtr<APartnerCharacter> Partner;
	TWeakObjectPtr<ACameraActor> Camera;
	TWeakObjectPtr<AStaticMeshActor> Cover;
	TArray<TWeakObjectPtr<AStaticMeshActor>> AttemptCage;
	TArray<TWeakObjectPtr<AEnemyBase>> Drones;
	TMap<TWeakObjectPtr<AEnemyBase>, FVector> PreviousLocations;
	TSet<TWeakObjectPtr<AEnemyBase>> DoorCrossers;
	int32 InvalidDoorCrossings = 0;
	TArray<double> FrameTimes;
	TArray<double> TickCosts;
	double TickStarted = 0.0;
	int32 Stage = -1, Samples = 0, Frames = 0, Shots = 0, Presentations = 0, InvalidShots = 0;
	int32 StageShots[5] = {};
	int32 VisiblePartnerSamples = 0, TargetMismatchSamples = 0, PartnerShots = 0, HiddenShooterFootsteps = 0;
	bool bListenServer = FParse::Param(FCommandLine::Get(), TEXT("OutlierCombatListen"));
	int32 InvalidShotPaths = 0, CoverBlockedSamples = 0, CloseShots = 0, HeightShots = 0;
	int32 Footsteps = 0, SearchFootsteps = 0, NoiseHandoffs = 0, SettledAfterSearchSamples = 0, ReacquireShots = 0;
	bool bSawSearch = false;
	double LastSearchDeadline = -1.0;
	EEnemyPerceptionGoalType PreviousGoal = EEnemyPerceptionGoalType::None;
	double CompletedAt = -1.0;
	int32 TelegraphSamples = 0, FiringSamples = 0, RecoverSamples = 0, SearchSamples = 0;
	int32 MaxDrones = 0, PenetrationSamples = 0, Progressed = 0;
	double FirstSight = -1.0, FirstCombat = -1.0, FirstTelegraph = -1.0, FirstShot = -1.0;
	bool bScene = false, bFinished = false, bHeader = false;
	bool bSetup = false;
	bool bRolesVerified = false;
	FString LocalRole = TEXT("Unassigned");
	FString LocalPawnClass;
	int32 ApprovedAttemptShots = 0, InvalidAttemptShots = 0, NormalShots = 0;
	int32 AttemptEndpointBlockers = 0, AttemptEndpointActorSkips = 0;
	int32 ShotsWhileHidden = 0, ShotsWhileInactive = 0, BypassShots = 0, MovingShots = 0;
	int32 TargetChanges = 0, YieldSamples = 0, ReservationConflictSamples = 0;
	int32 ReturnSamples = 0, ReturnGuardSamples = 0, ReturnCompletedSamples = 0, GuardSamples = 0;
	double FirstGuardAt = -1.0, FirstReturnAt = -1.0;
	TMap<TWeakObjectPtr<AEnemyBase>, TWeakObjectPtr<AActor>> PreviousTargets;
	TWeakObjectPtr<AEnemyBase> PoolOriginal;
	FEnemyPoolLeaseContext PoolContext;
	int32 PreviousPoolLease = INDEX_NONE;
	bool bPoolReturned = false, bPoolReused = false;
	int32 PoolReacquireShots = 0;
	double FirstAttemptShotAt = -1.0;
	FString RunRoot() const { return FPaths::GetPath(Output); }
	bool IsSearchScenario() const { return Scenario == TEXT("Search"); }
	bool IsAttemptScenario() const { return Scenario == TEXT("Attempt") || Scenario == TEXT("TurretAttempt"); }

	// 소유 입력은 원격과 Listen 호스트가 같은 경로를 사용한다. 관전자에게는 입력을 주지 않는다.
	void DriveLocalPlayer(UWorld* World, double Now)
	{
		APlayerController* PC = World->GetFirstPlayerController();
		AShooterCharacter* Shooter = PC && PC->IsLocalController() ? Cast<AShooterCharacter>(PC->GetPawn()) : nullptr;
		if (!Shooter || Shooter->GetActorLocation().X >= 5000)
		{
			return;
		}
		const double InputAge = StartedAt < 0.0 ? 0.0 : Now - StartedAt;
		const bool bSearchWalk = IsSearchScenario() && InputAge >= 18.0 && InputAge < 19.5;
		const bool bHiddenShooterWalk = Scenario == TEXT("TargetSelection") && InputAge >= 6.5 && InputAge < 8.5;
		if (bSearchWalk || bHiddenShooterWalk || (Scenario == TEXT("Combat") && FMath::Fmod(InputAge, 6.0) < 0.5))
		{
			Shooter->AddMovementInput(FVector(0, 1, 0), 1.0f);
		}
		const FObjectProperty* Property = FindFProperty<FObjectProperty>(Shooter->GetClass(), TEXT("InputConfig"));
		UShooterInputConfig* Config = Property ? Cast<UShooterInputConfig>(Property->GetObjectPropertyValue_InContainer(Shooter)) : nullptr;
		UInputAction* Action = Config ? Config->LeanAction : nullptr;
		if (UEnhancedPlayerInput* Input = Cast<UEnhancedPlayerInput>(PC->PlayerInput); Input && Action)
		{
			Input->InjectInputForAction(Action, FInputActionValue(FMath::Fmod(InputAge, 12.0) < 6.0 ? -1.0f : 1.0f));
		}
	}

	bool Accept(UWorld* World) const
	{
		return !bFinished && World && World->IsGameWorld() && World->HasBegunPlay()
			&& (!ActiveWorld.IsValid() || ActiveWorld.Get() == World)
			&& (Role == TEXT("Server") ? World->GetNetMode() == (bListenServer ? NM_ListenServer : NM_DedicatedServer)
				: World->GetNetMode() == NM_Client);
	}

	AStaticMeshActor* Block(UWorld* World, FVector Location, FVector Extent)
	{
		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>();
		if (Actor)
		{
			Actor->SetMobility(EComponentMobility::Movable);
			Actor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Actor->SetActorLocation(Location);
			Actor->SetActorScale3D(Extent / 50.0f);
			Actor->GetStaticMeshComponent()->SetCollisionResponseToAllChannels(ECR_Block);
			Actor->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			// 서버에서 나중에 추가한 엄폐물도 양 화면의 실제 충돌 배치와 일치시킨다.
			if (Role == TEXT("Server"))
			{
				Actor->SetReplicates(true);
				Actor->GetStaticMeshComponent()->SetIsReplicated(true);
				Actor->SetReplicateMovement(true);
				Actor->bAlwaysRelevant = true;
			}
		}
		return Actor;
	}

	void BlockAttemptEndpoints(UWorld* World, AEnemyBase& Enemy)
	{
		const UCapsuleComponent* Capsule = Enemy.GetCapsuleComponent();
		const ARangedWeaponBase* Weapon = Enemy.GetCurrentWeapon();
		if (!Capsule || !Weapon || !Target.IsValid())
		{
			return;
		}
		TArray<FVector> Destinations;
		const auto AddDestination = [&Destinations](const FVector& Destination)
		{
			if (!Destinations.ContainsByPredicate([&Destination](const FVector& Existing) { return Existing.Equals(Destination, 1.0); }))
			{
				Destinations.Add(Destination);
			}
		};
		// 경로만 둘러막으면 후보 상한에서 '미확인'으로 끝날 수 있다. 물리 소진 전용 배치에서는
		// 양쪽 선회 순서의 실제 후보 끝점도 막고, 완료된 높이/점유 검사가 탈락 이유를 증명하게 한다.
		for (const bool bLeft : {false, true})
		{
			TArray<EnemyCombatNavigation::FCandidate> Candidates;
			EnemyCombatNavigation::BuildCandidates(Enemy.GetActorLocation(), Target->GetActorLocation(),
				Weapon->GetEffectiveRange(), bLeft, Candidates);
			for (const auto& Candidate : Candidates)
			{
				// 생산 선택기도 시작점 60cm 안의 생성 후보는 제거한다.
				if (FVector::DistSquared(Candidate.Destination, Enemy.GetActorLocation()) >= FMath::Square(60.0))
				{
					AddDestination(Candidate.Destination);
				}
			}
		}
		TArray<FVector> CurrentRoute;
		FVector CurrentDestination;
		if (Enemy.GetCombatNavigationHandoff(CurrentRoute, CurrentDestination))
		{
			AddDestination(CurrentDestination);
		}
		const float HalfWidth = FMath::Clamp(Capsule->GetScaledCapsuleRadius() + 30.0f, 60.0f, 100.0f);
		const FVector Extent(HalfWidth, HalfWidth, 2000.0f);
		for (const FVector& Destination : Destinations)
		{
			const FVector Center(Destination.X, Destination.Y, Capsule->GetComponentLocation().Z);
			bool bTouchesActor = false;
			for (const UCapsuleComponent* Protected : {Capsule, static_cast<const UCapsuleComponent*>(Target->GetCapsuleComponent()),
				Partner.IsValid() ? static_cast<const UCapsuleComponent*>(Partner->GetCapsuleComponent()) : nullptr})
			{
				if (!Protected)
				{
					continue;
				}
				const FVector Delta = Center - Protected->GetComponentLocation();
				const double XGap = FMath::Max(FMath::Abs(Delta.X) - HalfWidth, 0.0);
				const double YGap = FMath::Max(FMath::Abs(Delta.Y) - HalfWidth, 0.0);
				bTouchesActor |= XGap * XGap + YGap * YGap <= FMath::Square(Protected->GetScaledCapsuleRadius() + 10.0f);
			}
			if (bTouchesActor)
			{
				// 시작 캡슐을 관통시켜 실패를 만들지 않는다. 제외 수를 결과에 남겨 미검증 후보를 구분한다.
				++AttemptEndpointActorSkips;
				continue;
			}
			if (AStaticMeshActor* Endpoint = Block(World, Center, Extent))
			{
				// WorldDynamic은 바닥 표본에 포함되지 않지만 실제 캡슐/탄환 충돌은 막는다.
				Endpoint->GetStaticMeshComponent()->SetCollisionObjectType(ECC_WorldDynamic);
				Endpoint->GetStaticMeshComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
				AttemptCage.Add(Endpoint);
				++AttemptEndpointBlockers;
			}
		}
	}

	void Scene(UWorld* World)
	{
		bScene = true;
		// 모듈 로딩/초기 셰이더 컴파일 시간은 접속 준비 제한 시간에 포함하지 않는다.
		WallStarted = FPlatformTime::Seconds();
		ActiveWorld = World;
		IFileManager::Get().MakeDirectory(*(Output / TEXT("Frames")), true);
		if (Role == TEXT("Server"))
		{
			Block(World, FVector(0, 0, -50), FVector(6000, 6000, 50));
			// 높이 구간은 낙하 중 순간 위치가 아니라 실제 발판 위의 표적을 검사한다.
			if (!Scenario.StartsWith(TEXT("Crowd")))
			{
				Block(World, FVector(900, 0, 100), FVector(100, 160, 100));
			}
			if (Scenario.StartsWith(TEXT("Crowd")))
			{
				Block(World, FVector(0, -1720, 500), FVector(100, 1500, 500));
				Block(World, FVector(0, 1720, 500), FVector(100, 1500, 500));
			}
		}
		if (Role != TEXT("Server") || bListenServer)
		{
			ADirectionalLight* Light = World->SpawnActor<ADirectionalLight>(FVector(0, 0, 1200), FRotator(-45, -35, 0));
			if (Light)
			{
				Light->GetLightComponent()->SetIntensity(5.0f);
			}
			const FVector Focus(300, 0, 150), Eye(-350, -900, 420);
			Camera = World->SpawnActor<ACameraActor>(Eye, (Focus - Eye).Rotation());
		}
	}

	bool Setup(UWorld* World)
	{
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Room.Level01.1"));
		URoomCombatSubsystem* Rooms = World->GetSubsystem<URoomCombatSubsystem>();
		URoomCombatDefinition* Definition = NewObject<URoomCombatDefinition>(Rooms);
		auto& Entry = Definition->RoomDefinitions.AddDefaulted_GetRef();
		Entry.RoomTag = Tag;
		Entry.CombatPhases.AddDefaulted_GetRef().Waves.AddDefaulted();
		Rooms->SetCombatDefinitionForTesting(Definition);
		ARoomVolume* Room = World->SpawnActor<ARoomVolume>();
		if (!Room)
		{
			return false;
		}
		CastChecked<UBoxComponent>(Room->GetRootComponent())->SetBoxExtent(FVector(5500, 5500, 2000));
		*FindFProperty<FStructProperty>(Room->GetClass(), TEXT("RoomTag"))->ContainerPtrToValuePtr<FGameplayTag>(Room) = Tag;
		Rooms->RegisterRoom(Room, Tag);
		UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr, TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
		UClass* DroneClass = Scenario == TEXT("TurretAttempt")
			? LoadClass<AEnemyBase>(nullptr, TEXT("/Game/Blueprints/Enemy/Turret/BP_RifleTurret.BP_RifleTurret_C"))
			: LoadClass<AEnemyBase>(nullptr, TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"));
		UClass* PartnerClass = LoadClass<APartnerCharacter>(nullptr, TEXT("/Game/Blueprints/Partner/BP_PartnerCharacter.BP_PartnerCharacter_C"));
		if (!ShooterClass || !DroneClass || !PartnerClass)
		{
			return false;
		}
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		int32 PlayerIndex = 0;
		AOutlierPlayerState* Pair = World->SpawnActor<AOutlierPlayerState>();
		if (!Pair || !World->GetGameState())
		{
			return false;
		}
		World->GetGameState()->AddPlayerState(Pair);
		TArray<APlayerController*> Players;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PC = It->Get();
			if (!PC)
			{
				continue;
			}
			if (bListenServer && PC->IsLocalController())
			{
				Players.Insert(PC, 0);
			}
			else if (PC->GetNetConnection())
			{
				Players.Add(PC);
			}
		}
		if (Players.Num() != (bListenServer ? 3 : 2))
		{
			return false;
		}
		// Listen의 첫 Pawn은 실제 로컬 호스트다. 남은 원격 한 명은 Partner, 다른 한 명은 관전자다.
		for (APlayerController* PC : Players)
		{
			if (PlayerIndex == 2)
			{
				PC->StartSpectatingOnly();
				// 서버의 관전 상태 변경만으로 원격 Controller의 상태 이름은 동기화되지 않는다.
				PC->ClientGotoState(NAME_Spectating);
				break;
			}
			APawn* Pawn = PlayerIndex == 0
				? static_cast<APawn*>(World->SpawnActor<AShooterCharacter>(ShooterClass, FVector(18000, 0, 100), FRotator(0, 180, 0), Spawn))
				: World->SpawnActor<APartnerCharacter>(PartnerClass, FVector(18000, 1000, 200), FRotator::ZeroRotator, Spawn);
			if (!Pawn)
			{
				return false;
			}
			Pawn->bAlwaysRelevant = true;
			Pawn->SetCanBeDamaged(false);
			PC->Possess(Pawn);
			if (PlayerIndex++ == 0)
			{
				Target = CastChecked<AShooterCharacter>(Pawn);
				Pair->SetShooterCharacter(Target.Get());
			}
			else
			{
				Partner = CastChecked<APartnerCharacter>(Pawn);
				Pair->SetPartnerCharacter(Partner.Get());
			}
		}
		bRolesVerified = Target.IsValid() && Target->GetController() == Players[0]
			&& Partner.IsValid() && Partner->GetController() == Players[1]
			&& Target->IsLocallyControlled() == bListenServer && !Partner->IsLocallyControlled();
		if (bListenServer)
		{
			LocalRole = TEXT("HostShooter");
			LocalPawnClass = Target->GetClass()->GetPathName();
		}
		else
		{
			LocalRole = TEXT("DedicatedServer");
			LocalPawnClass = TEXT("None");
		}
		const int32 Count = Scenario.StartsWith(TEXT("Crowd")) ? FCString::Atoi(*Scenario.Mid(5)) : 1;
		// BeginPlay의 실제 방/전투 등록이 기본 태그를 읽으므로 생성 완료 전에 소속을 준비한다.
		Spawn.bDeferConstruction = true;
		for (int32 Index = 0; Index < FMath::Clamp(Count, 1, 16); ++Index)
		{
			const FVector Position = Scenario == TEXT("Return") ? FVector(-3000, 0, 200)
				: (Count == 1 ? FVector(0, 0, 200) : FVector(-1000 - (Index / 4) * 180, (Index % 4 - 1.5f) * 160, 200));
			AEnemyBase* Drone = nullptr;
			if (Scenario == TEXT("Pool"))
			{
				UEnemyPoolSubsystem* Pool = World->GetSubsystem<UEnemyPoolSubsystem>();
				if (!Pool)
				{
					return false;
				}
				UEnemyPoolDefinition* PoolDefinition = NewObject<UEnemyPoolDefinition>(Pool);
				FEnemyPoolEntry& PoolEntry = PoolDefinition->Entries.AddDefaulted_GetRef();
				PoolEntry.EnemyClass = DroneClass;
				PoolEntry.PrewarmCount = 1;
				PoolEntry.MaxCount = 1;
				PoolContext.RoomTag = Tag;
				PoolContext.GameplayGeneration = 1;
				if (!Pool->PrewarmPool(PoolDefinition))
				{
					return false;
				}
				Drone = Pool->LeaseEnemy(DroneClass, FTransform(FRotator::ZeroRotator, Position), PoolContext);
				PoolOriginal = Drone;
				PreviousPoolLease = Drone ? Drone->GetPoolLeaseSerial() : INDEX_NONE;
			}
			else
			{
				Drone = World->SpawnActor<AEnemyBase>(DroneClass, Position, FRotator::ZeroRotator, Spawn);
			}
			if (!Drone)
			{
				return false;
			}
			Drone->bAlwaysRelevant = true;
			if (Scenario != TEXT("Pool"))
			{
				Drone->GetRoomTagComp()->AssignDefaultRoomTag(Tag);
				Drone->FinishSpawning(FTransform(FRotator::ZeroRotator, Position));
			}
			Drones.Add(Drone);
		}
		// 입력 배치만 준비한다. 자연 Sight/인지, 실제 저장 Tree와 무기/충돌은 계속 실행한다.
		return PlayerIndex == 2 && bRolesVerified;
	}

	void Finish(UWorld* World, bool bComplete)
	{
		if (bFinished)
		{
			return;
		}
		bFinished = true;
		const bool bServer = Role == TEXT("Server");
		const bool bCrowd = Scenario.StartsWith(TEXT("Crowd"));
		const bool bSearch = Scenario == TEXT("Search");
		const int32 ExpectedDrones = bCrowd ? FMath::Clamp(FCString::Atoi(*Scenario.Mid(5)), 1, 16) : 1;
		// 최초 한 발만으로 근접/높이 변경/차폐 후 복귀까지 성공했다고 기록하지 않는다.
		const bool bCombatPassed = FirstSight >= 0.0 && FirstCombat >= FirstSight && FirstShot >= FirstCombat
			&& TelegraphSamples > 0 && RecoverSamples > 0 && InvalidShots == 0
			&& InvalidShotPaths == 0 && CoverBlockedSamples > 0 && CloseShots > 0 && HeightShots > 0
			&& StageShots[0] > 0 && StageShots[1] > 0 && StageShots[2] > 0 && StageShots[4] > 0;
		const bool bSearchPassed = bSawSearch && SearchFootsteps > 0 && NoiseHandoffs > 0
			&& SettledAfterSearchSamples > 0 && ReacquireShots > 0 && InvalidShots == 0 && InvalidShotPaths == 0;
		const bool bTargetSelection = Scenario == TEXT("TargetSelection");
		const bool bTargetSelectionPassed = StageShots[0] > 0 && StageShots[2] > 0 && PartnerShots > 0
			&& HiddenShooterFootsteps > 0 && VisiblePartnerSamples > 0 && TargetMismatchSamples == 0
			&& InvalidShots == 0 && InvalidShotPaths == 0;
		bool bScenarioPassed = bTargetSelection ? bTargetSelectionPassed : (bSearch ? bSearchPassed : bCombatPassed);
		if (bCrowd)
		{
			bScenarioPassed = Progressed == ExpectedDrones && DoorCrossers.Num() == ExpectedDrones
				&& PenetrationSamples == 0 && InvalidDoorCrossings == 0;
		}
		else if (Scenario == TEXT("Bypass"))
		{
			bScenarioPassed = StageShots[0] > 0 && CoverBlockedSamples > 0 && BypassShots > 0 && MovingShots > 0;
		}
		else if (Scenario == TEXT("AlternatingTargets"))
		{
			bScenarioPassed = TargetChanges >= 4 && PartnerShots > 0 && Shots > PartnerShots;
		}
		else if (IsAttemptScenario())
		{
			bScenarioPassed = StageShots[0] > 0 && ApprovedAttemptShots > 0 && InvalidAttemptShots == 0 && NormalShots > 0;
		}
		else if (Scenario == TEXT("Return"))
		{
			bScenarioPassed = GuardSamples > 0 && ReturnSamples > 0 && FirstGuardAt >= 0.0
				&& FirstReturnAt - FirstGuardAt >= 2.8 && ReturnCompletedSamples > 0;
		}
		else if (Scenario == TEXT("Pool"))
		{
			bScenarioPassed = bPoolReturned && bPoolReused && PoolReacquireShots > 0 && ShotsWhileInactive == 0;
		}
		const bool bPassed = bComplete && bRolesVerified && Samples >= 20 && MaxDrones == ExpectedDrones
			&& InvalidShotPaths == 0 && InvalidShots == 0 && InvalidAttemptShots == 0 && ShotsWhileHidden == 0 && ShotsWhileInactive == 0 &&
			(bServer ? bScenarioPassed
			: Presentations > 0 && Frames > 0);
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetBoolField(TEXT("Passed"), bPassed);
		Json->SetBoolField(TEXT("Complete"), bComplete);
		Json->SetNumberField(TEXT("NetMode"), int32(World->GetNetMode()));
		const UNetDriver* NetDriver = World->GetNetDriver();
		// 요청한 실행 속도와 실제 드라이버 설정/관측 간격을 함께 남긴다. 설정은 변경하지 않는다.
		Json->SetStringField(TEXT("NetDriverClass"), NetDriver ? NetDriver->GetClass()->GetPathName() : TEXT("None"));
		Json->SetNumberField(TEXT("NetServerMaxTickRate"), NetDriver ? NetDriver->GetNetServerMaxTickRate() : 0);
#if DO_ENABLE_NET_TEST
		Json->SetBoolField(TEXT("PacketSimulationAvailable"), NetDriver != nullptr);
		Json->SetNumberField(TEXT("PacketSimulationLagMs"), NetDriver ? NetDriver->PacketSimulationSettings.PktLag : 0);
		Json->SetNumberField(TEXT("PacketSimulationLossPercent"), NetDriver ? NetDriver->PacketSimulationSettings.PktLoss : 0);
#else
		Json->SetBoolField(TEXT("PacketSimulationAvailable"), false);
#endif
		Json->SetBoolField(TEXT("Authority"), bServer);
		Json->SetStringField(TEXT("Scenario"), Scenario);
		Json->SetStringField(TEXT("VisualReview"), bServer && !bListenServer ? TEXT("NotApplicable") : TEXT("Pending"));
		Json->SetStringField(TEXT("Scope"), TEXT("Live saved BP, natural perception, saved StateTree/weapon/capsule; controlled placement/collision. Performed case only; no image, full Arena streaming or packaged acceptance implied."));
		Json->SetStringField(TEXT("PerformedCase"), Scenario);
		Json->SetStringField(TEXT("CaptureScope"), TEXT("Sparse time-spaced frames; visual motion review remains separate."));
		Json->SetNumberField(TEXT("CaptureLimit"), CaptureLimit);
		Json->SetBoolField(TEXT("ScenarioAssertionsPassed"), bServer && bScenarioPassed);
		Json->SetBoolField(TEXT("RolesVerified"), bRolesVerified);
		Json->SetStringField(TEXT("LocalRole"), LocalRole);
		Json->SetStringField(TEXT("LocalPawnClass"), LocalPawnClass);
		Json->SetNumberField(TEXT("ApprovedAttemptShots"), ApprovedAttemptShots);
		Json->SetNumberField(TEXT("AttemptEndpointBlockers"), AttemptEndpointBlockers);
		Json->SetNumberField(TEXT("AttemptEndpointActorSkips"), AttemptEndpointActorSkips);
		Json->SetNumberField(TEXT("InvalidAttemptShots"), InvalidAttemptShots);
		Json->SetNumberField(TEXT("NormalShots"), NormalShots);
		Json->SetNumberField(TEXT("ShotsWhileHidden"), ShotsWhileHidden);
		Json->SetNumberField(TEXT("ShotsWhileInactive"), ShotsWhileInactive);
		Json->SetNumberField(TEXT("BypassShots"), BypassShots);
		Json->SetNumberField(TEXT("MovingShotsBeforeArrival"), MovingShots);
		Json->SetNumberField(TEXT("TargetChanges"), TargetChanges);
		Json->SetNumberField(TEXT("YieldSamples"), YieldSamples);
		Json->SetNumberField(TEXT("ReservationConflictSamples"), ReservationConflictSamples);
		Json->SetNumberField(TEXT("GeneralGuardSamples"), GuardSamples);
		Json->SetNumberField(TEXT("ReturnSamples"), ReturnSamples);
		Json->SetNumberField(TEXT("ReturnGuardSamples"), ReturnGuardSamples);
		Json->SetNumberField(TEXT("ReturnCompletedSamples"), ReturnCompletedSamples);
		Json->SetNumberField(TEXT("FirstGuardAt"), FirstGuardAt);
		Json->SetNumberField(TEXT("FirstReturnAt"), FirstReturnAt);
		Json->SetBoolField(TEXT("PoolReturned"), bPoolReturned);
		Json->SetBoolField(TEXT("PoolSameActorReused"), bPoolReused);
		Json->SetNumberField(TEXT("PoolReacquireShots"), PoolReacquireShots);
		Json->SetNumberField(TEXT("Samples"), Samples);
		Json->SetNumberField(TEXT("Frames"), Frames);
		Json->SetNumberField(TEXT("Shots"), Shots);
		Json->SetNumberField(TEXT("VisiblePartnerSamples"), VisiblePartnerSamples);
		Json->SetNumberField(TEXT("TargetMismatchSamples"), TargetMismatchSamples);
		Json->SetNumberField(TEXT("PartnerShots"), PartnerShots);
		Json->SetNumberField(TEXT("HiddenShooterFootsteps"), HiddenShooterFootsteps);
		Json->SetNumberField(TEXT("Presentations"), Presentations);
		Json->SetNumberField(TEXT("InvalidShotAngles"), InvalidShots);
		Json->SetNumberField(TEXT("InvalidShotPaths"), InvalidShotPaths);
		Json->SetNumberField(TEXT("CoverBlockedSamples"), CoverBlockedSamples);
		Json->SetNumberField(TEXT("CloseShots"), CloseShots);
		Json->SetNumberField(TEXT("HeightShots"), HeightShots);
		Json->SetNumberField(TEXT("Footsteps"), Footsteps);
		Json->SetNumberField(TEXT("SearchFootsteps"), SearchFootsteps);
		Json->SetNumberField(TEXT("NoiseHandoffsBeforeDeadline"), NoiseHandoffs);
		Json->SetNumberField(TEXT("SettledAfterSearchSamples"), SettledAfterSearchSamples);
		Json->SetNumberField(TEXT("ReacquireShots"), ReacquireShots);
		Json->SetNumberField(TEXT("FirstSight"), FirstSight);
		Json->SetNumberField(TEXT("FirstCombat"), FirstCombat);
		Json->SetNumberField(TEXT("FirstTelegraph"), FirstTelegraph);
		Json->SetNumberField(TEXT("FirstShot"), FirstShot);
		Json->SetNumberField(TEXT("TelegraphSamples"), TelegraphSamples);
		Json->SetNumberField(TEXT("FiringSamples"), FiringSamples);
		Json->SetNumberField(TEXT("RecoverSamples"), RecoverSamples);
		Json->SetNumberField(TEXT("SearchSamples"), SearchSamples);
		Json->SetNumberField(TEXT("MaxDrones"), MaxDrones);
		Json->SetNumberField(TEXT("Progressed"), Progressed);
		Json->SetNumberField(TEXT("PenetrationSamples"), PenetrationSamples);
		Json->SetNumberField(TEXT("DoorCrossers"), DoorCrossers.Num());
		Json->SetNumberField(TEXT("InvalidDoorCrossings"), InvalidDoorCrossings);
		TArray<TSharedPtr<FJsonValue>> StageCounts;
		for (const int32 Count : StageShots)
		{
			StageCounts.Add(MakeShared<FJsonValueNumber>(Count));
		}
		Json->SetArrayField(TEXT("StageShots"), StageCounts);
		FrameTimes.Sort();
		const int32 Middle = FrameTimes.Num() / 2;
		const double MedianWorldDelta = FrameTimes.IsEmpty() ? 0.0
			: (FrameTimes.Num() % 2 == 0 ? (FrameTimes[Middle - 1] + FrameTimes[Middle]) * 0.5 : FrameTimes[Middle]);
		Json->SetNumberField(TEXT("MedianWorldDeltaMs"), MedianWorldDelta * 1000.0);
		Json->SetNumberField(TEXT("P95WorldDeltaMs"), FrameTimes.IsEmpty() ? 0.0 : FrameTimes[FMath::Min(FrameTimes.Num() - 1, FMath::FloorToInt(FrameTimes.Num() * .95))] * 1000.0);
		Json->SetNumberField(TEXT("P99WorldDeltaMs"), FrameTimes.IsEmpty() ? 0.0 : FrameTimes[FMath::Min(FrameTimes.Num() - 1, FMath::FloorToInt(FrameTimes.Num() * .99))] * 1000.0);
		Json->SetNumberField(TEXT("ObservedFrameCount"), FrameTimes.Num());
		TickCosts.Sort();
		Json->SetNumberField(TEXT("P95ObservedTickWallMsIncludesProbe"), TickCosts.IsEmpty() ? 0.0 : TickCosts[FMath::Min(TickCosts.Num() - 1, FMath::FloorToInt(TickCosts.Num() * .95))] * 1000.0);
		FString Text;
		FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text));
		FFileHelper::SaveStringToFile(Text, *(Output / TEXT("Summary.json")));
		UE_LOG(LogTemp, Display, TEXT("[CombatNetwork] Complete=%d Passed=%d Shots=%d Presentations=%d Samples=%d"), bComplete, bPassed, Shots, Presentations, Samples);
		CompletedAt = FPlatformTime::Seconds();
	}

	void Before(UWorld* World, ELevelTick, float Delta)
	{
		if (bFinished && Role == TEXT("Server") && ActiveWorld.Get() == World)
		{
			const bool bClientsDone = IFileManager::Get().FileExists(*(RunRoot() / TEXT("Client1/Summary.json")))
				&& IFileManager::Get().FileExists(*(RunRoot() / TEXT("Client2/Summary.json")));
			if (bClientsDone || FPlatformTime::Seconds() - CompletedAt > 30.0)
			{
				FPlatformMisc::RequestExit(false);
			}
			return;
		}
		if (!Accept(World))
		{
			return;
		}
		TickStarted = FPlatformTime::Seconds();
		if (!bScene)
		{
			Scene(World);
		}
		if (StartedAt >= 0.0)
		{
			FrameTimes.Add(Delta);
		}
		if (FPlatformTime::Seconds() - WallStarted > Duration + 300.0)
		{
			Finish(World, false);
			return;
		}
		const double Now = World->GetTimeSeconds();
		if (Role == TEXT("Server"))
		{
			if (!World->GetNetDriver() || World->GetNetDriver()->ClientConnections.Num() < 2)
			{
				return;
			}
			if (ConnectedAt < 0.0)
			{
				ConnectedAt = Now;
			}
			if (Now - ConnectedAt < 5.0)
			{
				return;
			}
			if (!bSetup)
			{
				bSetup = true;
				if (!Setup(World))
				{
					Finish(World, false);
					return;
				}
			}
			if (StartedAt < 0.0)
			{
				// 실제 렌더 클라이언트가 BP/메시를 받은 다음 최초 감지를 시작한다.
				if (!IFileManager::Get().FileExists(*(RunRoot() / TEXT("Client1/Ready.txt")))
					|| !IFileManager::Get().FileExists(*(RunRoot() / TEXT("Client2/Ready.txt"))))
				{
					return;
				}
				StartedAt = Now;
				if (Scenario == TEXT("Pool"))
				{
					// Entry 검증은 Arena 개시 권한을 다루지 않는다. 등장 완료 뒤 증원 방만 열고 공유 시야는 만들지 않는다.
					// 최초 대여 Pawn은 아직 NonCombat이며 아래 실제 노출 뒤 자기 Sight로 전투에 합류한다.
					if (UEnemyRoomSubsystem* EnemyRooms = World->GetSubsystem<UEnemyRoomSubsystem>())
					{
						EnemyRooms->SetActiveRoomTargetForTesting(PoolContext.RoomTag, FVector::ZeroVector, false);
					}
				}
				Target->GetCharacterMovement()->StopMovementImmediately();
				// 귀환 검사는 먼 원래 방어 위치에서 시작한다. 첫 단서도 실제 Sight 반경 안에 둬야 수색/귀환이 성립한다.
				const double InitialTargetX = Scenario == TEXT("Return") ? -1500.0 : Scenario.StartsWith(TEXT("Crowd")) ? 1800.0 : 600.0;
				Target->TeleportTo(FVector(InitialTargetX, 0, 100), Target->GetActorRotation(), false, true);
				FFileHelper::SaveStringToFile(TEXT("Started"), *(Output / TEXT("Started.txt")));
			}
			const double Age = Now - StartedAt;
			if (Scenario.StartsWith(TEXT("Crowd")) && Stage < 0 && Age >= 3.0)
			{
				// 먼저 사거리 밖에서 자연 발견을 성립시킨 뒤 문 너머 목표로 이동한다.
				// 초기 위치를 그대로 두면 문 앞에서 정상 사거리 사격을 시작해 통과 검사가 성립하지 않는다.
				Stage = 0;
				Target->TeleportTo(FVector(4000, 0, 100), Target->GetActorRotation(), false, true);
			}
			const int32 NewStage = FMath::FloorToInt(Age / (Scenario == TEXT("AlternatingTargets") ? 2.0 : 6.0));
			if (NewStage != Stage && Target.IsValid() && !Scenario.StartsWith(TEXT("Crowd")))
			{
				Stage = NewStage;
				if (Scenario == TEXT("Bypass") || IsAttemptScenario())
				{
					if (Stage == 1 && !Drones.IsEmpty() && Drones[0].IsValid())
					{
						AEnemyBase* Enemy = Drones[0].Get();
						// 정상 선회가 이미 쓴 회차와 새 차폐 대응을 분리한다. 실제 관측 목표를 의미 있게 옮겨
						// 생산 코드의 목표 변화 규칙으로 새 회차를 열며, 예산/시도 권한은 직접 변경하지 않는다.
						Target->GetCharacterMovement()->StopMovementImmediately();
						Target->TeleportTo(Target->GetActorLocation() + FVector(0, 350, 0), Target->GetActorRotation(), false, true);
						const FVector Center = FMath::Lerp(Enemy->GetCombatShotOrigin(), Target->GetActorLocation(), 0.5f);
						Cover = Block(World, Center, IsAttemptScenario() ? FVector(20, 5500, 2000) : FVector(30, 180, 450));
						if (Cover.IsValid())
						{
							// 눈의 관측과 탄환/이동 차단을 분리한 통제 배치다. 대안 소진 판정은 실제 Tree가 수행한다.
							Cover->GetStaticMeshComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
						}
						if (Scenario == TEXT("Attempt"))
						{
							// 평면 벽만 두면 유효한 후퇴 경유점이 남는다. 캡슐 시작은 비우고 주변 6면을 실제로 막는다.
							// 바깥 EQS 후보는 유지하되 그곳까지의 자연 경로 검사가 물리 실패를 판정하게 한다.
							const UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent();
							const FVector CageCenter = Capsule->GetComponentLocation();
							const FVector Inner(Capsule->GetScaledCapsuleRadius() + 30.0f,
								Capsule->GetScaledCapsuleRadius() + 30.0f, Capsule->GetScaledCapsuleHalfHeight() + 30.0f);
							for (int32 Axis = 0; Axis < 3; ++Axis)
							{
								for (const float Sign : {-1.0f, 1.0f})
								{
									FVector Offset = FVector::ZeroVector;
									Offset[Axis] = Sign * (Inner[Axis] + 10.0f);
									FVector WallExtent = Inner + FVector(20.0f);
									WallExtent[Axis] = 10.0f;
									AStaticMeshActor* Wall = Block(World, CageCenter + Offset, WallExtent);
									if (Wall)
									{
										Wall->GetStaticMeshComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
										AttemptCage.Add(Wall);
									}
								}
							}
							BlockAttemptEndpoints(World, *Enemy);
						}
					}
					else if (Stage == 3 && Cover.IsValid())
					{
						Cover->Destroy();
						Cover.Reset();
						for (const TWeakObjectPtr<AStaticMeshActor>& Wall : AttemptCage)
						{
							if (Wall.IsValid())
							{
								Wall->Destroy();
							}
						}
						AttemptCage.Reset();
					}
				}
				else if (Scenario == TEXT("AlternatingTargets"))
				{
					// 2초 교대는 준비 이력 3초 보존 안에서 자연 감지/선택/무기 회복을 함께 실행한다.
					const bool bShooterTurn = Stage % 2 == 0;
					Target->GetCharacterMovement()->StopMovementImmediately();
					Target->TeleportTo(bShooterTurn ? FVector(600, -150, 100) : FVector(18000, 0, 100), Target->GetActorRotation(), false, true);
					Partner->TeleportTo(bShooterTurn ? FVector(18000, 1000, 200) : FVector(600, 150, 220), Partner->GetActorRotation(), false, true);
				}
				else if (Scenario == TEXT("Return"))
				{
					if (Stage == 1)
					{
						Target->GetCharacterMovement()->StopMovementImmediately();
						Target->TeleportTo(FVector(18000, 0, 100), Target->GetActorRotation(), false, true);
					}
				}
				else if (Scenario == TEXT("Pool"))
				{
					UEnemyPoolSubsystem* Pool = World->GetSubsystem<UEnemyPoolSubsystem>();
					if (Stage == 2 && Pool && PoolOriginal.IsValid())
					{
						// 실제 반환/재대여 사이에 빈 구간을 두어 이전 무기의 지연 발사를 별도로 검사한다.
						bPoolReturned = Pool->ReturnEnemy(PoolOriginal.Get(), PoolContext.GameplayGeneration, PreviousPoolLease);
					}
					else if (Stage == 3 && Pool && PoolOriginal.IsValid() && bPoolReturned)
					{
						AEnemyBase* Reused = Pool->LeaseEnemy(PoolOriginal->GetClass(), FTransform(FVector(0, 0, 200)), PoolContext);
						bPoolReused = Reused == PoolOriginal.Get() && Reused && Reused->GetPoolLeaseSerial() != PreviousPoolLease;
						if (Reused)
						{
							Reused->bAlwaysRelevant = true;
						}
					}
				}
				else if (Scenario == TEXT("TargetSelection"))
				{
					// 배치와 실제 입력만 제어한다. Sight/청각/대상 채택/저장 Tree/무기는 생산 경로로 진행한다.
					if (Stage == 1)
					{
						Cover = Block(World, FVector(400, 0, 500), FVector(40, 2000, 500));
						Partner->TeleportTo(FVector(150, 500, 220), FRotator::ZeroRotator, false, true);
					}
					else if (Stage == 2 && Cover.IsValid())
					{
						Cover->Destroy();
						Cover.Reset();
					}
				}
				else if (Scenario == TEXT("Search"))
				{
					// 실제 높이 단서 -> 차폐/유한 수색 -> 원격 발소리 인계 -> 경계 복귀 -> 직접 재발견.
					if (Stage == 1)
					{
						Target->TeleportTo(FVector(900, 0, 300), Target->GetActorRotation(), false, true);
					}
					else if (Stage == 2)
					{
						Target->TeleportTo(FVector(800, 0, 100), Target->GetActorRotation(), false, true);
						Cover = Block(World, FVector(400, 0, 500), FVector(40, 2000, 500));
					}
					else if (Stage == 3 && !Drones.IsEmpty() && Drones[0].IsValid())
					{
						// 수색이 시작된 뒤에도 실제 발소리가 청각 범위 안에서 발생하도록 근거리 차폐를 배치한다.
						const FVector EnemyLocation = Drones[0]->GetActorLocation();
						if (Cover.IsValid())
						{
							Cover->Destroy();
						}
						Cover = Block(World, FVector(EnemyLocation.X + 150, EnemyLocation.Y, 500), FVector(40, 2000, 500));
						Target->GetCharacterMovement()->StopMovementImmediately();
						Target->TeleportTo(FVector(EnemyLocation.X + 300, EnemyLocation.Y, 100), Target->GetActorRotation(), false, true);
					}
					else if (Stage == 5)
					{
						Target->TeleportTo(FVector(18000, 0, 100), Target->GetActorRotation(), false, true);
					}
					else if (Stage == 6)
					{
						if (Cover.IsValid())
						{
							Cover->Destroy();
						}
						Target->GetCharacterMovement()->StopMovementImmediately();
						Target->TeleportTo(FVector(600, 0, 100), Target->GetActorRotation(), false, true);
					}
				}
				else if (Stage == 1)
				{
					const FVector NearDrone = Drones[0]->GetActorLocation() + FVector(200, 0, -100);
					Target->TeleportTo(NearDrone, Target->GetActorRotation(), false, true);
				}
				else if (Stage == 2)
				{
					Target->GetCharacterMovement()->StopMovementImmediately();
					Target->TeleportTo(FVector(900, 0, 300), Target->GetActorRotation(), false, true);
				}
				else if (Stage == 3)
				{
					Target->TeleportTo(FVector(800, 0, 100), Target->GetActorRotation(), false, true);
					Cover = Block(World, FVector(400, 0, 250), FVector(40, 2000, 250));
				}
				else if (Stage == 4 && Cover.IsValid())
				{
					Cover->Destroy();
				}
			}
			if (Age >= Duration)
			{
				Finish(World, true);
			}
			if (bListenServer)
			{
				DriveLocalPlayer(World, Now);
			}
		}
		else
		{
			if (APlayerController* PC = World->GetFirstPlayerController())
			{
				if (Camera.IsValid())
				{
					if (!Drones.IsEmpty() && Drones[0].IsValid())
					{
						const FVector Focus = Drones[0]->GetActorLocation() + FVector(300, 0, -50);
						const FVector Eye = Focus + FVector(-700, -1400, 650);
						Camera->SetActorLocationAndRotation(Eye, (Focus - Eye).Rotation());
					}
					// Shooter/Partner의 실제 소유 시점은 유지하고 관전자만 전체 장면을 촬영한다.
					if (!PC->GetPawn() || PC->GetStateName() == NAME_Spectating)
					{
						PC->SetViewTarget(Camera.Get());
					}
				}
				DriveLocalPlayer(World, Now);
			}
			// GPU 부하로 월드 시간이 늦어져도 서버와 같은 종료 경계를 관측한다.
			if (StartedAt >= 0.0 && IFileManager::Get().FileExists(*(RunRoot() / TEXT("Server/Summary.json"))))
			{
				Finish(World, true);
			}
		}
	}

	void After(UWorld* World, ELevelTick, float)
	{
		if (Accept(World) && StartedAt >= 0.0)
		{
			// 실제 월드 Delta와 관측 비용을 구분한다. 별도 벽시계 비용에는 검사 부하도 포함한다.
			TickCosts.Add(FPlatformTime::Seconds() - TickStarted);
		}
		if (!Accept(World) || World->GetTimeSeconds() - LastSample < 0.1)
		{
			return;
		}
		LastSample = World->GetTimeSeconds();
		if (Role != TEXT("Server"))
		{
			APlayerController* LocalPC = World->GetFirstPlayerController();
			APawn* LocalPawn = LocalPC ? LocalPC->GetPawn() : nullptr;
			LocalRole = TEXT("Unassigned");
			LocalPawnClass = LocalPawn ? LocalPawn->GetClass()->GetPathName() : TEXT("None");
			if (Cast<AShooterCharacter>(LocalPawn))
			{
				LocalRole = TEXT("RemoteShooter");
			}
			else if (Cast<APartnerCharacter>(LocalPawn))
			{
				LocalRole = TEXT("RemotePartner");
			}
			else if (bListenServer && LocalPC && LocalPC->GetStateName() == NAME_Spectating)
			{
				LocalRole = TEXT("Observer");
			}
			bRolesVerified = LocalPC && LocalPC->IsLocalController()
				&& (LocalRole == TEXT("Observer") || (LocalPawn && LocalPawn->IsLocallyControlled()
					&& (LocalRole == TEXT("RemotePartner") || (!bListenServer && LocalRole == TEXT("RemoteShooter")))));
		}
		if (Role == TEXT("Server") && Target.IsValid())
		{
			const UOutlierArenaSubsystem* Arena = World->GetSubsystem<UOutlierArenaSubsystem>();
			FFileHelper::SaveStringToFile(FString::Printf(TEXT("%.3f,%d,%s,%s,%s\n"), LastSample, Stage,
				*Target->GetActorLocation().ToCompactString(), *Target->GetCurrentRoomTag().ToString(),
				*GetNameSafe(Arena ? Arena->GetArenaLoadedLevel() : nullptr)), *(Output / TEXT("Players.csv")),
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
		}
		if (Role != TEXT("Server"))
		{
			Drones.Reset();
			for (TActorIterator<AEnemyBase> It(World); It; ++It)
			{
				if (!It->IsHidden())
				{
					Drones.Add(*It);
				}
			}
			if (!Drones.IsEmpty())
			{
				FFileHelper::SaveStringToFile(TEXT("Ready"), *(Output / TEXT("Ready.txt")));
				if (StartedAt < 0.0 && IFileManager::Get().FileExists(*(RunRoot() / TEXT("Server/Started.txt"))))
				{
					StartedAt = LastSample;
				}
			}
		}
		MaxDrones = FMath::Max(MaxDrones, Drones.Num());
		FString Rows;
		FString TacticsRows;
		if (!bHeader)
		{
			TacticsRows = TEXT("time,actor,episodeSerial,observing,canResumeBudget,aimObservationValid,aimEpoch,aimObservedAt,queryActive,queryResult,capturedSerial,queryObserving,selectionPending,nextCandidate,candidates,checkedCandidates,remainingNodes,remainingChecks,remainingMs,selectionPath,reservationRequest,reservationCurrent,reservationConflict,yieldStartedAt,nextYieldCheckAt,yieldCandidate,limitedCandidate,requestId,queryStartedAt,queryRunning,searchMode,reachability,orbit,retreat,sightReposition,pathPresent,pathStatus,pathChecks,pathNodes,pathMs,pathStartBlocked\n");
			Rows = TEXT("time,netMode,authority,actor,x,y,z,speed,combat,visible,phase,pitch,aimOwned,observation,target,ammo,recovery,preparation,episode,searchActive,searchPoints,searchDeadline,returnRequests,states\n");
			bHeader = true;
		}
		Progressed = 0;
		for (const auto& Weak : Drones)
		{
			AEnemyBase* Drone = Weak.Get();
			if (!Drone)
			{
				continue;
			}
			++Samples;
			const double Age = LastSample - StartedAt;
			const EEnemyAttackPhase Phase = Drone->GetAttackPhase();
			TelegraphSamples += Phase == EEnemyAttackPhase::Telegraph;
			FiringSamples += Phase == EEnemyAttackPhase::Firing;
			RecoverSamples += Phase == EEnemyAttackPhase::Recover;
			SearchSamples += Drone->GetSearchEpisode().bActive;
			if (Drone->IsPlayerCurrentlyVisible() && FirstSight < 0.0)
			{
				FirstSight = Age;
			}
			if (Drone->GetCombatState() == EEnemyCombatState::Combat && FirstCombat < 0.0)
			{
				FirstCombat = Age;
			}
			if (Phase == EEnemyAttackPhase::Telegraph && FirstTelegraph < 0.0)
			{
				FirstTelegraph = Age;
			}
			Progressed += Drone->GetActorLocation().X > 150;
			const FVector P = Drone->GetActorLocation();
			if (Role == TEXT("Server"))
			{
				const FEnemySearchEpisode& Episode = Drone->GetSearchEpisode();
				GuardSamples += Episode.bActive && Episode.bLocalGuard && Episode.InvestigationSerial == 0;
				if (Episode.bActive && Episode.bLocalGuard && Episode.InvestigationSerial == 0 && FirstGuardAt < 0.0)
				{
					FirstGuardAt = LastSample;
				}
				ReturnSamples += Drone->GetCombatState() == EEnemyCombatState::Returning;
				ReturnGuardSamples += Drone->IsReturnHomeGuarding();
				if (Drone->GetCombatState() == EEnemyCombatState::Returning && FirstReturnAt < 0.0)
				{
					FirstReturnAt = LastSample;
				}
				// 귀환 상태 진입만으로 완료를 인정하지 않는다. 원래 방어점 도착과 비전투 복귀를 함께 본다.
				ReturnCompletedSamples += FirstReturnAt >= 0.0 && LastSample > FirstReturnAt
					&& Drone->GetCombatState() == EEnemyCombatState::NonCombat
					&& FVector::DistSquared(Drone->GetActorLocation(), Drone->GetPatrolPointA()) <= FMath::Square(5.0f);
				TWeakObjectPtr<AActor>& PreviousTarget = PreviousTargets.FindOrAdd(Drone);
				if (PreviousTarget.IsValid() && Drone->GetTrackedTarget() && PreviousTarget.Get() != Drone->GetTrackedTarget())
				{
					++TargetChanges;
				}
				// 짧은 무대상 구간 뒤의 A→B도 세되, 다른 적의 대상을 교체로 잘못 세지 않는다.
				if (Drone->GetTrackedTarget())
				{
					PreviousTarget = Drone->GetTrackedTarget();
				}
				FVector YieldStart, YieldEnd;
				uint64 YieldSerial = 0;
				YieldSamples += Drone->GetCombatYieldContext(YieldStart, YieldEnd, YieldSerial);
			}
			if (Role == TEXT("Server") && Scenario.StartsWith(TEXT("Crowd")))
			{
				// 최종 X만으로 벽 위/옆 우회를 좁은 문 통과로 세지 않는다. 통과 평면의 캡슐 전체를 검사한다.
				if (const FVector* Previous = PreviousLocations.Find(Weak); Previous && Previous->X <= 0.0 && P.X > 0.0)
				{
					const FVector Crossing = FMath::Lerp(*Previous, P, -Previous->X / (P.X - Previous->X));
					const UCapsuleComponent* Capsule = Drone->GetCapsuleComponent();
					const bool bInsideDoor = FMath::Abs(Crossing.Y) + Capsule->GetScaledCapsuleRadius() <= 220.0
						&& Crossing.Z + Capsule->GetScaledCapsuleHalfHeight() <= 1000.0;
					if (bInsideDoor)
					{
						DoorCrossers.Add(Weak);
					}
					else
					{
						++InvalidDoorCrossings;
					}
				}
				PreviousLocations.Add(Weak, P);
			}
			if (Role == TEXT("Server") && Cover.IsValid() && Target.IsValid() && Drone->GetCurrentWeapon())
			{
				// 실제 현재 무기 사선의 차폐를 관측한다. 우회 뒤 열린 사격을 일괄 실패시키지 않는다.
				CoverBlockedSamples += !Drone->GetCurrentWeapon()->HasClearTargetAttackPath(Target.Get(), Drone->GetCombatAimPoint(Target.Get()));
			}
			const FEnemySearchEpisode& Search = Drone->GetSearchEpisode();
			if (Role == TEXT("Server") && Scenario == TEXT("Search"))
			{
				const EEnemyPerceptionGoalType Goal = Drone->GetPerceptionGoal().Type;
				if (Search.bActive)
				{
					bSawSearch = true;
					LastSearchDeadline = Search.EndsAt;
				}
				if (PreviousGoal == EEnemyPerceptionGoalType::Search && Goal == EEnemyPerceptionGoalType::InvestigateNoise
					&& LastSample < LastSearchDeadline && Drone->GetActiveNoiseInvestigationClue().Source.Get() == Target.Get())
				{
					++NoiseHandoffs;
				}
				SettledAfterSearchSamples += bSawSearch && Stage >= 4 && Stage < 6
					&& ((Search.bActive && Search.bLocalGuard) || (Drone->GetCombatState() == EEnemyCombatState::NonCombat && !Search.bActive));
				PreviousGoal = Goal;
				FFileHelper::SaveStringToFile(FString::Printf(TEXT("%.3f,%d,%llu,%.3f,%d\n"), LastSample, int32(Goal),
					Drone->GetPerceptionGoal().NoiseClueSerial, Drone->GetPerceptionGoal().InformationAt, Search.bActive),
					*(Output / TEXT("Clues.csv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
			}
			if (Role == TEXT("Server") && Scenario == TEXT("TargetSelection"))
			{
				const AEnemyAIController* AI = Cast<AEnemyAIController>(Drone->GetController());
				const bool bVisiblePartner = AI && AI->IsTargetDirectlyVisible(Partner.Get())
					&& !AI->IsTargetDirectlyVisible(Target.Get());
				VisiblePartnerSamples += bVisiblePartner;
				const FEnemyPerceptionGoal& Goal = Drone->GetPerceptionGoal();
				TargetMismatchSamples += (Goal.Type == EEnemyPerceptionGoalType::Combat && Goal.Target.Get() != Drone->GetTrackedTarget())
					|| (bVisiblePartner && Drone->GetTrackedTarget() != Partner.Get());
			}
			FString States;
			if (Role == TEXT("Server"))
			{
				const EnemyCombatTesting::FSnapshot Snapshot = EnemyCombatTesting::ReadComponent(
					*CastChecked<UEnemyStateTreeComponent>(Drone->GetStateTreeComponent()));
				States = Snapshot.Describe();
				States.ReplaceInline(TEXT(","), TEXT(";"));
				const auto* Task = Snapshot.FindTask<FEnemyRunEnvQueryTask>();
				const auto* Query = Task ? Task->Data.GetPtr<const FEnemyRunEnvQueryTaskInstanceData>() : nullptr;
				const AShooterCharacter* Shooter = Cast<AShooterCharacter>(Drone->GetTrackedTarget());
				FOutlierSightObservation Observation;
				const bool bAimObservationValid = Shooter && Shooter->GetObservedAimPoints(Drone, Observation);
				TacticsRows += FString::Printf(TEXT("%.3f,%s,%llu,%d,%d,%d,%u,%.3f,%d"),
					LastSample, *Drone->GetName(), Drone->GetCombatTacticalEpisodeSerial(),
					Drone->IsCombatObservationWaiting(), Drone->CanResumeCombatTacticalBudget(),
					bAimObservationValid, Observation.Epoch, Observation.ObservedAt, Query != nullptr);
				if (Query)
				{
					ReservationConflictSamples += Query->bCombatReservationConflict;
					TacticsRows += FString::Printf(TEXT(",%d,%llu,%d,%d,%d,%d,%d,%d,%d,%.3f,%d,%llu,%d,%d,%.3f,%.3f,%d,%d,%d,%.3f,%d,%d,%d,%d,%d,%d"),
						int32(Query->QueryResult), Query->CapturedCombatSerial, Query->bCombatObservationWaiting,
						Query->bCombatSelectionPending, Query->NextCombatCandidate, Query->CombatCandidates.Num(),
						Query->CheckedPathCandidates, Query->CombatRemainingNodes, Query->CombatRemainingChecks,
						Query->CombatRemainingMilliseconds, int32(Query->SelectionPathStatus), Query->CombatReservationRequest,
						Drone->IsCombatReservationRequestCurrent(Query->CombatReservationRequest), Query->bCombatReservationConflict,
						Query->CombatYieldStartedAt, Query->NextCombatYieldCheckAt, Query->CombatYieldCandidate,
						Query->bCombatHadLimitedCandidate, Query->RequestId, Query->QueryStartedAt, Query->bActive,
						int32(Query->SearchMode), Query->bValidateReachability, Query->bValidateOrbitPath,
						Query->bValidateRetreatPath, Query->bCapturedForSightReposition);
				}
				else
				{
					TacticsRows += TEXT(",-1,0,0,0,0,0,0,0,0,0,-1,0,0,0,-1,0,-1,0,-1,0,0,-1,0,0,0,0");
				}
				const FFlightPathResult* Path = Query && Query->CombatPathSearch ? &Query->CombatPathSearch->GetResult() : nullptr;
				TacticsRows += FString::Printf(TEXT(",%d,%d,%d,%d,%.3f,%d\n"), Path != nullptr,
					Path ? int32(Path->Status) : -1, Path ? Path->CollisionChecks : 0,
					Path ? Path->SearchNodes : 0, Path ? Path->ElapsedMilliseconds : 0.0, Path && Path->bStartBlocked);
			}
			Rows += FString::Printf(TEXT("%.3f,%d,%d,%s,%.2f,%.2f,%.2f,%.2f,%d,%d,%d,%.2f,%d,%u,%s,%d,%.3f,%.3f,%llu,%d,%d,%.3f,%d,%s\n"),
				LastSample, int32(World->GetNetMode()), Drone->HasAuthority(), *Drone->GetName(), P.X, P.Y, P.Z,
				Drone->GetVelocity().Size(), int32(Drone->GetCombatState()), Drone->IsPlayerCurrentlyVisible(), int32(Phase),
				Drone->GetBaseAimRotation().Pitch, Drone->IsCombatAimPresentationOwned(), Drone->GetCombatAimObservationSerial(),
				*GetNameSafe(Drone->GetTrackedTarget()), Drone->GetCurrentWeapon() ? Drone->GetCurrentWeapon()->GetCurrentAmmo() : -1,
				Drone->GetTargetAttackRecoveryRemaining(), Drone->GetTargetAttackPreparationRemaining(), Search.Serial,
				Search.bActive, Search.PointIndex, Search.EndsAt, Drone->GetReturnHomeRequestCount(), *States);
		}
		// 캡슐의 실제 중첩만 센다. 메시 장식 겹침이나 예약 성공을 물리 통과로 대신하지 않는다.
		for (int32 A = 0; A < Drones.Num(); ++A)
		{
			for (int32 B = A + 1; B < Drones.Num(); ++B)
			{
				if (Drones[A].IsValid() && Drones[B].IsValid())
				{
					const FVector Difference = Drones[A]->GetActorLocation() - Drones[B]->GetActorLocation();
					const float Radius = Drones[A]->GetCapsuleComponent()->GetScaledCapsuleRadius() + Drones[B]->GetCapsuleComponent()->GetScaledCapsuleRadius();
					const float Height = Drones[A]->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + Drones[B]->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
					const float Gap = FMath::Max(0.0f, FMath::Abs(Difference.Z) - (Height - Radius));
					PenetrationSamples += Difference.SizeSquared2D() + Gap * Gap < FMath::Square(Radius - 1.0f);
				}
			}
		}
		FFileHelper::SaveStringToFile(Rows, *(Output / TEXT("Samples.csv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
		if (Role == TEXT("Server"))
		{
			FFileHelper::SaveStringToFile(TacticsRows, *(Output / TEXT("Tactics.csv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
		}
		const double CaptureInterval = FMath::Max(0.2, double(Duration) / FMath::Max(1, CaptureLimit - 1));
		if ((Role != TEXT("Server") || bListenServer) && StartedAt >= 0.0
			&& Frames < CaptureLimit && LastSample - LastFrame >= CaptureInterval)
		{
			LastFrame = LastSample;
			const FString FileName = FString::Printf(TEXT("Frame_%05d.png"), Frames);
			FFileHelper::SaveStringToFile(FString::Printf(TEXT("%s,%.6f,%.6f\n"), *FileName, LastSample,
				World->GetGameState() ? World->GetGameState()->GetServerWorldTimeSeconds() : -1.0f),
				*(Output / TEXT("Frames.csv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
			FScreenshotRequest::RequestScreenshot(Output / TEXT("Frames") / FString::Printf(TEXT("Frame_%05d.png"), Frames++), false, false);
		}
	}

	void Shot(const FWallShotObservation& Shot)
	{
		if (bFinished || !Shot.Weapon || Shot.Weapon->GetWorld() != ActiveWorld.Get())
		{
			return;
		}
		AEnemyBase* Owner = Cast<AEnemyBase>(Shot.Weapon->GetOwner());
		if (!Owner)
		{
			return;
		}
		if (Shot.bPresentation)
		{
			++Presentations;
			return;
		}
		++Shots;
		PartnerShots += Owner->GetTrackedTarget() == Partner.Get() && Partner.IsValid();
		ReacquireShots += Scenario == TEXT("Search") && Stage >= 6;
		if (Stage >= 0 && Stage < UE_ARRAY_COUNT(StageShots))
		{
			++StageShots[Stage];
		}
		if (FirstShot < 0.0)
		{
			FirstShot = ActiveWorld->GetTimeSeconds() - StartedAt;
		}
		const FVector Aim = Owner->GetCombatAimPoint(Owner->GetTrackedTarget());
		// 일반/시도 경로를 합쳐 총구당 관측 조회 상한을 공유한다.
		int32 PathChecks = 16;
		const bool bNormalPath = Shot.Weapon->HasClearTargetAttackPathFrom(Owner->GetTrackedTarget(), Aim, Shot.Start, PathChecks);
		const bool bAttempt = Owner->IsTargetAttemptActive();
		const AEnemyAIController* AI = Cast<AEnemyAIController>(Owner->GetController());
		const bool bDirect = AI && AI->IsTargetDirectlyVisible(Owner->GetTrackedTarget());
		FOutlierSightObservation Observation;
		const AShooterCharacter* ObservedShooter = Cast<AShooterCharacter>(Owner->GetTrackedTarget());
		bool bOwnObservation = !ObservedShooter;
		uint32 SelectedEpoch = 0;
		int32 SelectedBone = INDEX_NONE;
		FVector SelectedPoint = FVector::ZeroVector;
		if (ObservedShooter && ObservedShooter->GetObservedAimPoints(Owner, Observation)
			&& Owner->GetCombatAimSelectionForTesting(ObservedShooter, SelectedEpoch, SelectedBone, SelectedPoint))
		{
			// 같은 본의 관측 좌표도 0.1초 조준 캐시 사이에 갱신된다. 최신 좌표와 직접 비교하지 않고
			// 선택 대상/관측 세대/본과 실제 캐시 조준점의 연결을 검사해 정상 이동을 오분류하지 않는다.
			bOwnObservation = Observation.Epoch == SelectedEpoch && SelectedPoint.Equals(Aim, 0.01)
				&& (Observation.BodyBoneIndices.Contains(SelectedBone) || Observation.HeadBoneIndices.Contains(SelectedBone)
					|| Observation.OtherBoneIndices.Contains(SelectedBone));
		}
		// 예외가 켜졌다는 사실만 신뢰하지 않는다. 자기 관측과 실제 원점의 환경/동료 경로를 독립 확인한다.
		const bool bAllowedAttempt = bAttempt && bDirect && bOwnObservation
			&& !Shot.Weapon->IsOutsideEffectiveRange(Aim)
			&& Owner->GetTargetAttackPreparationRemaining() <= KINDA_SMALL_NUMBER
			&& Owner->GetTargetAttackRecoveryRemaining() <= KINDA_SMALL_NUMBER
			&& Shot.Weapon->HasTargetAttemptPathFrom(Owner->GetTrackedTarget(), Aim, Shot.Start, PathChecks);
		InvalidShotPaths += !bNormalPath && !bAllowedAttempt;
		ApprovedAttemptShots += bAttempt && bAllowedAttempt;
		InvalidAttemptShots += bAttempt && !bAllowedAttempt;
		NormalShots += !bAttempt && bNormalPath;
		ShotsWhileHidden += !bDirect || !bOwnObservation;
		ShotsWhileInactive += Owner->IsPoolManaged() && Owner->GetEnemyPoolState() != EEnemyPoolState::CombatActive;
		if (bAttempt && IsAttemptScenario() && Cover.IsValid())
		{
			if (FirstAttemptShotAt < 0.0)
			{
				FirstAttemptShotAt = ActiveWorld->GetTimeSeconds();
			}
			// 같은 통제 벽은 제거 전까지 변하지 않는다. 보통 사격을 거쳐도 사용 기회를 재충전하지 않는다.
			InvalidAttemptShots += ActiveWorld->GetTimeSeconds() - FirstAttemptShotAt > 0.5 + KINDA_SMALL_NUMBER;
		}
		if (Scenario == TEXT("Bypass") && Cover.IsValid() && !bAttempt && bNormalPath)
		{
			++BypassShots;
			TArray<FVector> Route;
			FVector Destination;
			// 속도만 남은 도착 직후를 제외하고, 실제 이어갈 경로와 목적지 잔여 거리를 확인한다.
			MovingShots += Owner->GetVelocity().SizeSquared() > FMath::Square(1.0f)
				&& Owner->GetCombatNavigationHandoff(Route, Destination)
				&& FVector::DistSquared(Owner->GetActorLocation(), Destination) > FMath::Square(60.0f);
		}
		PoolReacquireShots += Scenario == TEXT("Pool") && bPoolReused && Owner == PoolOriginal.Get()
			&& Owner->GetPoolLeaseSerial() != PreviousPoolLease;
		CloseShots += FVector::Dist(Shot.Start, Aim) <= 300.0f;
		HeightShots += FMath::Abs(Aim.Z - Shot.Start.Z) >= 100.0f;
		const double Error = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct((Aim - Shot.Start).GetSafeNormal(), Shot.BaseDirection), -1.0, 1.0)));
		InvalidShots += Error > Owner->GetRuntimeStat().AttackAimTolerance + 0.1f;
		FFileHelper::SaveStringToFile(FString::Printf(TEXT("%.3f,%u,%s,%s,%.3f,%s,%s,%s,%d,%d,%s,%d,%d,%d,%d,%d,%llu,%d,%d\n"), ActiveWorld->GetTimeSeconds(), Shot.ShotId,
			*Owner->GetName(), *Shot.Socket.ToString(), Error, *Shot.Start.ToCompactString(), *Shot.BaseDirection.ToCompactString(), *Aim.ToCompactString(),
			Shot.bOccluded, Shot.bHit, *Shot.HitActor, bAttempt, bAllowedAttempt, bNormalPath, bDirect, bOwnObservation,
			Owner->GetCombatTacticalEpisodeSerial(), Owner->GetPoolGameplayGeneration(), Owner->GetPoolLeaseSerial()),
			*(Output / TEXT("Shots.csv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
	}

	void Footstep(USkeletalMeshComponent* Mesh, const FVector& Location)
	{
		if (bFinished || !Mesh || Mesh->GetWorld() != ActiveWorld.Get() || Mesh->GetOwner() != Target.Get()
			|| Role != TEXT("Server") || StartedAt < 0.0)
		{
			return;
		}
		++Footsteps;
		if (Scenario == TEXT("TargetSelection") && Stage == 1 && !Drones.IsEmpty() && Drones[0].IsValid())
		{
			const AEnemyAIController* AI = Cast<AEnemyAIController>(Drones[0]->GetController());
			HiddenShooterFootsteps += AI && !AI->IsTargetDirectlyVisible(Target.Get());
		}
		const bool bSearching = !Drones.IsEmpty() && Drones[0].IsValid() && Drones[0]->GetSearchEpisode().bActive;
		SearchFootsteps += bSearching;
		FFileHelper::SaveStringToFile(FString::Printf(TEXT("%.3f,%s,%d,%s\n"), ActiveWorld->GetTimeSeconds(),
			*GetNameSafe(Mesh->GetOwner()), bSearching, *Location.ToCompactString()), *(Output / TEXT("Footsteps.csv")),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
	}
};

FProbe Probe;
struct FRegister
{
	FRegister()
	{
		if (!FParse::Value(FCommandLine::Get(), TEXT("OutlierCombatProbe="), Probe.Role)
			|| !FParse::Value(FCommandLine::Get(), TEXT("OutlierCombatOutput="), Probe.Output))
		{
			return;
		}
		Probe.Scenario = TEXT("Combat");
		FParse::Value(FCommandLine::Get(), TEXT("OutlierCombatScenario="), Probe.Scenario);
		FParse::Value(FCommandLine::Get(), TEXT("OutlierCombatDuration="), Probe.Duration);
		FParse::Value(FCommandLine::Get(), TEXT("OutlierCombatCaptureLimit="), Probe.CaptureLimit);
		Probe.CaptureLimit = FMath::Clamp(Probe.CaptureLimit, 1, 24);
		FWorldDelegates::OnWorldTickStart.AddRaw(&Probe, &FProbe::Before);
		FWorldDelegates::OnWorldPostActorTick.AddRaw(&Probe, &FProbe::After);
		WallShotProbe::Observed().AddRaw(&Probe, &FProbe::Shot);
		OutlierFootstepTesting::Reported().AddRaw(&Probe, &FProbe::Footstep);
	}
} Register;
}
#endif
