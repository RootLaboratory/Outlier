#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/EnemyStateTreeTacticalTasks.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "Enemy/EnemyPoolDefinition.h"
#include "Enemy/EnemyPoolSubsystem.h"
#include "Enemy/EnemyRoomReturnTestTags.h"
#include "Enemy/EnemyCombatTestHelpers.h"
#include "Enemy/EnemyEnvQuery.h"
#include "Enemy/EnemyStateTreeCombatMovementTasks.h"
#include "Enemy/EnemyStateTreeMovementTasks.h"
#include "Enemy/EnemyStateTreeSelfDestructTasks.h"
#include "Enemy/EnemyStateTreeAttackTasks.h"
#include "AbilitySystemComponent.h"
#include "Explosion/ExplosionComponent.h"
#include "Weapon/RangedWeaponBase.h"
#include "Weapon/WallShotProbe.h"
#include "Enemy/VECDrone.h"
#include "Enemy/VECDroneMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"
#include "GameplayTags/OutlierGameplayTags.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/StaticMesh.h"
#include "Interaction/InteractableDoor.h"
#include "Components/CapsuleComponent.h"
#include "Components/StateTreeComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISense_Sight.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomTagComponent.h"
#include "Room/RoomVolume.h"
#include "Shooter/ShooterCharacter.h"
#include "StateTree.h"
#include "UObject/UnrealType.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"

namespace GunAimNetworkProbe
{
// 명시적 실행 인자가 있을 때만 동작한다. 클라이언트는 복제된 상태를 관측하며 조준값을 쓰지 않는다.
struct FProbe
{
	FString Role, Output;
	bool bRead = false, bHeader = false;
	double Started = -1;
	float ConnectedAt = -1, LastSample = -1;
	TArray<TWeakObjectPtr<AVECDrone>> Drones;
	TWeakObjectPtr<AActor> AlternateTarget;
	bool bSwitchedTarget = false;
	bool Enabled(UWorld* World)
	{
		if (!bRead)
		{
			bRead = true;
			FParse::Value(FCommandLine::Get(), TEXT("OutlierGunAimProbe="), Role);
			FParse::Value(FCommandLine::Get(), TEXT("OutlierGunAimOutput="), Output);
		}
		return !Output.IsEmpty() && World->IsGameWorld() && World->HasBegunPlay()
			&& (Role == TEXT("Server") ? World->GetNetMode() == NM_DedicatedServer : World->GetNetMode() == NM_Client);
	}
	void Before(UWorld* World, ELevelTick, float DeltaTime)
	{
		if (!Enabled(World)) return;
		if (Started < 0) Started = FPlatformTime::Seconds();
		if (FPlatformTime::Seconds() - Started > 65) { FPlatformMisc::RequestExit(false); return; }
		if (Role == TEXT("Server"))
		{
			if (!World->GetNetDriver() || World->GetNetDriver()->ClientConnections.Num() < 2) return;
			if (ConnectedAt < 0) ConnectedAt = World->GetTimeSeconds();
			if (World->GetTimeSeconds() - ConnectedAt < 3) return;
			if (Drones.IsEmpty())
			{
				UClass* Class = LoadClass<AVECDrone>(nullptr, TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"));
				for (int32 Index = 0; Index < 4; ++Index)
				{
					FActorSpawnParameters Params;
					Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					AVECDrone* Drone = World->SpawnActor<AVECDrone>(Class, FVector(0, Index * 1000, 134), FRotator::ZeroRotator, Params);
					if (!Drone) { UE_LOG(LogTemp, Error, TEXT("[GunAimNetwork] Spawn failed")); return; }
					Drone->GetStateTreeComponent()->StopLogic(TEXT("Gun aim network probe"));
					if (auto* Controller = Cast<AEnemyAIController>(Drone->GetController())) Controller->SetEnemyPerceptionEnabled(false);
					Drone->GetCharacterMovement()->DisableMovement();
					Drone->SetActorEnableCollision(false);
					const float Distances[] = {466.50f, 235.64f, 103.45f, 103.45f};
					const FVector Target = Drone->GetCombatShotOrigin() + FVector(Distances[Index], 0, -35.81f);
					Drone->EnterCombatFromRoom(Target, false);
					Drone->GetStateTreeComponent()->StopLogic(TEXT("Gun aim network probe"));
					UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr, TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
					ACharacter* Observed = Index == 3
						? static_cast<ACharacter*>(World->SpawnActor<AVECDrone>(Class, Target, FRotator::ZeroRotator, Params))
						: World->SpawnActor<AShooterCharacter>(ShooterClass, Target, FRotator::ZeroRotator, Params);
					if (AVECDrone* OtherDrone = Cast<AVECDrone>(Observed))
					{
						OtherDrone->GetStateTreeComponent()->StopLogic(TEXT("Observed drone target"));
						// 관측용 드론만 플레이어 팀으로 둬 실제 유효 적대 대상 분류를 거치게 한다.
						FindFProperty<FBoolProperty>(OtherDrone->GetClass(), TEXT("bIsPossessed"))->SetPropertyValue_InContainer(OtherDrone, true);
						AlternateTarget = OtherDrone;
					}
					Observed->SetActorEnableCollision(false);
					Observed->SetCanBeDamaged(false);
					Observed->GetCharacterMovement()->DisableMovement();
					Observed->SetActorTickEnabled(false);
					auto* AI = CastChecked<AEnemyAIController>(Drone->GetController());
					AI->SetCachedVisibleTargetForTesting(Observed);
					Drone->SetPlayerCurrentlyVisible(true);
					Drone->ObserveTrackedTarget(Observed);
					Drones.Add(Drone);
				}
			}
			const float Elapsed = World->GetTimeSeconds() - ConnectedAt - 3;
			if (Elapsed >= 12 && !bSwitchedTarget && AlternateTarget.IsValid())
			{
				bSwitchedTarget = true;
				AVECDrone* Drone = Drones[2].Get();
				CastChecked<AEnemyAIController>(Drone->GetController())->SetCachedVisibleTargetForTesting(AlternateTarget.Get());
				Drone->ObserveTrackedTarget(AlternateTarget.Get());
			}
			const int32 Step = int32(Elapsed / 4) % 3;
			for (auto& Weak : Drones) if (AVECDrone* Drone = Weak.Get())
			{
				// 외형 Phase만 순환한다. 조준권과 제한 회전은 실제 서버 Controller 경로가 결정한다.
				Drone->BeginTargetAttackPreparation(0.25f);
				Drone->AdvanceTargetAttackPreparation(DeltaTime);
				Drone->SetCombatAimPresentationOwned(true);
				Drone->SetAttackPhase(Step == 0 ? EEnemyAttackPhase::Telegraph : Step == 1 ? EEnemyAttackPhase::Firing : EEnemyAttackPhase::Recover);
			}
			if (Elapsed > 25) FPlatformMisc::RequestExit(false);
		}
		else
		{
			// NullRHI에서는 렌더 시각만 보장한다. 실제 회전·애니메이션 평가·네트워크 복제는 그대로 실행한다.
			for (TActorIterator<AVECDrone> It(World); It; ++It) It->GetMesh()->SetLastRenderTime(World->GetTimeSeconds());
		}
	}
	void After(UWorld* World, ELevelTick, float)
	{
		if (!Enabled(World) || World->GetTimeSeconds() - LastSample < .1f) return;
		LastSample = World->GetTimeSeconds();
		FString Rows;
		if (!bHeader) { Rows = TEXT("time,netMode,case,phase,expected,baseAim,facing,tilt,actor,body,shooter,aimOwned\n"); bHeader = true; }
		for (TActorIterator<AVECDrone> It(World); It; ++It)
		{
			AVECDrone* Drone = *It;
			if (Drone->IsEnemyPossessed()) continue;
			USkeletalMeshComponent* Mesh = Drone->GetMesh();
			const FReferenceSkeleton& Skeleton = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
			const int32 Bone = Skeleton.FindBoneIndex(TEXT("BodyBone"));
			float BodyPitch = 999;
			if (Bone != INDEX_NONE)
			{
				FTransform Ref = Skeleton.GetRefBonePose()[Bone];
				for (int32 Parent = Skeleton.GetParentIndex(Bone); Parent != INDEX_NONE; Parent = Skeleton.GetParentIndex(Parent)) Ref = Ref * Skeleton.GetRefBonePose()[Parent];
				const auto* DefaultMesh = Drone->GetClass()->GetDefaultObject<AVECDrone>()->GetMesh();
				const FVector Forward = Ref.GetRotation().UnrotateVector(DefaultMesh->GetRelativeRotation().UnrotateVector(FVector::ForwardVector));
				BodyPitch = Mesh->GetBoneTransform(Bone).GetRotation().RotateVector(Forward).Rotation().Pitch;
			}
			Rows += FString::Printf(TEXT("%.3f,%d,%d,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d\n"), World->GetTimeSeconds(), int32(World->GetNetMode()), FMath::RoundToInt(Drone->GetActorLocation().Y / 1000), int32(Drone->GetAttackPhase()),
				(Drone->GetLastKnownPlayerLocation() - Drone->GetCombatShotOrigin()).Rotation().Pitch, Drone->GetBaseAimRotation().Pitch,
				Drone->GetAIFacingPitchRoot()->GetRelativeRotation().Pitch, Drone->GetThirdPersonTiltRoot()->GetRelativeRotation().Pitch, Drone->GetActorRotation().Pitch, BodyPitch, Drone->IsAimingAtShooter(), Drone->IsCombatAimPresentationOwned());
		}
		FFileHelper::SaveStringToFile(Rows, *Output, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
	}
};
FProbe Probe;
struct FRegister
{
	FRegister()
	{
		FWorldDelegates::OnWorldTickStart.AddRaw(&Probe, &FProbe::Before);
		FWorldDelegates::OnWorldPostActorTick.AddRaw(&Probe, &FProbe::After);
	}
} Register;
}

namespace
{
struct FEnemySearchIntegrationWorld
{
	FGameplayTag SearchRoomTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")));
	UWorld* World = nullptr;
	UGameInstance* GameInstance = nullptr;
	ARoomVolume* Room = nullptr;
	URoomCombatDefinition* CombatDefinition = nullptr;

	FEnemySearchIntegrationWorld(bool bStartPlay = false)
	{
		if (!GEngine) return;

		const FName WorldName = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("EnemySearchIntegration"));
		World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
		if (!World) return;

		FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
		Context.RunAsDedicated = IsRunningDedicatedServer();
		World->AddToRoot();
		Context.SetCurrentWorld(World);

		GameInstance = NewObject<UGameInstance>(GEngine);
		World->SetGameInstance(GameInstance);
		GameInstance->Init();
		// 임시 Game 월드는 AI System을 기본 생성하지 않는다. 실제 EQS Manager를 준비한 뒤 액터를 초기화한다.
		World->CreateAISystem();
		if (bStartPlay)
		{
			// 프로젝트 GameMode의 플레이어/스테이지 생성 없이 실제 월드 Tick만 실행한다.
			FURL URL;
			URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
			World->SetGameMode(URL);
		}
		World->InitializeActorsForPlay(FURL());
		if (bStartPlay && IsRunningDedicatedServer())
		{
			FURL ListenURL;
			ListenURL.Port = 0;
			World->Listen(ListenURL);
		}
		if (bStartPlay) World->BeginPlay();

		// BeginPlay에서 로드하는 프로젝트 방 정의 대신, 테스트에 필요한 방만 주입한다.
		CombatDefinition = NewObject<URoomCombatDefinition>(GetTransientPackage());
		FRoomCombatRoomDefinition& RoomDefinition = CombatDefinition->RoomDefinitions.AddDefaulted_GetRef();
		RoomDefinition.RoomTag = SearchRoomTag;
		RoomDefinition.CombatPhases.AddDefaulted();

		if (URoomCombatSubsystem* Combat = World->GetSubsystem<URoomCombatSubsystem>())
		{
			Combat->SetCombatDefinitionForTesting(CombatDefinition);
		}
	}

	~FEnemySearchIntegrationWorld()
	{
		if (!World) return;

		GEngine->ShutdownWorldNetDriver(World);
		// 실제 Tick 테스트는 액터와 서브시스템의 종료 처리를 마친 뒤 월드를 제거한다.
		if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(true);
		if (GameInstance) GameInstance->Shutdown();
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}

	bool CreateRoom(const FVector& Extent = FVector(1600.0, 1600.0, 700.0))
	{
		if (!World) return false;
		Room = World->SpawnActor<ARoomVolume>(ARoomVolume::StaticClass(), FTransform::Identity);
		if (!Room) return false;

		if (UBoxComponent* Box = Cast<UBoxComponent>(Room->GetRootComponent()))
		{
			Box->SetBoxExtent(Extent);
			Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Box->SetCollisionObjectType(ECC_WorldStatic);
			Box->SetCollisionResponseToAllChannels(ECR_Overlap);
		}

		URoomCombatSubsystem* Combat = World->GetSubsystem<URoomCombatSubsystem>();
		return Combat && Combat->RegisterRoom(Room, SearchRoomTag);
	}

	AEnemyBase* SpawnEnemy(const FVector& Location = FVector::ZeroVector) const
	{
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AEnemyBase* Enemy = World ? World->SpawnActor<AEnemyBase>(
			AEnemyBase::StaticClass(), FTransform(Location), Parameters) : nullptr;
		if (!Enemy) return nullptr;

		Enemy->GetRoomTagComp()->AssignDefaultRoomTag(SearchRoomTag);
		Enemy->EnterCombatFromRoom(FVector::ZeroVector, false);
		Enemy->UpdateLastKnownPlayerLocation(FVector::ZeroVector);
		return Enemy;
	}

	AActor* SpawnBlockingBox(const FVector& Location, const FVector& Extent) const
	{
		AActor* Actor = World ? World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location)) : nullptr;
		if (!Actor) return nullptr;

		UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
		Box->SetBoxExtent(Extent);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Actor->SetRootComponent(Box);
		Box->RegisterComponent();
		Box->SetWorldLocation(Location);
		World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
		World->GetPhysicsScene()->Flush();
		return Actor;
	}

	void Tick(float DeltaTime) const
	{
		// TimerManager는 같은 엔진 프레임에서 중복 Tick하지 않는다. 테스트가 프레임 경계도 진행한다.
		++GFrameCounter;
		World->Tick(LEVELTICK_All, DeltaTime);
	}
};

FEnvQueryInstance MakePointResult(UObject* Owner, const TArray<FVector>& Points)
{
	FEnvQueryInstance Result;
	Result.Owner = Owner;
	Result.ItemType = UEnvQueryItemType_Point::StaticClass();
	Result.ItemTypeVectorCDO = GetMutableDefault<UEnvQueryItemType_Point>();
	Result.ValueSize = GetDefault<UEnvQueryItemType_Point>()->GetValueSize();
	Result.MarkAsFinishedWithoutIssues();
	for (const FVector& Point : Points)
	{
		Result.AddItemData<UEnvQueryItemType_Point>(Point);
	}
	return Result;
}

FEnemyRunEnvQueryTaskInstanceData MakeSearchData(
	AEnemyBase* Enemy, ARoomVolume* Room, EEnemySearchQueryMode Mode)
{
	FEnemyRunEnvQueryTaskInstanceData Data;
	Data.Enemy = Enemy;
	Data.SearchMode = Mode;
	Data.bRequireVisibleTarget = false;
	Data.RunMode = EEnvQueryRunMode::AllMatching;
	Data.CachedRooms = Enemy ? Enemy->GetWorld()->GetSubsystem<URoomCombatSubsystem>() : nullptr;
	Data.CapturedRoomTag = Enemy->GetRoomTagComp()->GetDefaultRoomTag();
	Data.CapturedRoom = Room;
	if (Data.CachedRooms) Data.CachedRooms->BuildEnemyMovementRoomContext(Enemy, Data.MovementRooms);
	Data.CapturedCombatState = EEnemyCombatState::Combat;
	Data.CapturedGeneration = Enemy ? Enemy->GetPoolGameplayGeneration() : 0;
	Data.CapturedLease = Enemy ? Enemy->GetPoolLeaseSerial() : 0;
	Data.QueryTimeout = 60.0f;
	Data.QueryStartedAt = Enemy->GetWorld()->GetTimeSeconds();
	Data.PathCellSize = 100.0f;
	Data.SearchPadding = 900.0f;
	Data.MaxPathCandidates = 16;
	Data.MaxSearchNodes = 65536;
	Data.MaxCollisionChecks = 262144;
	Data.MaxSearchMilliseconds = 10000.0f;
	Data.SearchPointSeparation = 200.0f;
	if (Enemy)
	{
		const FEnemySearchEpisode& Episode = Enemy->GetSearchEpisode();
		Data.CapturedSearchSerial = Episode.Serial;
		Data.CapturedSharedRevision = Episode.SharedRevision;
		Data.CapturedOrbitCenter = Episode.Center;
		Data.CapturedSearchHeight = Enemy->GetActorLocation().Z;
	}
	return Data;
}

bool BeginSearch(FAutomationTestBase& Test, AEnemyBase* Enemy, const FVector& Center)
{
	if (!Test.TestNotNull(TEXT("Enemy exists"), Enemy)) return false;
	Enemy->UpdateLastKnownPlayerLocation(Center);
	Test.TestTrue(TEXT("Search episode begins"), Enemy->BeginSearchEpisode());
	return Enemy->GetSearchEpisode().bActive;
}

bool RediscoverSearchTarget(FAutomationTestBase& Test, AEnemyBase& Enemy, AEnemyAIController& Controller)
{
	UClass* TargetClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	if (!Test.TestNotNull(TEXT("Rediscovery has a concrete player class"), TargetClass)) return false;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AShooterCharacter* Target = Enemy.GetWorld()->SpawnActor<AShooterCharacter>(TargetClass,
		FTransform(Enemy.GetActorLocation() + FVector(1000, 0, 0)), Spawn);
	if (!Test.TestNotNull(TEXT("Rediscovered player exists"), Target)) return false;
	Target->SetActorEnableCollision(false);
	Target->GetCharacterMovement()->SetComponentTickEnabled(false);
	// 시야 플래그만 켜면 실제 대상이 없는 관측으로 판단되어 즉시 해제된다.
	// 감지 결과만 주입하고, 추적 확정과 이전 수색 중단은 실제 갱신 경로가 수행하게 한다.
	Controller.SetCachedVisibleTargetForTesting(Target);
	Enemy.SetPlayerCurrentlyVisible(true);
	return Test.TestTrue(TEXT("Rediscovery retains valid direct sight"), Enemy.IsPlayerCurrentlyVisible()
		&& Controller.IsTargetDirectlyVisible(Target))
		&& Test.TestEqual(TEXT("Rediscovery tracks the observed player"), Enemy.GetTrackedTarget(), static_cast<AActor*>(Target));
}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyNoiseFlightIntegrationTest,
	"Outlier.Enemy.Perception.Investigation.Flight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyNoiseFlightIntegrationTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : { TEXT("Arrival"), TEXT("Detour"), TEXT("NoPath"),
		TEXT("BudgetExceeded"), TEXT("DirectReacquire"), TEXT("TimedSearch"), TEXT("RetargetDuringSearch"),
		TEXT("RetargetBeforeCompletion"), TEXT("GeneralSearchRetarget") })
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyNoiseFlightIntegrationTest::RunTest(const FString& Parameters)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	FEnemySearchIntegrationWorld Fixture(true);
	if (!TestTrue(TEXT("Flight test world begins play"), Fixture.World && Fixture.World->HasBegunPlay())
		|| !TestTrue(TEXT("Flight room registers"), Fixture.CreateRoom(FVector(1200, 800, 500)))) return false;
	FVector Start(-600, 0, 100);
	const FVector HeardLocation(600, 0, 100);
	UClass* EnemyClass = LoadClass<AEnemyBase>(nullptr,
		TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"));
	UClass* PlayerClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	if (!TestNotNull(TEXT("Flight enemy BP loads"), EnemyClass)
		|| !TestNotNull(TEXT("Noise source BP loads"), PlayerClass)) return false;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// BeginPlay 월드에서는 실제 스탯 행이 있는 BP를 써야 첫 Tick에도 생존한다.
	AEnemyBase* Enemy = Fixture.World->SpawnActor<AEnemyBase>(EnemyClass, FTransform(Start), Spawn);
	if (!TestNotNull(TEXT("Flight enemy exists"), Enemy)) return false;
	Enemy->GetRoomTagComp()->AssignDefaultRoomTag(Fixture.SearchRoomTag);
	Enemy->EnterCombatFromRoom(Start, false);
	AEnemyAIController* Controller = Cast<AEnemyAIController>(Enemy->GetController());
	if (!TestNotNull(TEXT("Flight enemy controller exists"), Controller)) return false;
	AShooterCharacter* Source = Fixture.World->SpawnActor<AShooterCharacter>(PlayerClass,
		FTransform(FVector(8000, 8000, 100)), Spawn);
	if (!TestNotNull(TEXT("Noise source BP spawns"), Source)) return false;
	if (UStateTreeComponent* Tree = Enemy->GetStateTreeComponent())
		Tree->StopLogic(TEXT("Noise flight integration owns movement"));
	// 조사 입력만 주입하고 실제 이동은 World/CharacterMovement가 수행한다.
	// 자동 Sight 입력은 차단해 소리 조사 중 우연히 직접 시야를 얻는 일을 방지한다.
	Controller->SetEnemyPerceptionEnabled(false);
	UCharacterMovementComponent* Movement = Enemy->GetCharacterMovement();
	if (!TestNotNull(TEXT("Character movement exists"), Movement)) return false;
	const_cast<FEnemyStat&>(Enemy->GetRuntimeStat()).MoveSpeed = 600.0f;
	Movement->SetMovementMode(MOVE_Flying);
	Movement->MaxFlySpeed = 600.0f;
	Movement->MaxAcceleration = 2048.0f;
	Movement->BrakingDecelerationFlying = 2048.0f;
	Source->GetCharacterMovement()->SetComponentTickEnabled(false);
	const bool bSealed = Parameters == TEXT("NoPath");
	const bool bWall = bSealed || Parameters == TEXT("Detour") || Parameters == TEXT("BudgetExceeded");
	if (bWall && !TestNotNull(TEXT("Flight obstacle exists"), Fixture.SpawnBlockingBox(
		FVector::ZeroVector, FVector(40, bSealed ? 800 : 180, 500)))) return false;
	Fixture.Tick(1.0f / 32.0f);
	Start = Enemy->GetActorLocation();
	if (Parameters == TEXT("GeneralSearchRetarget"))
	{
		FEnemySearchSessionTaskInstanceData Search;
		Search.Enemy = Enemy;
		if (!TestEqual(TEXT("General lost-contact search enters"),
			EnemySearchTesting::EnterSession(Search), EStateTreeRunStatus::Running)) return false;
		const uint64 OldSearchSerial = Enemy->GetSearchEpisode().Serial;
		Fixture.Tick(1.0f / 32.0f);
		if (!TestTrue(TEXT("A new sound is admitted during general search"), Enemy->RecordHeardNoise(
			Source, HeardLocation, Fixture.World->GetTimeSeconds()))) return false;
		EnemySearchTesting::RefreshSessionTransitions(Search);
		TestTrue(TEXT("General search exposes hearing handoff without finishing its children"), Search.bShouldInvestigateNoise);
		TestFalse(TEXT("Hearing handoff never requests shared-hint restart"), Search.bShouldRestart);
		FVector Destination;
		if (!TestTrue(TEXT("Handoff starts the sound investigation"), Enemy->BeginNoiseInvestigation(Destination))) return false;
		const uint64 NoiseSerial = Enemy->GetNoiseInvestigationSerial();
		Enemy->SetActorLocation(Destination);
		if (!TestTrue(TEXT("Replacement investigation can reach its search"),
			Enemy->BeginNoiseInvestigationSearch(NoiseSerial, Enemy->GetActorLocation()))) return false;
		const uint64 NewSearchSerial = Enemy->GetSearchEpisode().Serial;
		TestTrue(TEXT("Replacement search owns a different episode"), NewSearchSerial != OldSearchSerial);
		EnemySearchTesting::RefreshSessionTransitions(Search);
		TestEqual(TEXT("Late old Tick cannot capture replacement ownership"), Search.CapturedSearchSerial, OldSearchSerial);
		EnemySearchTesting::ExitSession(Search);
		TestTrue(TEXT("Late general-search Exit preserves replacement search"), Enemy->GetSearchEpisode().bActive);
		TestEqual(TEXT("Late Exit cannot change replacement ownership"), Enemy->GetSearchEpisode().Serial, NewSearchSerial);
		return true;
	}
	if (!TestTrue(TEXT("Flight hears a fixed player-side occurrence"), Enemy->RecordHeardNoise(
		Source, HeardLocation, Fixture.World->GetTimeSeconds()))) return false;

	FEnemyFlyToLocationTaskInstanceData Data;
	Data.Enemy = Enemy;
	Data.bInvestigateNoise = true;
	Data.bUsePathfinding = true;
	Data.CachedController = Controller;
	Data.CachedCapsule = Enemy->GetCapsuleComponent();
	Data.CachedRoomSubsystem = Fixture.World->GetSubsystem<URoomCombatSubsystem>();
	Data.CachedRoom = Fixture.Room;
	Data.PathRoomTag = Fixture.SearchRoomTag;
	Data.bOwnsMovement = true;
	Data.MoveResult = EEnemyFlightMoveResult::Moving;
	Data.AcceptanceRadius = 25.0f;
	Data.GoalSearchRadius = 0.0f;
	Data.MaxSearchNodes = Parameters == TEXT("BudgetExceeded") ? 1 : 65536;
	Data.MaxCollisionChecks = 262144;
	Data.MaxSearchMilliseconds = 10000.0f;
	Data.MaxRepathAttempts = 2;
	Data.RepathInterval = 0.125f;
	Data.RepathTimeout = 1.0f;
	ON_SCOPE_EXIT { EnemyFlightTesting::Stop(Data); };
	EStateTreeRunStatus Status = EnemyFlightTesting::StartPath(Data);
	if (!TestEqual(TEXT("Flight starts through the production path helper"), Status, EStateTreeRunStatus::Running)) return false;
	const uint64 Serial = Data.CapturedNoiseSerial;
	if (Parameters == TEXT("Detour"))
	{
		if (!TestEqual(TEXT("A bounded grid route exists around the wall"), Data.PathStatus, EFlightPathStatus::Success)) return false;
		TestTrue(TEXT("Wall route contains intermediate waypoints"), Data.PathWaypoints.Num() > 2);
	}
	if (Parameters == TEXT("BudgetExceeded") || bSealed)
	{
		const EFlightPathStatus Expected = bSealed ? EFlightPathStatus::NoPathWithinBounds : EFlightPathStatus::BudgetExceeded;
		if (!TestEqual(TEXT("Initial path result preserves physical versus budget failure"), Data.PathStatus, Expected)) return false;
		TestFalse(TEXT("Failed query cannot expose an unverified movement path"), Data.PathWaypoints.IsValidIndex(Data.WaypointIndex));
		TestTrue(TEXT("Failure enters a bounded retry episode"), Data.bWaitingForPath);
	}
	if (Parameters == TEXT("DirectReacquire"))
	{
		Controller->SetCachedVisibleTargetForTesting(Source);
		Enemy->SetPlayerCurrentlyVisible(true);
		Status = EnemyFlightTesting::TickPath(Data, 1.0f / 32.0f);
		TestEqual(TEXT("Sight reacquisition interrupts the old flight"), Status, EStateTreeRunStatus::Failed);
		TestEqual(TEXT("Interruption is not reported as arrival"), Data.MoveResult, EEnemyFlightMoveResult::Interrupted);
		TestFalse(TEXT("Interrupted movement cannot publish a noise search"), Data.bNoiseSearchReady);
		TestFalse(TEXT("Sight reacquisition releases investigation ownership"), Enemy->IsNoiseInvestigationActive());
		TestFalse(TEXT("Old flight cannot complete after reacquisition"), Enemy->BeginNoiseInvestigationSearch(Serial, Enemy->GetActorLocation()));
		return true;
	}

	float MaxSideDistance = 0.0f;
	constexpr float Step = 1.0f / 32.0f;
	for (int32 Frame = 0; Frame < 640 && Status == EStateTreeRunStatus::Running; ++Frame)
	{
		Status = EnemyFlightTesting::TickPath(Data, Step);
		if (Status == EStateTreeRunStatus::Running) Fixture.Tick(Step);
		MaxSideDistance = FMath::Max(MaxSideDistance, static_cast<float>(FMath::Abs(Enemy->GetActorLocation().Y)));
	}
	TestTrue(TEXT("Noise movement completes within the finite simulation bound"), Status != EStateTreeRunStatus::Running);
	TestTrue(TEXT("Completed movement hands off a local noise search"), Data.bNoiseSearchReady);
	TestTrue(TEXT("Noise search phase becomes active"), Enemy->IsNoiseSearchActive());
	TestTrue(TEXT("Search center is the physically reached actor position"),
		Enemy->GetSearchEpisode().Center.Equals(Enemy->GetActorLocation(), 1.0));
	if (!bSealed && Parameters != TEXT("BudgetExceeded"))
	{
		TestEqual(TEXT("Valid path ends in arrival rather than fallback"), Data.MoveResult, EEnemyFlightMoveResult::Arrived);
		TestTrue(TEXT("Actual movement reaches the sound acceptance radius"), Enemy->GetActorLocation().Equals(HeardLocation, Data.AcceptanceRadius + 1.0f));
		TestTrue(TEXT("Movement is simulated rather than completed at the start"), FVector::Distance(Start, Enemy->GetActorLocation()) > 1000.0);
		if (Parameters == TEXT("Detour")) TestTrue(TEXT("Actual flight travels around the wall"), MaxSideDistance > 180.0f);
	}
	else
	{
		TestEqual(TEXT("Exhausted retries report blocked movement"), Data.MoveResult, EEnemyFlightMoveResult::Blocked);
		TestTrue(TEXT("Failed movement never crosses to the unreachable sound"), Enemy->GetActorLocation().Equals(Start, 1.0));
		TestTrue(TEXT("Retries obey the configured attempt bound"), Data.RepathAttempts <= Data.MaxRepathAttempts);
		TestTrue(TEXT("Fallback search does not adopt the unreached sound position"), !Enemy->GetSearchEpisode().Center.Equals(HeardLocation, 1.0));
	}
	if (Parameters == TEXT("TimedSearch") || Parameters == TEXT("RetargetDuringSearch")
		|| Parameters == TEXT("RetargetBeforeCompletion"))
	{
		FEnemySearchSessionTaskInstanceData Search;
		Search.Enemy = Enemy;
		Search.bNoiseInvestigationSearch = true;
		Search.NoiseSearchDuration = Parameters == TEXT("TimedSearch") ? 0.5f : 5.0f;
		if (!TestEqual(TEXT("Reached noise search enters the production session"),
			EnemySearchTesting::EnterSession(Search), EStateTreeRunStatus::Running)) return false;
		// 이동 완료 시각부터 별도 수색 시간을 계산한다. 소리 발생 시각은 수색 타이머를 소모하지 않는다.
		for (int32 Frame = 0; Frame < 15; ++Frame) Fixture.Tick(Step);
		EnemySearchTesting::TickSession(Search);
		TestFalse(TEXT("Noise search remains active before its duration"), Search.bNoiseSearchComplete);
		Fixture.Tick(Step);
		if (Parameters == TEXT("TimedSearch"))
		{
			EnemySearchTesting::TickSession(Search);
			TestTrue(TEXT("Noise search completes exactly at the configured duration"), Search.bNoiseSearchComplete);
			EnemySearchTesting::ExitSession(Search);
			TestFalse(TEXT("Completed search releases investigation ownership"), Enemy->IsNoiseInvestigationActive());
			TestFalse(TEXT("Completed search releases the search episode"), Enemy->GetSearchEpisode().bActive);
			FVector Destination;
			TestFalse(TEXT("Completion consumes the sound instead of restarting it"), Enemy->BeginNoiseInvestigation(Destination));
		}
		else
		{
			if (Parameters == TEXT("RetargetDuringSearch"))
			{
				Search.NoiseSearchDuration = 0.5f;
				EnemySearchTesting::TickSession(Search);
				TestTrue(TEXT("Completion is published before newer evidence arrives"), Search.bNoiseSearchComplete);
			}
			else TestFalse(TEXT("New sound arrives before the five-second search completes"), Search.bNoiseSearchComplete);
			for (int32 Frame = 0; Frame < 16; ++Frame) Fixture.Tick(Step);
			// 위치 변경과 새 발생 시각으로 수색에서 새 이동으로 인계한다. 수색 단계에는 거리 이득을 요구하지 않는다.
			const FVector SearchCenter = Enemy->GetSearchEpisode().Center;
			Enemy->SetActorLocation(SearchCenter + FVector(0, 400, 0));
			const FVector NewSound = SearchCenter + FVector(0, 200, 0);
			TestTrue(TEXT("A fresh separated sound is heard during search"),
				Enemy->RecordHeardNoise(Source, NewSound, Fixture.World->GetTimeSeconds()));
			EnemySearchTesting::RefreshSessionTransitions(Search);
			TestTrue(TEXT("Search exposes a retarget transition for the new sound"), Search.bShouldInvestigateNoise);
			TestFalse(TEXT("Retarget is not a completed search"), Search.bNoiseSearchComplete);
			FVector Destination;
			if (!TestTrue(TEXT("The replacement investigation starts"), Enemy->BeginNoiseInvestigation(Destination))) return false;
			const uint64 ReplacementSerial = Enemy->GetNoiseInvestigationSerial();
			TestTrue(TEXT("Replacement adopts the new occurrence position"), Destination.Equals(NewSound));
			EnemySearchTesting::ExitSession(Search);
			TestTrue(TEXT("Old session exit cannot release replacement ownership"), Enemy->IsNoiseInvestigationCurrent(ReplacementSerial));
			TestFalse(TEXT("Old movement completion cannot overwrite replacement search"),
				Enemy->BeginNoiseInvestigationSearch(Serial, Enemy->GetActorLocation()));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySearchAnchorCorrectionIntegrationTest,
	"Outlier.Enemy.Search.Integration.AnchorCorrection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemySearchAnchorCorrectionIntegrationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemySearchIntegrationWorld Fixture;
	if (!TestTrue(TEXT("Room registers"), Fixture.CreateRoom())) return false;
	AEnemyBase* Enemy = Fixture.SpawnEnemy(FVector(-300.0, 0.0, 300.0));
	if (!BeginSearch(*this, Enemy, FVector::ZeroVector)) return false;

	// 목격 XY에 가장 가까운 보정 후보가 점수순 앞 후보보다 우선되어야 한다.
	FEnemyRunEnvQueryTaskInstanceData Data = MakeSearchData(Enemy, Fixture.Room, EEnemySearchQueryMode::Anchor);
	const FEnvQueryInstance Result = MakePointResult(Enemy,
		{FVector(350.0, 0.0, 300.0), FVector(0.0, 0.0, 300.0), FVector(100.0, 0.0, 300.0)});

	TestTrue(TEXT("Anchor selection succeeds through real selector"),
		EnemySearchTesting::SelectDestinations(Data, Result));
	TestEqual(TEXT("Anchor selects remembered XY first"), Data.Destination, FVector(0.0, 0.0, 300.0));
	TestTrue(TEXT("Anchor result is exposed as destination"), Data.Destination.Equals(FVector(0.0, 0.0, 300.0)));

	// 마지막 목격 위치를 실제 콜리전으로 막으면 주변 접근 가능한 후보로 보정한다.
	// 100cm 후보도 박스 내부에 넣는다. 60cm 박스에서는 기본 캡슐이 해당 후보에 안전하게 들어갈 수 있다.
	Fixture.SpawnBlockingBox(FVector(0.0, 0.0, 300.0), FVector(120.0));
	Data = MakeSearchData(Enemy, Fixture.Room, EEnemySearchQueryMode::Anchor);
	TestTrue(TEXT("Blocked remembered position selects nearby correction"),
		EnemySearchTesting::SelectDestinations(Data, Result));
	TestEqual(TEXT("Correction selects the closest capsule-safe candidate"), Data.Destination, FVector(350.0, 0.0, 300.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySearchCandidateCountIntegrationTest,
	"Outlier.Enemy.Search.Integration.CandidateCounts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemySearchCandidateCountIntegrationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemySearchIntegrationWorld Fixture;
	if (!TestTrue(TEXT("Room registers"), Fixture.CreateRoom())) return false;

	// 후보 0개: 범위 밖 후보만 있으면 LocalGuard로 갈 수 있는 실패를 남긴다.
	AEnemyBase* ZeroEnemy = Fixture.SpawnEnemy(FVector::ZeroVector);
	if (!BeginSearch(*this, ZeroEnemy, FVector::ZeroVector)) return false;
	FEnemyRunEnvQueryTaskInstanceData ZeroData = MakeSearchData(ZeroEnemy, Fixture.Room, EEnemySearchQueryMode::Points);
	const FEnvQueryInstance ZeroResult = MakePointResult(ZeroEnemy, {FVector(1300.0, 0.0, 0.0)});
	TestFalse(TEXT("Out-of-range candidates are not selected"),
		EnemySearchTesting::SelectDestinations(ZeroData, ZeroResult));
	TestEqual(TEXT("Zero candidates report no reachable destination"),
		ZeroData.QueryResult, EEnemyEnvQueryResult::NoReachableDestination);
	TestTrue(TEXT("Zero candidates leave no selected points"), ZeroData.SelectedSearchPoints.IsEmpty());
	TestTrue(TEXT("Empty point commit enters LocalGuard"), ZeroEnemy->SetSearchPoints({}));
	TestTrue(TEXT("Zero-candidate episode is LocalGuard"), ZeroEnemy->GetSearchEpisode().bLocalGuard);
	ZeroEnemy->SetActorEnableCollision(false);

	// 후보 1개: 두 번째 후보가 너무 가까우면 보충하지 않고 한 지점만 사용한다.
	AEnemyBase* OneEnemy = Fixture.SpawnEnemy(FVector::ZeroVector);
	if (!BeginSearch(*this, OneEnemy, FVector::ZeroVector)) return false;
	FEnemyRunEnvQueryTaskInstanceData OneData = MakeSearchData(OneEnemy, Fixture.Room, EEnemySearchQueryMode::Points);
	const FVector FirstPoint(550.0, 0.0, 0.0);
	const FEnvQueryInstance OneResult = MakePointResult(OneEnemy, {FirstPoint, FVector(620.0, 0.0, 0.0)});
	TestTrue(TEXT("One reachable point is accepted"),
		EnemySearchTesting::SelectDestinations(OneData, OneResult));
	TestEqual(TEXT("Only one separated point is selected"), OneData.SelectedSearchPoints.Num(), 1);
	TestEqual(TEXT("One-point destination is the first point"), OneData.Destination, FirstPoint);
	OneEnemy->SetActorEnableCollision(false);

	// 후보 2개: 두 번째 이동은 첫 지점에서 출발하는 실제 경로로 검증된다.
	AEnemyBase* TwoEnemy = Fixture.SpawnEnemy(FVector::ZeroVector);
	if (!BeginSearch(*this, TwoEnemy, FVector::ZeroVector)) return false;
	FEnemyRunEnvQueryTaskInstanceData TwoData = MakeSearchData(TwoEnemy, Fixture.Room, EEnemySearchQueryMode::Points);
	const FVector SecondPoint(0.0, 650.0, 0.0);
	const FEnvQueryInstance TwoResult = MakePointResult(TwoEnemy, {FirstPoint, SecondPoint});
	TestTrue(TEXT("Two reachable points are accepted"),
		EnemySearchTesting::SelectDestinations(TwoData, TwoResult));
	if (!TestEqual(TEXT("Two points are preselected"), TwoData.SelectedSearchPoints.Num(), 2)) return false;
	TestTrue(TEXT("First selected point is preserved"), TwoData.SelectedSearchPoints[0].Equals(FirstPoint));
	TestTrue(TEXT("Second selected point is preserved"), TwoData.SelectedSearchPoints[1].Equals(SecondPoint));

	TestTrue(TEXT("Selected points can be committed to the episode"),
		TwoEnemy->SetSearchPoints(TwoData.SelectedSearchPoints));
	FEnemySearchProgressTaskInstanceData Progress;
	Progress.Enemy = TwoEnemy;
	Progress.Action = EEnemySearchProgress::SelectCurrentPoint;
	TestEqual(TEXT("Progress exposes first point"), EnemySearchTesting::Progress(Progress), EStateTreeRunStatus::Succeeded);
	TestEqual(TEXT("Current point index starts at zero"), Progress.PointIndex, 0);
	TestTrue(TEXT("Current point is first selected destination"), Progress.Destination.Equals(FirstPoint));
	Progress.Action = EEnemySearchProgress::PointChecked;
	TestEqual(TEXT("First point completion succeeds"), EnemySearchTesting::Progress(Progress), EStateTreeRunStatus::Succeeded);
	TestEqual(TEXT("Progress advances to second point"), Progress.PointIndex, 1);
	TestTrue(TEXT("Second point remains available"), Progress.bHasPoint && Progress.Destination.Equals(SecondPoint));
	Progress.Action = EEnemySearchProgress::PointChecked;
	TestEqual(TEXT("Second point completion enters LocalGuard"), EnemySearchTesting::Progress(Progress), EStateTreeRunStatus::Succeeded);
	TestTrue(TEXT("Episode reaches LocalGuard after two points"), TwoEnemy->GetSearchEpisode().bLocalGuard);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySearchObstacleBoundaryBudgetIntegrationTest,
	"Outlier.Enemy.Search.Integration.ObstacleBoundaryBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemySearchObstacleBoundaryBudgetIntegrationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemySearchIntegrationWorld Fixture;
	if (!TestTrue(TEXT("Room registers"), Fixture.CreateRoom(FVector(900.0, 900.0, 500.0)))) return false;

	// 이동을 시작하기 전에 막힌 후보를 제외하고 다음 후보의 경로를 검증한다.
	AEnemyBase* Enemy = Fixture.SpawnEnemy(FVector(-500.0, -500.0, 100.0));
	if (!BeginSearch(*this, Enemy, FVector::ZeroVector)) return false;
	Fixture.SpawnBlockingBox(FVector(600.0, 0.0, 100.0), FVector(70.0));
	const FVector First(500.0, -500.0, 100.0);
	const FVector BlockedSecond(600.0, 0.0, 100.0);
	const FVector ReachableSecond(0.0, 650.0, 100.0);
	FEnemyRunEnvQueryTaskInstanceData Data = MakeSearchData(Enemy, Fixture.Room, EEnemySearchQueryMode::Points);
	const FEnvQueryInstance Result = MakePointResult(Enemy, {First, BlockedSecond, ReachableSecond});
	TestTrue(TEXT("Selection uses next candidate after a blocked second leg"),
		EnemySearchTesting::SelectDestinations(Data, Result));
	TestEqual(TEXT("Two points selected despite dynamic obstacle"), Data.SelectedSearchPoints.Num(), 2);
	TestTrue(TEXT("Blocked second leg is skipped"), !Data.SelectedSearchPoints.Contains(BlockedSecond));
	TestTrue(TEXT("Reachable second leg is selected"), Data.SelectedSearchPoints.Contains(ReachableSecond));

	// 방 밖 후보는 실제 RoomVolume 경계 검사에서 탈락한다.
	FEnemySearchIntegrationWorld BoundaryFixture;
	if (!TestTrue(TEXT("Boundary room registers"), BoundaryFixture.CreateRoom(FVector(600.0, 900.0, 500.0)))) return false;
	AEnemyBase* BoundaryEnemy = BoundaryFixture.SpawnEnemy(FVector::ZeroVector);
	if (!BeginSearch(*this, BoundaryEnemy, FVector::ZeroVector)) return false;
	FEnemyRunEnvQueryTaskInstanceData BoundaryData = MakeSearchData(BoundaryEnemy, BoundaryFixture.Room, EEnemySearchQueryMode::Points);
	const FEnvQueryInstance BoundaryResult = MakePointResult(BoundaryEnemy, {FVector(0.0, 650.0, 0.0), FVector(700.0, 0.0, 0.0)});
	TestTrue(TEXT("Inside-room candidate succeeds"),
		EnemySearchTesting::SelectDestinations(BoundaryData, BoundaryResult));
	TestEqual(TEXT("Outside boundary candidate is not added"), BoundaryData.SelectedSearchPoints.Num(), 1);

	// 같은 막힘도 충분한 예산이면 NoPath, 지나치게 작은 예산이면 BudgetExceeded로 구분한다.
	FEnemySearchIntegrationWorld SealedFixture;
	if (!TestTrue(TEXT("Sealed room registers"), SealedFixture.CreateRoom(FVector(900.0, 900.0, 500.0)))) return false;
	SealedFixture.SpawnBlockingBox(FVector(0.0, 0.0, 100.0), FVector(40.0, 900.0, 500.0));
	AEnemyBase* NoPathEnemy = SealedFixture.SpawnEnemy(FVector(-500.0, 0.0, 100.0));
	if (!BeginSearch(*this, NoPathEnemy, FVector::ZeroVector)) return false;
	FEnemyRunEnvQueryTaskInstanceData NoPathData = MakeSearchData(NoPathEnemy, SealedFixture.Room, EEnemySearchQueryMode::Points);
	const FEnvQueryInstance NoPathResult = MakePointResult(NoPathEnemy, {FVector(650.0, 0.0, 100.0)});
	TestFalse(TEXT("Sealed wall prevents route"),
		EnemySearchTesting::SelectDestinations(NoPathData, NoPathResult));
	TestEqual(TEXT("Physical no-path is not a budget failure"),
		NoPathData.QueryResult, EEnemyEnvQueryResult::NoReachableDestination);
	TestEqual(TEXT("Physical failure records no path status"),
		NoPathData.SelectionPathStatus, EFlightPathStatus::NoPathWithinBounds);

	AEnemyBase* BudgetEnemy = SealedFixture.SpawnEnemy(FVector(-500.0, 300.0, 100.0));
	if (!BeginSearch(*this, BudgetEnemy, FVector::ZeroVector)) return false;
	FEnemyRunEnvQueryTaskInstanceData BudgetData = MakeSearchData(BudgetEnemy, SealedFixture.Room, EEnemySearchQueryMode::Points);
	BudgetData.MaxSearchNodes = 1;
	const FEnvQueryInstance BudgetResult = MakePointResult(BudgetEnemy, {FVector(650.0, 300.0, 100.0)});
	TestFalse(TEXT("Tiny search budget fails distinctly"),
		EnemySearchTesting::SelectDestinations(BudgetData, BudgetResult));
	TestEqual(TEXT("Budget failure is explicit"),
		BudgetData.QueryResult, EEnemyEnvQueryResult::PathBudgetExceeded);
	TestEqual(TEXT("Budget status is preserved"),
		BudgetData.SelectionPathStatus, EFlightPathStatus::BudgetExceeded);
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemySearchSelectionResumeIntegrationTest,
	"Outlier.Enemy.Search.Integration.SelectionResume",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemySearchSelectionResumeIntegrationTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("AnchorNextCandidate"), TEXT("PointsNextCandidate"), TEXT("RetryLimit"),
		TEXT("CandidateLimit"), TEXT("Disabled"), TEXT("NoRemainingCandidate"), TEXT("RemainingRejected"),
		TEXT("Stale"), TEXT("SightRegained"), TEXT("ControllerLost"), TEXT("SessionEnded"),
		TEXT("Timeout"), TEXT("MissingResult")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemySearchSelectionResumeIntegrationTest::RunTest(const FString& Parameters)
{
	FEnemySearchIntegrationWorld Fixture;
	if (!TestTrue(TEXT("Room registers"), Fixture.CreateRoom())) return false;
	Fixture.SpawnBlockingBox(FVector(0.0, 0.0, 100.0), FVector(40.0, 1600.0, 700.0));
	AEnemyBase* Enemy = Fixture.SpawnEnemy(FVector(-500.0, 0.0, 100.0));
	AEnemyAIController* Controller = Fixture.World->SpawnActor<AEnemyAIController>();
	if (!TestNotNull(TEXT("Enemy"), Enemy) || !TestNotNull(TEXT("Controller"), Controller)) return false;
	Controller->Possess(Enemy);
	if (!BeginSearch(*this, Enemy, FVector(100.0, 0.0, 100.0))) return false;
	const bool bPoints = Parameters == TEXT("PointsNextCandidate");
	FEnemyRunEnvQueryTaskInstanceData Data = MakeSearchData(Enemy, Fixture.Room,
		bPoints ? EEnemySearchQueryMode::Points : EEnemySearchQueryMode::Anchor);
	Data.MaxSearchNodes = 1;
	Data.MaxSearchSelectionRetries = 1;
	Data.MaxPathCandidates = 4;
	EnemySearchTesting::CaptureQuery(Data);
	const FVector First = bPoints ? FVector(650.0, 0.0, 100.0) : FVector(100.0, 0.0, 100.0);
	const FVector Safe = bPoints ? FVector(-650.0, 0.0, 100.0) : FVector(-200.0, 0.0, 100.0);
	TArray<FVector> Candidates = {First, Safe};
	if (Parameters == TEXT("RetryLimit")) Candidates = {First, FVector(200.0, 0.0, 100.0), FVector(300.0, 0.0, 100.0)};
	if (Parameters == TEXT("RemainingRejected")) Candidates = {First, FVector(-900.0, 0.0, 100.0)};
	if (Parameters == TEXT("CandidateLimit")) Data.MaxPathCandidates = 1;
	if (Parameters == TEXT("Disabled")) Data.MaxSearchSelectionRetries = 0;
	if (Parameters == TEXT("NoRemainingCandidate")) Candidates = {First};
	const TSharedPtr<FEnvQueryInstance> Result = MakeShared<FEnvQueryInstance>(MakePointResult(Enemy, Candidates));
	EnemySearchTesting::CompleteQuery(Data, *Result);
	TestFalse(TEXT("Budget exhaustion exposes no unverified destination"), Data.bHasDestination);
	TestEqual(TEXT("First candidate consumes one attempt"), Data.CheckedPathCandidates, 1);
	if (Parameters == TEXT("CandidateLimit") || Parameters == TEXT("Disabled") || Parameters == TEXT("NoRemainingCandidate"))
	{
		TestFalse(TEXT("Bounded selection cannot schedule more work"), Data.bSearchSelectionPending);
		TestEqual(TEXT("Exhaustion is not physical no-path"), Data.QueryResult, EEnemyEnvQueryResult::PathBudgetExceeded);
		return true;
	}
	if (!TestTrue(TEXT("Budget failure defers rather than completes the Task"), Data.bSearchSelectionPending)) return false;
	TestEqual(TEXT("Pending task remains Running"), Data.QueryResult, EEnemyEnvQueryResult::Running);
	Data.DeferredSearchResult = Result;
	TestEqual(TEXT("Interval prevents immediate expensive retry"),
		EnemySearchTesting::ResumeSelection(Data), EStateTreeRunStatus::Running);
	TestEqual(TEXT("Waiting does not recheck first candidate"), Data.CheckedPathCandidates, 1);
	Data.NextSearchSelectionTime = Fixture.World->GetTimeSeconds();
	const bool bStale = Parameters == TEXT("Stale") || Parameters == TEXT("SightRegained")
		|| Parameters == TEXT("ControllerLost") || Parameters == TEXT("SessionEnded");
	if (Parameters == TEXT("Stale")) ++Data.CapturedSearchSerial;
	if (Parameters == TEXT("SightRegained") && !RediscoverSearchTarget(*this, *Enemy, *Controller)) return false;
	if (Parameters == TEXT("ControllerLost")) Controller->UnPossess();
	if (Parameters == TEXT("SessionEnded")) Enemy->EndSearchEpisode();
	if (Parameters == TEXT("Timeout")) Data.QueryStartedAt = Fixture.World->GetTimeSeconds() - Data.QueryTimeout;
	if (Parameters == TEXT("MissingResult")) Data.DeferredSearchResult.Reset();
	const EStateTreeRunStatus Status = EnemySearchTesting::ResumeSelection(Data);
	if (bStale || Parameters == TEXT("Timeout") || Parameters == TEXT("MissingResult"))
	{
		TestEqual(TEXT("Invalid continuation fails safely"), Status, EStateTreeRunStatus::Failed);
		const EEnemyEnvQueryResult Expected = bStale ? EEnemyEnvQueryResult::Stale
			: Parameters == TEXT("Timeout") ? EEnemyEnvQueryResult::TimedOut : EEnemyEnvQueryResult::Failed;
		TestEqual(TEXT("Cancellation preserves its reason"), Data.QueryResult, Expected);
		TestEqual(TEXT("Cancelled selection performs no extra path checks"), Data.CheckedPathCandidates, 1);
		TestFalse(TEXT("Cancellation releases saved query"), Data.DeferredSearchResult.IsValid());
		TestFalse(TEXT("Cancellation clears pending flag"), Data.bSearchSelectionPending);
		return true;
	}
	TestEqual(TEXT("Continuation does not recheck the first candidate"), Data.CheckedPathCandidates,
		Parameters == TEXT("RemainingRejected") ? 1 : 2);
	TestFalse(TEXT("Final selection releases saved query"), Data.DeferredSearchResult.IsValid());
	if (Parameters == TEXT("RetryLimit") || Parameters == TEXT("RemainingRejected"))
	{
		TestEqual(TEXT("Retry limit terminates even with unchecked candidates"), Status, EStateTreeRunStatus::Failed);
		TestEqual(TEXT("Retry limit retains budget reason"), Data.QueryResult, EEnemyEnvQueryResult::PathBudgetExceeded);
		TestEqual(TEXT("Exactly one continuation was scheduled"), Data.SearchSelectionRetries, 1);
		TestFalse(TEXT("Retry exhaustion leaves no pending work"), Data.bSearchSelectionPending);
	}
	else
	{
		TestEqual(TEXT("Next reachable candidate succeeds"), Status, EStateTreeRunStatus::Succeeded);
		TestEqual(TEXT("Destination is the verified alternate"), Data.Destination, Safe);
		TestTrue(TEXT("Verified destination is exposed"), Data.bHasDestination);
		if (bPoints) TestEqual(TEXT("Only verified point is committed"), Enemy->GetSearchEpisode().Points.Num(), 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySearchReservationIntegrationTest,
	"Outlier.Enemy.Search.Integration.ReservationRejection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemySearchReservationIntegrationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemySearchIntegrationWorld Fixture;
	if (!TestTrue(TEXT("Room registers"), Fixture.CreateRoom())) return false;
	UEnemyRoomSubsystem* Rooms = Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
	AEnemyBase* First = Fixture.SpawnEnemy(FVector::ZeroVector);
	AEnemyBase* Second = Fixture.SpawnEnemy(FVector(0.0, 300.0, 0.0));
	if (!TestNotNull(TEXT("Room subsystem exists"), Rooms)
		|| !BeginSearch(*this, First, FVector::ZeroVector)
		|| !BeginSearch(*this, Second, FVector::ZeroVector))
	{
		return false;
	}

	const FVector Reserved(550.0, 0.0, 0.0);
	const FVector Alternative(0.0, 650.0, 0.0);
	TestTrue(TEXT("First enemy reserves a search point"),
		Rooms->ReserveSearchDestinations(First, {Reserved}, 200.0f));

	FEnemyRunEnvQueryTaskInstanceData Data = MakeSearchData(Second, Fixture.Room, EEnemySearchQueryMode::Points);
	const FEnvQueryInstance Result = MakePointResult(Second, {Reserved, Alternative});
	TestTrue(TEXT("Second enemy finds non-reserved alternative"),
		EnemySearchTesting::SelectDestinations(Data, Result));
	if (!TestEqual(TEXT("Reservation leaves one usable point"), Data.SelectedSearchPoints.Num(), 1)) return false;
	TestEqual(TEXT("Reserved point is rejected"), Data.SelectedSearchPoints[0], Alternative);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySearchAssetQueryIntegrationTest,
	"Outlier.Enemy.Search.Integration.AssetQueries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemySearchAssetQueryIntegrationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEnemySearchIntegrationWorld Fixture;
	if (!TestTrue(TEXT("Room registers"), Fixture.CreateRoom())) return false;
	AEnemyBase* Enemy = Fixture.SpawnEnemy(FVector::ZeroVector);
	if (!BeginSearch(*this, Enemy, FVector::ZeroVector)) return false;

	UEnvQueryManager* Manager = UEnvQueryManager::GetCurrent(Fixture.World);
	if (!TestNotNull(TEXT("EQS manager exists"), Manager)) return false;

	// 실제 에셋 템플릿을 동기 실행해 Generator/Test 연결이 살아 있는지 확인한다.
	UEnvQuery* AnchorQuery = LoadObject<UEnvQuery>(
		nullptr, TEXT("/Game/Blueprints/AI/VECDrone/EQS/EQS_Drone_SearchAnchor.EQS_Drone_SearchAnchor"));
	UEnvQuery* PointsQuery = LoadObject<UEnvQuery>(
		nullptr, TEXT("/Game/Blueprints/AI/VECDrone/EQS/EQS_SearchPoints.EQS_SearchPoints"));
	if (!TestNotNull(TEXT("SearchAnchor EQS asset loads"), AnchorQuery)
		|| !TestNotNull(TEXT("SearchPoints EQS asset loads"), PointsQuery))
	{
		return false;
	}

	FEnvQueryRequest AnchorRequest(AnchorQuery, Enemy);
	EnemySearch::SetQuerySnapshot(AnchorRequest, Enemy->GetSearchEpisode().Center, Enemy->GetActorLocation().Z);
	const TSharedPtr<FEnvQueryResult> AnchorResult = Manager->RunInstantQuery(AnchorRequest, EEnvQueryRunMode::AllMatching);
	TestTrue(TEXT("Anchor query finishes successfully"), AnchorResult.IsValid() && AnchorResult->IsSuccessful());
	TestTrue(TEXT("Anchor query returns point-compatible results"),
		AnchorResult.IsValid() && AnchorResult->ItemType
		&& AnchorResult->ItemType->IsChildOf(UEnvQueryItemType_Point::StaticClass()));
	TestTrue(TEXT("Anchor query produces at least one candidate"),
		AnchorResult.IsValid() && AnchorResult->Items.Num() > 0);

	FEnvQueryRequest PointsRequest(PointsQuery, Enemy);
	EnemySearch::SetQuerySnapshot(PointsRequest, Enemy->GetSearchEpisode().Center, Enemy->GetActorLocation().Z);
	const TSharedPtr<FEnvQueryResult> PointsResult = Manager->RunInstantQuery(PointsRequest, EEnvQueryRunMode::AllMatching);
	TestTrue(TEXT("Search points query finishes successfully"), PointsResult.IsValid() && PointsResult->IsSuccessful());
	TestTrue(TEXT("Search points query returns point-compatible results"),
		PointsResult.IsValid() && PointsResult->ItemType
		&& PointsResult->ItemType->IsChildOf(UEnvQueryItemType_Point::StaticClass()));
	TestTrue(TEXT("Search points query produces at least one candidate"),
		PointsResult.IsValid() && PointsResult->Items.Num() > 0);
	if (AnchorResult.IsValid() && AnchorResult->IsSuccessful())
	{
		FEnemyRunEnvQueryTaskInstanceData Data = MakeSearchData(Enemy, Fixture.Room, EEnemySearchQueryMode::Anchor);
		TestTrue(TEXT("Real anchor EQS result passes actual path selection"),
			EnemySearchTesting::SelectDestinations(Data, *AnchorResult));
	}
	if (PointsResult.IsValid() && PointsResult->IsSuccessful())
	{
		FEnemyRunEnvQueryTaskInstanceData Data = MakeSearchData(Enemy, Fixture.Room, EEnemySearchQueryMode::Points);
		TestTrue(TEXT("Real points EQS result passes actual path selection"),
			EnemySearchTesting::SelectDestinations(Data, *PointsResult));
		TestTrue(TEXT("Real EQS selects reachable search points within the episode limit"),
			Data.SelectedSearchPoints.Num() >= 1 && Data.SelectedSearchPoints.Num() <= Enemy->GetSearchPointLimit());
	}
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemySearchQueryCompletionTest,
	"Outlier.Enemy.Search.Integration.QueryCompletion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemySearchQueryCompletionTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("Success"), TEXT("FreshHint"), TEXT("NewEpisode"), TEXT("Rediscovery"),
		TEXT("LocalGuard"), TEXT("Stun"), TEXT("Impact"), TEXT("Death"), TEXT("Possessed"),
		TEXT("LeaseChanged"), TEXT("GenerationChanged"), TEXT("ControllerLost"), TEXT("RoomUnregistered"),
		TEXT("SessionEnded"), TEXT("TimedOut"), TEXT("Empty")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemySearchQueryCompletionTest::RunTest(const FString& Parameters)
{
	FEnemySearchIntegrationWorld Fixture;
	if (!TestTrue(TEXT("Room registers"), Fixture.CreateRoom())) return false;
	AEnemyBase* Enemy = Fixture.SpawnEnemy();
	if (!BeginSearch(*this, Enemy, FVector::ZeroVector)) return false;
	AEnemyAIController* Controller = Fixture.World->SpawnActor<AEnemyAIController>();
	if (!TestNotNull(TEXT("Controller exists"), Controller)) return false;
	Controller->Possess(Enemy);
	FEnemyRunEnvQueryTaskInstanceData Data = MakeSearchData(Enemy, Fixture.Room, EEnemySearchQueryMode::Points);
	EnemySearchTesting::CaptureQuery(Data);
	const FVector Goal(550.0, 0.0, 0.0);
	const FEnvQueryInstance Result = MakePointResult(Enemy, {Goal});
	EEnemyEnvQueryResult Expected = EEnemyEnvQueryResult::Stale;
	if (Parameters == TEXT("Success")) Expected = EEnemyEnvQueryResult::Success;
	else if (Parameters == TEXT("FreshHint")) Enemy->ApplySharedTargetContact(FVector(100.0, 200.0, 0.0), true, 10);
	else if (Parameters == TEXT("NewEpisode")) Enemy->BeginSearchEpisode();
	else if (Parameters == TEXT("Rediscovery"))
	{
		if (!RediscoverSearchTarget(*this, *Enemy, *Controller)) return false;
	}
	else if (Parameters == TEXT("LocalGuard")) Enemy->EnterSearchLocalGuard();
	else if (Parameters == TEXT("Stun")) Enemy->EnterStun();
	else if (Parameters == TEXT("Impact"))
	{
		// 충격 입력만 준비한다. 결과 폐기는 실제 Impact 상태/질의 적용 함수를 거친다.
		Enemy->ApplyMagneticPullVelocity(FVector(1000, 0, 0), Enemy);
		if (!TestTrue(TEXT("Impact reaction starts"), Enemy->BeginImpactReaction())) return false;
	}
	else if (Parameters == TEXT("Death"))
	{
		Enemy->GetAbilitySystemComponent()->AddLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("State.Dead")));
		Enemy->BeginDeathForPoolTesting();
		TestTrue(TEXT("Death state is active before the query completes"), Enemy->IsDead());
	}
	else if (Parameters == TEXT("Possessed")) Enemy->SetEnemyPossessed(true);
	else if (Parameters == TEXT("LeaseChanged")) ++Data.CapturedLease;
	else if (Parameters == TEXT("GenerationChanged")) ++Data.CapturedGeneration;
	else if (Parameters == TEXT("ControllerLost")) Controller->UnPossess();
	else if (Parameters == TEXT("RoomUnregistered")) Data.CachedRooms->UnregisterRoom(Fixture.Room);
	else if (Parameters == TEXT("SessionEnded")) Enemy->EndSearchEpisode();
	else if (Parameters == TEXT("TimedOut"))
	{
		Data.QueryStartedAt -= Data.QueryTimeout + 1.0;
		Expected = EEnemyEnvQueryResult::TimedOut;
	}
	else if (Parameters == TEXT("Empty")) Expected = EEnemyEnvQueryResult::NoResults;
	else
	{
		AddError(FString::Printf(TEXT("Unknown query completion scenario %s"), *Parameters));
		return false;
	}
	if (Parameters == TEXT("Empty")) EnemySearchTesting::CompleteQuery(Data, MakePointResult(Enemy, {}));
	else EnemySearchTesting::CompleteQuery(Data, Result);
	TestEqual(TEXT("Real result application classifies completion correctly"), Data.QueryResult, Expected);
	TestEqual(TEXT("Only a successful current query exposes a destination"), Data.bHasDestination, Expected == EEnemyEnvQueryResult::Success);
	TestEqual(TEXT("Only a successful result commits search points"), Enemy->GetSearchEpisode().bPointsSelected, Expected == EEnemyEnvQueryResult::Success);
	if (Expected == EEnemyEnvQueryResult::Success)
	{
		TestEqual(TEXT("Committed destination matches query result"), Data.Destination, Goal);
		TestTrue(TEXT("Successful query commits exactly one search point"), Enemy->GetSearchEpisode().Points.Num() == 1);
	}
	Enemy->EndSearchEpisode();
	AddInfo(FString::Printf(TEXT("[SearchScenario] QueryCompletion=%s Result=%s"),
		*Parameters, *UEnum::GetValueAsString(Data.QueryResult)));
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyChaseIntegrationTest,
	"Outlier.Enemy.SelfDestruct.Integration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyChaseIntegrationTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("ChaseSuccess"), TEXT("LiveQuery"), TEXT("SightLost"), TEXT("SightLostGrace"),
		TEXT("TargetChanged"), TEXT("TargetMoved"), TEXT("Empty"), TEXT("ForeignCandidate"),
		TEXT("BudgetExceeded"), TEXT("ChargeReady"), TEXT("ChargeBlocked"), TEXT("ChargeHeight"), TEXT("ChargeSightLost"), TEXT("ChargeSightLostGrace")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyChaseIntegrationTest::RunTest(const FString& Parameters)
{
	FEnemySearchIntegrationWorld Fixture;
	if (!TestTrue(TEXT("Room registers"), Fixture.CreateRoom())) return false;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASelfDestructDrone* Drone = Fixture.World->SpawnActor<ASelfDestructDrone>(
		ASelfDestructDrone::StaticClass(), FTransform(FVector(0, 0, 150)), Spawn);
	if (!TestNotNull(TEXT("Self destruct drone"), Drone)) return false;
	Drone->GetRoomTagComp()->AssignDefaultRoomTag(Fixture.SearchRoomTag);
	Drone->EnterCombatFromRoom(FVector(1000, 0, 150), false);
	AEnemyAIController* Controller = Fixture.World->SpawnActor<AEnemyAIController>();
	UClass* TargetClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	if (!TestNotNull(TEXT("Concrete player class exists"), TargetClass)) return false;
	AShooterCharacter* Target = Fixture.World->SpawnActor<AShooterCharacter>(TargetClass,
		FTransform(FVector(1000, 0, 150)), Spawn);
	if (!TestNotNull(TEXT("Controller"), Controller) || !TestNotNull(TEXT("Target"), Target)) return false;
	Target->SetActorEnableCollision(false);
	Target->GetCharacterMovement()->SetComponentTickEnabled(false);
	Controller->Possess(Drone);
	// 일반 Actor는 추적 대상으로 인정되지 않는다. 실제 플레이어를 관측한 상태부터 준비해
	// 아래 시나리오가 타깃 검증에서 조기 종료하지 않고 후보/결과/캡슐 Sweep/A*까지 검사하게 한다.
	const_cast<FEnemyStat&>(Drone->GetRuntimeStat()).TargetTrackingGraceSeconds = 3.0f;
	Controller->SetCachedVisibleTargetForTesting(Target);
	Drone->ObserveTrackedTarget(Target);
	Drone->SetPlayerCurrentlyVisible(true);
	if (!TestEqual(TEXT("Real player is tracked before scenario"), Drone->GetTrackedTarget(), static_cast<AActor*>(Target))) return false;
	if (Parameters.StartsWith(TEXT("Charge")))
	{
		Target->SetActorLocation(FVector(400, 0, 150));
		EEnemyChargeEligibility Expected = EEnemyChargeEligibility::Ready;
		if (Parameters == TEXT("ChargeBlocked"))
		{
			Fixture.SpawnBlockingBox(FVector(200, 0, 150), FVector(20, 200, 200));
			Expected = EEnemyChargeEligibility::BlockedPath;
		}
		else if (Parameters == TEXT("ChargeHeight"))
		{
			Target->SetActorLocation(FVector(400, 0, 550));
			Expected = EEnemyChargeEligibility::OutOfRange;
		}
		else if (Parameters == TEXT("ChargeSightLost") || Parameters == TEXT("ChargeSightLostGrace"))
		{
			// 유예 종료와 유예 중 진입을 분리한다. 0초 설정은 월드 Tick 없이 만료 경계를 검사한다.
			const bool bGraceActive = Parameters == TEXT("ChargeSightLostGrace");
			if (!bGraceActive) const_cast<FEnemyStat&>(Drone->GetRuntimeStat()).TargetTrackingGraceSeconds = 0.0f;
			Controller->SetCachedVisibleTargetForTesting(nullptr);
			Drone->SetPlayerCurrentlyVisible(false);
			TestFalse(TEXT("Lost sight does not remain directly visible"), Drone->IsPlayerCurrentlyVisible());
			TestEqual(TEXT("Charge tracking follows grace eligibility"), Drone->HasTrackableTarget(), bGraceActive);
			Expected = bGraceActive ? EEnemyChargeEligibility::Ready : EEnemyChargeEligibility::NoVisibleTarget;
		}
		TestEqual(TEXT("Authoritative charge entry result"), EnemyCharge::Assess(Drone, Controller, Target, 500, 5), Expected);
		return true;
	}
	FEnemyRunEnvQueryTaskInstanceData Data;
	Data.Enemy = Drone;
	Data.bValidateChasePath = true;
	Data.RunMode = EEnvQueryRunMode::AllMatching;
	Data.MaxSearchMilliseconds = 1000;
	EnemySearchTesting::CaptureQuery(Data);
	if (!TestEqual(TEXT("Chase snapshot captures the tracked player"), Data.CapturedTarget.Get(), static_cast<AActor*>(Target))) return false;
	if (Parameters == TEXT("LiveQuery"))
	{
		UEnvQueryManager* Manager = UEnvQueryManager::GetCurrent(Fixture.World);
		if (!TestNotNull(TEXT("Actual EQS manager"), Manager)) return false;
		UEnvQuery* Query = NewObject<UEnvQuery>();
		UEnvQueryOption* Option = NewObject<UEnvQueryOption>(Query);
		Query->GetOptionsMutable().Add(Option);
		Option->Generator = NewObject<UEnemyEnvQueryGenerator_Chase>(Option);
		Option->Tests.Add(NewObject<UEnemyEnvQueryTest_FlightDestination>(Option));
		FEnvQueryRequest Request(Query, Drone);
		EnemyChase::SetQuerySnapshot(Request, Data.CapturedChaseStart, Data.CapturedOrbitCenter, Data.CapturedChaseLane);
		const TSharedPtr<FEnvQueryResult> Result = Manager->RunInstantQuery(Request, EEnvQueryRunMode::AllMatching);
		if (!TestTrue(TEXT("Live chase query produces candidates"), Result.IsValid() && Result->IsSuccessful() && !Result->Items.IsEmpty())) return false;
		EnemySearchTesting::CompleteQuery(Data, *Result);
		TestEqual(TEXT("Real generator and destination filter feed bounded A star"), Data.QueryResult, EEnemyEnvQueryResult::Success);
		return true;
	}
	TArray<FVector> Candidates;
	EnemyChase::BuildCandidates(Data.CapturedChaseStart, Data.CapturedOrbitCenter, Data.CapturedChaseLane, Candidates);
	if (!TestTrue(TEXT("Chase candidates exist"), !Candidates.IsEmpty())) return false;
	EEnemyEnvQueryResult Expected = EEnemyEnvQueryResult::Stale;
	if (Parameters == TEXT("ChaseSuccess")) Expected = EEnemyEnvQueryResult::Success;
	else if (Parameters == TEXT("SightLost") || Parameters == TEXT("SightLostGrace"))
	{
		const bool bGraceActive = Parameters != TEXT("SightLost");
		if (!bGraceActive) const_cast<FEnemyStat&>(Drone->GetRuntimeStat()).TargetTrackingGraceSeconds = 0.0f;
		Controller->SetCachedVisibleTargetForTesting(nullptr);
		Drone->SetPlayerCurrentlyVisible(false);
		TestFalse(TEXT("Lost sight does not remain directly visible"), Drone->IsPlayerCurrentlyVisible());
		TestEqual(TEXT("Chase tracking follows grace eligibility"), Drone->HasTrackableTarget(), bGraceActive);
		Expected = bGraceActive ? EEnemyEnvQueryResult::Success : EEnemyEnvQueryResult::Stale;
	}
	else if (Parameters == TEXT("TargetChanged"))
	{
		AShooterCharacter* Replacement = Fixture.World->SpawnActor<AShooterCharacter>(TargetClass,
			FTransform(Target->GetActorLocation()), Spawn);
		if (!TestNotNull(TEXT("Replacement player"), Replacement)) return false;
		Replacement->SetActorEnableCollision(false);
		Controller->SetCachedVisibleTargetForTesting(Replacement);
		Drone->ObserveTrackedTarget(Replacement);
		if (!TestEqual(TEXT("Replacement player becomes tracked target"), Drone->GetTrackedTarget(), static_cast<AActor*>(Replacement))) return false;
	}
	else if (Parameters == TEXT("TargetMoved")) Target->SetActorLocation(FVector(1250, 0, 150));
	else if (Parameters == TEXT("Empty"))
	{
		Candidates.Reset();
		Expected = EEnemyEnvQueryResult::NoResults;
	}
	else if (Parameters == TEXT("ForeignCandidate"))
	{
		Candidates = {FVector(-800, 0, 150)};
		Expected = EEnemyEnvQueryResult::NoReachableDestination;
	}
	else if (Parameters == TEXT("BudgetExceeded"))
	{
		Data.MaxCollisionChecks = 1;
		Expected = EEnemyEnvQueryResult::PathBudgetExceeded;
	}
	else { AddError(TEXT("Unknown chase scenario")); return false; }
	EnemySearchTesting::CompleteQuery(Data, MakePointResult(Drone, Candidates));
	TestEqual(TEXT("Chase completion classification"), Data.QueryResult, Expected);
	TestEqual(TEXT("Only current reachable candidates expose a destination"), Data.bHasDestination, Expected == EEnemyEnvQueryResult::Success);
	TestEqual(TEXT("Query failure does not end combat"), Drone->GetCombatState(), EEnemyCombatState::Combat);
	TestFalse(TEXT("Chase failure cannot force search LocalGuard"), Drone->GetSearchEpisode().bLocalGuard);
	if (Expected == EEnemyEnvQueryResult::Success)
	{
		TestTrue(TEXT("Preferred lane order survives EQS ties"), Data.Destination.Equals(Candidates[0], 1));
		TestEqual(TEXT("Actual A star accepted candidate"), Data.SelectionPathStatus, EFlightPathStatus::Success);
	}
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemySearchWorldLifecycleTest,
	"Outlier.Enemy.Search.Integration.WorldLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemySearchWorldLifecycleTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("OpenRoom"), TEXT("Detour"), TEXT("PointDetour"), TEXT("OneCandidate"), TEXT("NoCandidates"),
		TEXT("NewHint"), TEXT("SightRegained")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
		// 기존 Gun 테스트 이름은 유지하고 같은 실제 월드 시나리오를 Melee에도 적용한다.
		const FString MeleeScenario = FString(TEXT("Melee.")) + Scenario;
		Names.Add(MeleeScenario);
		Commands.Add(MeleeScenario);
	}
}

bool FEnemySearchWorldLifecycleTest::RunTest(const FString& Parameters)
{
	const bool bMelee = Parameters.StartsWith(TEXT("Melee."));
	const FString Scenario = bMelee ? Parameters.RightChop(6) : Parameters;
	const bool bPointDetour = Scenario == TEXT("PointDetour");
	const bool bDetour = Scenario == TEXT("Detour") || bPointDetour;
	// 우회 실패는 후보 필터 탈락과 A* 예산 초과를 구분할 수 있도록 실제 질의 로그를 남긴다.
	// 테스트가 끝나면 기존 설정을 복원해 다른 테스트나 에디터의 로그 설정을 바꾸지 않는다.
	IConsoleVariable* QueryDebug = IConsoleManager::Get().FindConsoleVariable(TEXT("outlier.Enemy.EQSDebug"));
	const bool bEnableQueryDebug = bDetour && QueryDebug;
	const int32 PreviousQueryDebug = QueryDebug ? QueryDebug->GetInt() : 0;
	const EConsoleVariableFlags QueryDebugPriority = QueryDebug
		? static_cast<EConsoleVariableFlags>(QueryDebug->GetFlags() & ECVF_SetByMask) : ECVF_SetByCode;
	if (bEnableQueryDebug) QueryDebug->Set(2, QueryDebugPriority);
	IConsoleVariable* FlightDebug = IConsoleManager::Get().FindConsoleVariable(TEXT("outlier.Enemy.FlightDebug"));
	const int32 PreviousFlightDebug = FlightDebug ? FlightDebug->GetInt() : 0;
	const EConsoleVariableFlags FlightDebugPriority = FlightDebug
		? static_cast<EConsoleVariableFlags>(FlightDebug->GetFlags() & ECVF_SetByMask) : ECVF_SetByCode;
	if (bDetour && FlightDebug) FlightDebug->Set(2, FlightDebugPriority);
	IConsoleVariable* SegmentDebug = IConsoleManager::Get().FindConsoleVariable(TEXT("outlier.Flight.SegmentDebug"));
	const int32 PreviousSegmentDebug = SegmentDebug ? SegmentDebug->GetInt() : 0;
	const EConsoleVariableFlags SegmentPriority = SegmentDebug
		? static_cast<EConsoleVariableFlags>(SegmentDebug->GetFlags() & ECVF_SetByMask) : ECVF_SetByCode;
	if (bDetour && SegmentDebug) SegmentDebug->Set(1, SegmentPriority);
	IConsoleVariable* EndpointDebug = IConsoleManager::Get().FindConsoleVariable(TEXT("outlier.Flight.EndpointDebug"));
	const int32 PreviousEndpointDebug = EndpointDebug ? EndpointDebug->GetInt() : 0;
	const EConsoleVariableFlags EndpointPriority = EndpointDebug
		? static_cast<EConsoleVariableFlags>(EndpointDebug->GetFlags() & ECVF_SetByMask) : ECVF_SetByCode;
	if (bDetour && EndpointDebug) EndpointDebug->Set(1, EndpointPriority);
	ON_SCOPE_EXIT
	{
		if (bEnableQueryDebug) QueryDebug->Set(PreviousQueryDebug, QueryDebugPriority);
		if (bDetour && FlightDebug) FlightDebug->Set(PreviousFlightDebug, FlightDebugPriority);
		if (bDetour && SegmentDebug) SegmentDebug->Set(PreviousSegmentDebug, SegmentPriority);
		if (bDetour && EndpointDebug) EndpointDebug->Set(PreviousEndpointDebug, EndpointPriority);
	};
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	FEnemySearchIntegrationWorld Fixture(true);
	if (!TestTrue(TEXT("Test world begins play"), Fixture.World && Fixture.World->HasBegunPlay())) return false;
	const bool bNoCandidates = Scenario == TEXT("NoCandidates");
	const bool bOneCandidate = Scenario == TEXT("OneCandidate");
	const FVector RoomExtent = bPointDetour ? FVector(550, 350, 110) : bOneCandidate ? FVector(350, 100, 110)
		: bNoCandidates ? FVector(200, 200, 700) : FVector(1600, 1600, 700);
	if (!TestTrue(TEXT("Room registers"), Fixture.CreateRoom(RoomExtent))) return false;
	// 폭과 높이를 제한해 +X 5m 후보만 드론 크기까지 포함해 들어갈 수 있게 한다.
	// 다른 링/방향/높이 후보는 실제 Room/캡슐 검사에서 탈락한다.
	if (bOneCandidate) Fixture.Room->SetActorLocation(FVector(300, 0, 200));
	// +X 방향 후보만 방 안에 남기되, 벽 좌우에는 실제 우회할 수 있는 폭을 확보한다.
	if (bPointDetour) Fixture.Room->SetActorLocation(FVector(500, 0, 200));
	if (!TestNotNull(TEXT("Floor exists"), Fixture.SpawnBlockingBox(
		FVector(0, 0, -150), FVector(1800, 1800, 40)))) return false;
	if (Scenario == TEXT("Detour") && !TestNotNull(TEXT("Detour wall exists"),
		Fixture.SpawnBlockingBox(FVector(0, 0, 200), FVector(40, 180, 300)))) return false;
	if (bPointDetour && !TestNotNull(TEXT("Point detour wall exists"),
		Fixture.SpawnBlockingBox(FVector(250, 0, 200), FVector(40, 80, 300)))) return false;
	// 6.5m 후보는 두 번째 벽 안에 들어가 제외된다. 남은 5m / 8m 지점 사이도 다시 우회해야 한다.
	if (bPointDetour && !TestNotNull(TEXT("Second point detour wall exists"),
		Fixture.SpawnBlockingBox(FVector(650, 0, 200), FVector(40, 80, 300)))) return false;

	// 실제 BP의 공용 Tree / Linked Battle / EQS 설정을 사용하고 Task를 수동 완료하지 않는다.
	UClass* EnemyClass = LoadClass<AEnemyBase>(nullptr, bMelee
		? TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Explosion.BP_VECDrone_Explosion_C")
		: TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"));
	if (!TestNotNull(TEXT("Enemy BP loads"), EnemyClass)) return false;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Start = bOneCandidate || bPointDetour ? FVector(100, 0, 200)
		: bNoCandidates ? FVector(0, 0, 200) : FVector(-600, 0, 200);
	AEnemyBase* Enemy = Fixture.World->SpawnActor<AEnemyBase>(EnemyClass, FTransform(Start), Spawn);
	if (!TestNotNull(TEXT("Enemy spawns"), Enemy)) return false;
	// Melee 테스트가 잘못 연결된 Gun Tree를 실행하고 통과하지 않도록 실제 Linked Asset도 확인한다.
	const FStructProperty* BattleProperty = FindFProperty<FStructProperty>(EnemyClass, TEXT("BattleStateTreeReference"));
	if (!TestNotNull(TEXT("Battle tree reference exists"), BattleProperty)) return false;
	const FStateTreeReference* BattleReference = BattleProperty->ContainerPtrToValuePtr<FStateTreeReference>(Enemy);
	const FString ExpectedBattlePath = bMelee
		? TEXT("/Game/Blueprints/AI/VECDrone/StateTree/ST_VECDroneBattle_Melee.ST_VECDroneBattle_Melee")
		: TEXT("/Game/Blueprints/AI/VECDrone/StateTree/ST_VECDroneBattle_Gun.ST_VECDroneBattle_Gun");
	if (!TestTrue(TEXT("BP uses the expected battle asset"), BattleReference && BattleReference->GetStateTree()
		&& BattleReference->GetStateTree()->GetPathName() == ExpectedBattlePath)) return false;
	UStateTreeComponent* Tree = Enemy->GetStateTreeComponent();
	AEnemyAIController* Controller = Cast<AEnemyAIController>(Enemy->GetController());
	if (!TestNotNull(TEXT("Actual StateTree component exists"), Tree)
		|| !TestNotNull(TEXT("AI controller exists"), Controller)) return false;
	if (!TestTrue(TEXT("Enemy has a nonzero flight speed"), Enemy->GetRuntimeStat().MoveSpeed > 0)) return false;

	// 감지 입력만 차단한다. 플레이어나 관측자가 없으므로 공유 위치도 자연 갱신되지 않는다.
	Controller->SetEnemyPerceptionEnabled(false);
	Enemy->GetRoomTagComp()->AssignDefaultRoomTag(Fixture.SearchRoomTag);
	Enemy->SetPlayerCurrentlyVisible(false);
	const FVector Center = bOneCandidate || bPointDetour ? FVector(0, 0, 200)
		: bNoCandidates ? FVector(5000, 0, 200) : FVector(600, 0, 200);
	Enemy->EnterCombatFromRoom(Center, false);
	Controller->SetEnemyPerceptionEnabled(false);
	Enemy->UpdateLastKnownPlayerLocation(Center);
	Enemy->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	if (bPointDetour)
	{
		// 상태 전이를 보기 전에 양방향 격자 경로부터 증명한다. 실패하면 테스트 공간 문제와 실행 문제를 구분한다.
		FFlightPathRequest Request;
		Request.Start = FVector(800, 0, 200);
		Request.Goal = FVector(500, 0, 200);
		Request.SearchBounds = FBox(Request.Start.ComponentMin(Request.Goal), Request.Start.ComponentMax(Request.Goal)).ExpandBy(800);
		Request.bRequireRoom = true;
		Request.Room = Fixture.Room;
		const FFlightPathResult Path = FlightPathfinding::FindPath(*Fixture.World, *Enemy->GetCapsuleComponent(), Request);
		AddInfo(FString::Printf(TEXT("[SearchGeometry] Radius=%.1f HalfHeight=%.1f Cell=%.1f ReversePath=%s Checks=%d"),
			Enemy->GetCapsuleComponent()->GetScaledCapsuleRadius(), Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),
			Request.CellSize, *UEnum::GetValueAsString(Path.Status), Path.CollisionChecks));
		if (!TestEqual(TEXT("Point fixture has a reverse grid route before tree execution"), Path.Status, EFlightPathStatus::Success)) return false;
	}
	Tree->StopLogic(TEXT("Controlled search lifecycle setup"));
	// 기존 우회/지점 완료 검증은 두 지점의 경로 기하를 고정한다. 기본 4지점/12초는 별도 deadline 테스트가 검증한다.
	FEnemyStat& GeometryStat = const_cast<FEnemyStat&>(Enemy->GetRuntimeStat());
	GeometryStat.SearchMaxPoints = 2;
	GeometryStat.SearchMaxSeconds = 60.0f;
	Tree->StartLogic();

	// 실제 월드가 Timer / EQS callback / StateTree / Movement를 순서대로 진행한다.
	// 프레임마다 Task를 직접 호출하거나 위치를 순간 이동시키면 연결 오류를 놓칠 수 있다.
	constexpr float Step = 1.0f / 30.0f;
	constexpr int32 MaxFrames = 1800;
	bool bSawSearch = false;
	bool bSawAnchorMovement = false;
	bool bSawPointLookAround = false;
	bool bSawFirstPointDetour = false;
	bool bSawSecondPointDetour = false;
	bool bReachedGuard = false;
	FString LastStates;
	uint64 EpisodeSerial = 0;
	uint64 SharedRevision = Enemy->GetSharedTargetRevision();
	for (int32 Frame = 0; Frame < MaxFrames; ++Frame)
	{
		Fixture.Tick(Step);
		const TArray<FName> States = Tree->GetActiveStateNames();
		const FEnemySearchEpisode& Episode = Enemy->GetSearchEpisode();
		bSawSearch |= Episode.bActive;
		bSawAnchorMovement |= States.Contains(FName(TEXT("MoveAnchor")));
		bSawPointLookAround |= States.Contains(FName(TEXT("PointLookAround")));
		// 지점 이동 중 벽 너비 밖으로 실제 움직여야 한다. 선정 성공이나 제자리 상태 전이만으로 통과하지 않는다.
		if (States.Contains(FName(TEXT("MovePoint"))) && FMath::Abs(Enemy->GetActorLocation().Y) > 80.0)
		{
			bSawFirstPointDetour |= Episode.PointIndex == 0;
			bSawSecondPointDetour |= Episode.PointIndex == 1;
		}
		const FString StateNames = FString::JoinBy(States, TEXT(" > "),
			[](FName Name) { return Name.ToString(); });
		// 상태 전이 로그만으로는 우회 중 저속 진행과 경로 정체를 구분할 수 없다.
		// 실제 Task를 읽기만 하며, 제한 시간이나 이동 결과를 바꾸지 않는다.
		if (bDetour && (Frame % 30 == 0 || StateNames != LastStates))
		{
			const UEnemyStateTreeComponent* DiagnosticTree = Cast<UEnemyStateTreeComponent>(Tree);
			const EnemyCombatTesting::FSnapshot Snapshot = DiagnosticTree
				? EnemyCombatTesting::ReadComponent(*DiagnosticTree) : EnemyCombatTesting::FSnapshot{};
			const auto* Task = Snapshot.FindTask<FEnemyFlyToLocationTask>();
			const auto* Fly = Task ? Task->Data.GetPtr<const FEnemyFlyToLocationTaskInstanceData>() : nullptr;
			if (Fly && Enemy->GetCharacterMovement())
			{
				const UCharacterMovementComponent* Movement = Enemy->GetCharacterMovement();
				const bool bHasWaypoint = Fly->PathWaypoints.IsValidIndex(Fly->WaypointIndex);
				const FVector Next = bHasWaypoint ? Fly->PathWaypoints[Fly->WaypointIndex] : Fly->Destination;
				const float ExitSpeed = Fly->WaypointSpeedLimits.IsValidIndex(Fly->WaypointIndex)
					? Fly->WaypointSpeedLimits[Fly->WaypointIndex] : -1.0f;
				AddInfo(FString::Printf(TEXT("[SearchFlight] Time=%.2f Location=%s Destination=%s Next=%s Waypoint=%d/%d Path=%s Waiting=%d Repaths=%d Elapsed=%.2f Limit=%.2f NoProgress=%.2f"),
					Fixture.World->GetTimeSeconds(), *Enemy->GetActorLocation().ToCompactString(),
					*Fly->Destination.ToCompactString(), *Next.ToCompactString(), Fly->WaypointIndex,
					Fly->PathWaypoints.Num(), *UEnum::GetValueAsString(Fly->PathStatus), Fly->bWaitingForPath,
					Fly->RepathAttempts, Fly->MoveElapsed, Fly->MoveTimeout, Fly->NoProgressElapsed));
				AddInfo(FString::Printf(TEXT("[SearchFlightSpeed] Velocity=%s Speed=%.2f Max=%.2f Base=%.2f Multiplier=%.3f Exit=%.2f Acceleration=%.2f Braking=%.2f"),
					*Movement->Velocity.ToCompactString(), Movement->Velocity.Size(), Movement->MaxFlySpeed,
					Enemy->GetRuntimeStat().MoveSpeed, Fly->SpeedMultiplier, ExitSpeed,
					Movement->GetMaxAcceleration(), Movement->BrakingDecelerationFlying));
			}
		}
		if (StateNames != LastStates)
		{
			AddInfo(FString::Printf(TEXT("[SearchWorld] Scenario=%s Time=%.2f States=%s Location=%s Episode=%llu Selected=%d PointIndex=%d Checked=%d Guard=%d"),
				*Parameters, Fixture.World->GetTimeSeconds(), *StateNames, *Enemy->GetActorLocation().ToCompactString(),
				Episode.Serial, Episode.Points.Num(), Episode.PointIndex, Episode.CheckedPoints.Num(), Episode.bLocalGuard));
			LastStates = StateNames;
		}
		if (Episode.bLocalGuard && States.Contains(FName(TEXT("GuardWait"))))
		{
			bReachedGuard = true;
			EpisodeSerial = Episode.Serial;
			break;
		}
		if (Tree->GetStateTreeRunStatus() != EStateTreeRunStatus::Running || Enemy->IsDead()) break;
	}
	TestTrue(TEXT("Actual tree starts a search episode"), bSawSearch);
	if (!TestTrue(*FString::Printf(TEXT("Search reaches GuardWait within 60 simulated seconds: States=%s Location=%s Checked=%d"),
		*LastStates, *Enemy->GetActorLocation().ToCompactString(), Enemy->GetSearchEpisode().CheckedPoints.Num()), bReachedGuard)) return false;
	TestEqual(TEXT("No spontaneous shared updates"), Enemy->GetSharedTargetRevision(), SharedRevision);
	if (bNoCandidates)
	{
		TestEqual(TEXT("Unreachable center checks no points"), Enemy->GetSearchEpisode().CheckedPoints.Num(), 0);
		TestTrue(TEXT("No-candidate guard stays at spawn"), Enemy->GetActorLocation().Equals(Start, 10));
	}
	else
	{
		if (!bOneCandidate) TestTrue(TEXT("Anchor Fly state actually executes"), bSawAnchorMovement);
		TestTrue(TEXT("Point LookAround state actually executes"), bSawPointLookAround);
		TestEqual(TEXT("All expected points finish before guard"), Enemy->GetSearchEpisode().CheckedPoints.Num(), bOneCandidate ? 1 : 2);
		TestFalse(TEXT("Movement is real, not just a successful query"), Enemy->GetActorLocation().Equals(Start, 50));
		if (bPointDetour)
		{
			TestTrue(TEXT("First point Fly actually moves around the wall"), bSawFirstPointDetour);
			TestTrue(TEXT("Second point Fly actually moves around the wall"), bSawSecondPointDetour);
		}
	}

	// 유한 경계가 끝나기 전에는 같은 정보로 수색을 다시 시작하거나 이동하지 않는다.
	const FVector GuardLocation = Enemy->GetActorLocation();
	const double GuardEndsAt = Enemy->GetSearchEpisode().GuardEndsAt;
	const double GuardRemaining = GuardEndsAt - Fixture.World->GetTimeSeconds();
	if (!TestTrue(TEXT("GuardWait is reached before the finite guard deadline"), GuardRemaining > 2.0 * Step))
	{
		return false;
	}
	// GuardWait에 도달하기 전의 두리번도 3초 경계를 소비한다.
	// 실제 남은 시간의 절반을 남겨 아래의 새 단서가 귀환 전에 선점하도록 한다.
	const double ObserveUntil = Fixture.World->GetTimeSeconds() + FMath::Min(1.0, GuardRemaining * 0.5);
	while (Fixture.World->GetTimeSeconds() + Step < ObserveUntil)
	{
		Fixture.Tick(Step);
	}
	TestEqual(TEXT("No new information keeps the same episode"), Enemy->GetSearchEpisode().Serial, EpisodeSerial);
	TestTrue(TEXT("Guard remains active"), Enemy->GetSearchEpisode().bLocalGuard);
	TestTrue(TEXT("Guard does not move"), Enemy->GetActorLocation().Equals(GuardLocation, 10));
	TestEqual(TEXT("Guard observation does not restart its deadline"), Enemy->GetSearchEpisode().GuardEndsAt, GuardEndsAt);
	TestEqual(TEXT("Fresh-clue fixture remains in combat before guard completion"), Enemy->GetCombatState(), EEnemyCombatState::Combat);

	if (Scenario == TEXT("NewHint"))
	{
		const FVector Hint(0, 500, 200);
		Enemy->ApplySharedTargetContact(Hint, true, SharedRevision + 1);
		for (int32 Frame = 0; Frame < 60 && Enemy->GetSearchEpisode().Serial == EpisodeSerial; ++Frame)
			Fixture.Tick(Step);
		TestTrue(TEXT("Fresh hint restarts the actual tree"), Enemy->GetSearchEpisode().Serial > EpisodeSerial);
		TestTrue(TEXT("Restart uses the new center"), Enemy->GetSearchEpisode().Center.Equals(Hint, 1));
		const uint64 RestartSerial = Enemy->GetSearchEpisode().Serial;
		Enemy->ApplySharedTargetContact(Hint, true, SharedRevision + 1);
		for (int32 Frame = 0; Frame < 30; ++Frame) Fixture.Tick(Step);
		TestEqual(TEXT("Re-delivery of the same revision does not restart"), Enemy->GetSearchEpisode().Serial, RestartSerial);
	}
	else if (Scenario == TEXT("SightRegained"))
	{
		// 네이티브 Shooter는 추상 클래스다. 실제 플레이어 BP로 시야 재발견 입력을 구성한다.
		UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
			TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
		if (!TestNotNull(TEXT("Visible target BP loads"), ShooterClass))
		{
			return false;
		}
		AShooterCharacter* Target = Fixture.World->SpawnActor<AShooterCharacter>(ShooterClass,
			FTransform(Enemy->GetActorLocation() + FVector(1500, 0, 0)), Spawn);
		if (!TestNotNull(TEXT("Visible target fixture exists"), Target))
		{
			return false;
		}
		Target->SetActorEnableCollision(false);
		Target->GetCharacterMovement()->SetComponentTickEnabled(false);
		// 시야 플래그만 바꾸면 Sync의 TargetActor가 비어 있다. 실제 Sight 캐시에 자극을 전달한다.
		Controller->EnemyPerceptionComponent->RegisterStimulus(Target, FAIStimulus(
			*GetDefault<UAISense_Sight>(), 1.0f, Target->GetActorLocation(), Enemy->GetActorLocation()));
		Controller->EnemyPerceptionComponent->ProcessStimuli();
		TestEqual(TEXT("Sight cache contains the real target"), Controller->GetPreferredVisibleTarget(), static_cast<AActor*>(Target));
		// 감지 콜백이 수색 회차를 먼저 종료하므로 bActive로 Tick을 중단하면 전이를 확인할 수 없다.
		// 실제 Tree가 다음 프레임에서 전투 상태를 선택할 때까지 기다린다.
		for (int32 Frame = 0; Frame < 60
			&& !Tree->GetActiveStateNames().Contains(FName(TEXT("DirectCombat"))); ++Frame)
		{
			Fixture.Tick(Step);
		}
		AddInfo(FString::Printf(TEXT("[SearchWorld] SightRegained FinalStates=%s SearchActive=%d"),
			*FString::JoinBy(Tree->GetActiveStateNames(), TEXT(" > "),
				[](FName Name) { return Name.ToString(); }), Enemy->GetSearchEpisode().bActive));
		TestFalse(TEXT("Sight regained exits search"), Enemy->GetSearchEpisode().bActive);
		TestTrue(TEXT("Sight regained selects DirectCombat"), Tree->GetActiveStateNames().Contains(FName(TEXT("DirectCombat"))));
	}
	Tree->StopLogic(TEXT("Search lifecycle test complete"));
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemySearchDeadlineWorldTest,
	"Outlier.Enemy.Search.Integration.DeadlineWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemySearchDeadlineWorldTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Type : { TEXT("Gun"), TEXT("Melee") })
	{
		for (const TCHAR* Scenario : { TEXT("Move15"), TEXT("Move30"), TEXT("Move60"),
			TEXT("LookLargeDelta"), TEXT("SightAtDeadline") })
		{
			const FString Name = FString(Type) + TEXT(".") + Scenario;
			Names.Add(Name);
			Commands.Add(Name);
		}
	}
}

bool FEnemySearchDeadlineWorldTest::RunTest(const FString& Parameters)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	FEnemySearchIntegrationWorld Fixture(true);
	if (!TestTrue(TEXT("Deadline world begins play"), Fixture.World && Fixture.World->HasBegunPlay())
		|| !TestTrue(TEXT("Deadline room registers"), Fixture.CreateRoom()))
	{
		return false;
	}
	if (!TestNotNull(TEXT("Deadline floor exists"), Fixture.SpawnBlockingBox(FVector(0, 0, -150), FVector(1800, 1800, 40))))
	{
		return false;
	}
	const bool bMelee = Parameters.StartsWith(TEXT("Melee."));
	const bool bLook = Parameters.EndsWith(TEXT("LookLargeDelta"));
	const bool bSight = Parameters.EndsWith(TEXT("SightAtDeadline"));
	const float Step = Parameters.EndsWith(TEXT("Move15")) ? 1.0f / 15.0f
		: Parameters.EndsWith(TEXT("Move60")) ? 1.0f / 60.0f : 1.0f / 30.0f;
	UClass* EnemyClass = LoadClass<AEnemyBase>(nullptr, bMelee
		? TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Explosion.BP_VECDrone_Explosion_C")
		: TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"));
	if (!TestNotNull(TEXT("Deadline enemy BP loads"), EnemyClass))
	{
		return false;
	}
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// 회전 선점 fixture는 단서 XY에서 시작해 불필요한 앵커 접근 시간을 없앤다. 실제 지점 이동과 Look 진입은 Tree가 수행한다.
	const FVector Start = bLook ? FVector(600, 0, 200) : FVector(-600, 0, 200);
	AEnemyBase* Enemy = Fixture.World->SpawnActor<AEnemyBase>(EnemyClass, FTransform(Start), Spawn);
	if (!TestNotNull(TEXT("Deadline enemy spawns"), Enemy))
	{
		return false;
	}
	UStateTreeComponent* Tree = Enemy->GetStateTreeComponent();
	AEnemyAIController* Controller = Cast<AEnemyAIController>(Enemy->GetController());
	if (!TestNotNull(TEXT("Deadline tree component exists"), Tree) || !TestNotNull(TEXT("Deadline controller exists"), Controller))
	{
		return false;
	}
	Controller->SetEnemyPerceptionEnabled(false);
	Enemy->GetRoomTagComp()->AssignDefaultRoomTag(Fixture.SearchRoomTag);
	const FVector Clue(600, 0, 500);
	Enemy->SetPlayerCurrentlyVisible(false);
	Enemy->EnterCombatFromRoom(Clue, false);
	Controller->SetEnemyPerceptionEnabled(false);
	Enemy->UpdateLastKnownPlayerLocation(Clue);
	Enemy->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	FEnemyStat& Stat = const_cast<FEnemyStat&>(Enemy->GetRuntimeStat());
	TestEqual(TEXT("Default general search point limit is four"), Stat.SearchMaxPoints, 4);
	TestEqual(TEXT("Default general search deadline is twelve seconds"), Stat.SearchMaxSeconds, 12.0f);
	// 이동은 계속 진행하되 12초 안에 앵커에 도착하지 않는 속도다. 정체/이동 timeout으로 끝나는 회귀와 구분한다.
	Stat.MoveSpeed = bLook ? 600.0f : 50.0f;
	Tree->StopLogic(TEXT("Controlled deadline setup"));
	Tree->StartLogic();
	const FName ExpectedStep = bLook ? FName(TEXT("PointLookAround")) : FName(TEXT("MoveAnchor"));
	bool bSawExpectedStep = false;
	FString LastStepPath;
	for (int32 Frame = 0; Frame < FMath::CeilToInt(11.5f / Step); ++Frame)
	{
		Fixture.Tick(Step);
		const TArray<FName> ActiveStates = Tree->GetActiveStateNames();
		const FString StepPath = FString::JoinBy(ActiveStates, TEXT(" > "), [](const FName Name) { return Name.ToString(); });
		if (bLook && LastStepPath != StepPath)
		{
			AddInfo(FString::Printf(TEXT("[SearchDeadlineApproach] Time=%.3f Location=%s States=%s"),
				Fixture.World->GetTimeSeconds(), *Enemy->GetActorLocation().ToCompactString(), *StepPath));
			LastStepPath = StepPath;
		}
		if (ActiveStates.Contains(ExpectedStep))
		{
			bSawExpectedStep = true;
			break;
		}
		if (Enemy->GetSearchEpisode().bLocalGuard)
		{
			break;
		}
	}
	if (!TestTrue(ExpectedStep.ToString() + TEXT(" executes in the actual saved tree"), bSawExpectedStep))
	{
		return false;
	}
	const FEnemySearchEpisode Initial = Enemy->GetSearchEpisode();
	TestTrue(TEXT("Episode starts with its fixed twelve-second window"), FMath::IsNearlyEqual(Initial.EndsAt - Initial.StartedAt, 12.0));
	TestEqual(TEXT("Saved search captures the four-point limit"), Initial.MaxPoints, 4);
	const int32 CheckedBeforeDeadline = Initial.CheckedPoints.Num();
	if (bLook)
	{
		float CluePitch = 0.0f;
		TestTrue(TEXT("Actual search provides its frozen height clue"), Enemy->GetSearchLookPitch(CluePitch));
		const float BeforeError = FMath::Abs(FMath::FindDeltaAngleDegrees(Controller->GetControlRotation().Pitch, CluePitch));
		Fixture.Tick(Step);
		const float AfterError = FMath::Abs(FMath::FindDeltaAngleDegrees(Controller->GetControlRotation().Pitch, CluePitch));
		TestTrue(TEXT("Saved LookAround tick turns pitch toward the clue"), AfterError < BeforeError || AfterError <= 1.0f);
		// 큰 Delta에서도 회전 자식의 완료를 기다리지 않고 같은 고정 기한을 처리한다.
		// WorldSettings의 기본 프레임 제한이 입력 Delta를 줄이지 않게 이 임시 월드에서만 허용한다.
		const float LargeDelta = static_cast<float>(Initial.EndsAt - Fixture.World->GetTimeSeconds()) + 0.05f;
		AWorldSettings* Settings = Fixture.World->GetWorldSettings();
		if (!TestNotNull(TEXT("Large-delta fixture has world settings"), Settings))
		{
			return false;
		}
		TGuardValue<float> FrameLimitGuard(Settings->MaxUndilatedFrameTime, FMath::Max(Settings->MaxUndilatedFrameTime, LargeDelta));
		Fixture.Tick(LargeDelta);
		TestTrue(TEXT("Large-delta tick actually crosses the fixed deadline"), Fixture.World->GetTimeSeconds() >= Initial.EndsAt);
	}
	else if (bSight)
	{
		while (Fixture.World->GetTimeSeconds() + Step < Initial.EndsAt)
		{
			Fixture.Tick(Step);
		}
		if (!RediscoverSearchTarget(*this, *Enemy, *Controller))
		{
			return false;
		}
		// 직접 재발견과 기한 종료가 같은 평가에 겹쳐도 새 전투 의도가 먼저 반영되어야 한다.
		Fixture.Tick(Step);
	}
	else
	{
		while (Fixture.World->GetTimeSeconds() < Initial.EndsAt + Step)
		{
			Fixture.Tick(Step);
		}
	}
	// 전이 트리거와 자식 선택을 한 프레임 더 허용하되 수색 기한을 늘리지 않는다.
	Fixture.Tick(Step);
	const TArray<FName> States = Tree->GetActiveStateNames();
	AddInfo(FString::Printf(TEXT("[SearchDeadlineWorld] Case=%s Time=%.3f Started=%.3f Ends=%.3f States=%s Checked=%d"),
		*Parameters, Fixture.World->GetTimeSeconds(), Initial.StartedAt, Initial.EndsAt,
		*FString::JoinBy(States, TEXT(" > "), [](const FName Name) { return Name.ToString(); }),
		Enemy->GetSearchEpisode().CheckedPoints.Num()));
	if (bSight)
	{
		TestTrue(TEXT("Same-evaluation direct sight selects combat before deadline guard"), States.Contains(FName(TEXT("DirectCombat"))));
		TestFalse(TEXT("Direct sight leaves search ownership"), Enemy->GetSearchEpisode().bActive);
	}
	else
	{
		TestTrue(TEXT("Deadline enters LocalGuard"), Enemy->GetSearchEpisode().bLocalGuard);
		TestFalse(TEXT("Expired search stops its finite child immediately"), States.Contains(ExpectedStep));
		TestTrue(TEXT("Actual saved tree selects local guard"), States.Contains(FName(TEXT("GuardLookAround")))
			|| States.Contains(FName(TEXT("GuardWait"))) || States.Contains(FName(TEXT("EnterLocalGuard"))));
		TestEqual(TEXT("Deadline does not mark an unfinished point as checked"), Enemy->GetSearchEpisode().CheckedPoints.Num(), CheckedBeforeDeadline);
		TestEqual(TEXT("Deadline preserves the episode identity"), Enemy->GetSearchEpisode().Serial, Initial.Serial);
		const FVector GuardPosition = Enemy->GetActorLocation();
		const double GuardEndsAt = Enemy->GetSearchEpisode().GuardEndsAt;
		const double GuardObserveUntil = FMath::Min(Fixture.World->GetTimeSeconds() + 1.0, GuardEndsAt - Step);
		while (Fixture.World->GetTimeSeconds() + Step < GuardObserveUntil)
		{
			Fixture.Tick(Step);
		}
		TestTrue(TEXT("Guard remains stationary before its finite deadline"), Enemy->GetActorLocation().Equals(GuardPosition, 10.0f));
		TestTrue(TEXT("Guard wait remains reachable without per-tick guard reentry"), Tree->GetActiveStateNames().Contains(FName(TEXT("GuardWait")))
			|| Tree->GetActiveStateNames().Contains(FName(TEXT("GuardLookAround"))));
		TestEqual(TEXT("Guard ticking cannot refill the deadline"), Enemy->GetSearchEpisode().EndsAt, Initial.EndsAt);
		TestEqual(TEXT("Guard ticking cannot refill the three-second deadline"), Enemy->GetSearchEpisode().GuardEndsAt, GuardEndsAt);
		while (Fixture.World->GetTimeSeconds() < GuardEndsAt + 2.0 * Step)
		{
			Fixture.Tick(Step);
		}
		TestFalse(TEXT("Finite guard releases expired search ownership"), Enemy->GetSearchEpisode().bActive);
		TestTrue(TEXT("Finite guard hands off to return or completed home guard"),
			Enemy->GetCombatState() == EEnemyCombatState::Returning || Enemy->GetCombatState() == EEnemyCombatState::NonCombat);
		TestFalse(TEXT("Completed deadline cannot remain in the old combat search"),
			Tree->GetActiveStateNames().Contains(FName(TEXT("Search"))));
	}
	Tree->StopLogic(TEXT("Search deadline integration complete"));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySearchBudgetIsolationTest,
	"Outlier.Enemy.Search.Integration.BudgetIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemySearchBudgetIsolationTest::RunTest(const FString& Parameters)
{
	FEnemySearchIntegrationWorld Fixture;
	FEnemySearchIntegrationWorld Other;
	FlightPathfinding::BeginTestBudget(*Fixture.World, 4096);
	ON_SCOPE_EXIT { FlightPathfinding::EndTestBudget(); };
	TestEqual(TEXT("Test world replaces the asset budget"), FlightPathfinding::GetTestBudget(*Fixture.World, 8192), 4096);
	TestEqual(TEXT("Other worlds preserve asset settings"), FlightPathfinding::GetTestBudget(*Other.World, 8192), 8192);
	FlightPathfinding::RecordTestQuery(Fixture.World, false);
	FlightPathfinding::RecordTestQuery(Fixture.World, true);
	FlightPathfinding::RecordTestQuery(Other.World, false);
	FlightPathfinding::RecordTestQuery(Other.World, true);
	FlightPathfinding::RecordTestQuery(nullptr, true);
	TestEqual(TEXT("Only the measured world's EQS starts count"), FlightPathfinding::GetTestCost().QueriesStarted, int64(1));
	TestEqual(TEXT("Only the measured world's EQS completions count"), FlightPathfinding::GetTestCost().QueriesFinished, int64(1));
	FlightPathfinding::EndTestBudget();
	FlightPathfinding::RecordTestQuery(Fixture.World, false);
	TestEqual(TEXT("No EQS counting after cleanup"), FlightPathfinding::GetTestCost().QueriesStarted, int64(1));
	TestEqual(TEXT("Cleanup restores the normal asset budget"), FlightPathfinding::GetTestBudget(*Fixture.World, 2048), 2048);
	FlightPathfinding::BeginTestBudget(*Fixture.World, 4096);
	TestEqual(TEXT("A new measurement resets EQS counters"), FlightPathfinding::GetTestCost().QueriesStarted, int64(0));
	return true;
}

namespace
{
	TAutoConsoleVariable<int32> CVarSearchLoadEnabled(TEXT("outlier.Enemy.SearchLoadTest"), 0,
		TEXT("Explicitly allow high-memory search stress tests. 0: refuse, 1: allow."));

	struct FSearchLoadSetting
	{
		IConsoleVariable* Variable = nullptr;
		int32 Previous = 0;
		EConsoleVariableFlags Priority = ECVF_SetByCode;
	};

	struct FSearchLoadTick
	{
		int32 Frame = 0;
		double WorldMs = 0;
		FlightPathfinding::FTestCost Cost;
		FString StatesAfterTick;
	};

	struct FSearchLoadRun
	{
		FAutomationTestBase& Test;
		FString Name;
		int32 Count;
		bool bMelee;
		bool bWalls;
		UClass* EnemyClass = nullptr;
		TUniquePtr<FEnemySearchIntegrationWorld> Fixture;
		TArray<AEnemyBase*> Enemies;
		TArray<FSearchLoadSetting> Settings;
		TArray<double> TickTimes;
		TArray<FSearchLoadTick> SlowTicks;
		int32 Frame = 0;
		int32 Columns = 0;
		double Started = FPlatformTime::Seconds();
		double SpawnMs = 0;
		uint64 InitialMemory = FPlatformMemory::GetStats().UsedPhysical;
		uint64 PeakMemory = InitialMemory;
		bool bOwnsBudget = false;

		FSearchLoadRun(FAutomationTestBase& InTest, const FString& InName, int32 InCount, bool InMelee, bool InWalls)
			: Test(InTest), Name(InName), Count(InCount), bMelee(InMelee), bWalls(InWalls) {}

		~FSearchLoadRun()
		{
			if (bOwnsBudget) FlightPathfinding::EndTestBudget();
			// 취소/실패 때도 테스트 월드와 임시 진단 설정이 남지 않는다.
			Fixture.Reset();
			for (const FSearchLoadSetting& Setting : Settings)
				Setting.Variable->Set(Setting.Previous, Setting.Priority);
		}

		void Silence(const TCHAR* NameToDisable)
		{
			if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(NameToDisable))
			{
				FSearchLoadSetting& Setting = Settings.AddDefaulted_GetRef();
				Setting.Variable = Variable;
				Setting.Previous = Variable->GetInt();
				Setting.Priority = static_cast<EConsoleVariableFlags>(Variable->GetFlags() & ECVF_SetByMask);
				Variable->Set(0, Setting.Priority);
			}
		}

		bool Initialize()
		{
			for (const TCHAR* Variable : {TEXT("outlier.Enemy.EQSDebug"), TEXT("outlier.Enemy.FlightDebug"),
				TEXT("outlier.Flight.EndpointDebug"), TEXT("outlier.Flight.SegmentDebug"), TEXT("outlier.Flight.PathCostLog")}) Silence(Variable);
			EnemyClass = LoadClass<AEnemyBase>(nullptr, bMelee
				? TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Explosion.BP_VECDrone_Explosion_C")
				: TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"));
			if (!Test.TestNotNull(TEXT("Stress enemy BP loads"), EnemyClass)) return false;
			Fixture = MakeUnique<FEnemySearchIntegrationWorld>(true);
			Columns = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(Count)));
			if (!Test.TestTrue(TEXT("Stress room registers"), Fixture->CreateRoom(FVector(Columns * 2400 + 2000, Columns * 2400 + 2000, 1000)))) return false;
			Enemies.Reserve(Count);
			FlightPathfinding::BeginTestBudget(*Fixture->World, 4096);
			bOwnsBudget = true;
			Test.AddInfo(FString::Printf(TEXT("[SearchLoad] Begin=%s Count=%d Checks=4096 NodesAndTime=AssetValues Layout=Separated2400cm Authority=Standalone"), *Name, Count));
			Test.TestEqual(TEXT("Test world collision budget is exactly 4096"), FlightPathfinding::GetTestBudget(*Fixture->World, 2048), 4096);
			return true;
		}

		bool SpawnBatch()
		{
			// 메모리 고갈로 에디터를 종료시키는 대신 실패로 남기고 이미 생성한 월드를 정리한다.
			if (FPlatformMemory::GetStats().AvailablePhysical < 1024ull * 1024 * 1024)
			{
				Test.AddError(TEXT("Search load aborted: less than 1 GiB available physical memory."));
				Report();
				return false;
			}
			const double BatchStarted = FPlatformTime::Seconds();
			const int32 End = FMath::Min(Enemies.Num() + 25, Count);
			while (Enemies.Num() < End)
			{
				const int32 Index = Enemies.Num();
				const FVector Start((Index % Columns) * 2400, (Index / Columns) * 2400, 200);
				FActorSpawnParameters Parameters;
				Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				AEnemyBase* Enemy = Fixture->World->SpawnActor<AEnemyBase>(EnemyClass, FTransform(Start), Parameters);
				if (!Test.TestNotNull(TEXT("Stress enemy spawns"), Enemy)) return false;
				Enemies.Add(Enemy);
				AEnemyAIController* Controller = Cast<AEnemyAIController>(Enemy->GetController());
				UStateTreeComponent* Tree = Enemy->GetStateTreeComponent();
				if (!Test.TestNotNull(TEXT("Stress controller exists"), Controller) || !Test.TestNotNull(TEXT("Stress tree exists"), Tree)) return false;
				Tree->StopLogic(TEXT("Load test setup"));
				Enemy->GetRoomTagComp()->AssignDefaultRoomTag(Fixture->SearchRoomTag);
				Enemy->EnterCombatFromRoom(Start + FVector(600, 0, 0), false);
				Controller->SetEnemyPerceptionEnabled(false);
				Enemy->SetPlayerCurrentlyVisible(false);
				Enemy->UpdateLastKnownPlayerLocation(Start + FVector(600, 0, 0));
				Enemy->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
				if (bWalls && !Test.TestNotNull(TEXT("Stress obstacle exists"),
					Fixture->SpawnBlockingBox(Start + FVector(300, 0, 0), FVector(40, 180, 300)))) return false;
				Tree->StartLogic();
			}
			SpawnMs += (FPlatformTime::Seconds() - BatchStarted) * 1000;
			return true;
		}

		void RecordTick(double Ms, const FlightPathfinding::FTestCost& Before)
		{
			TickTimes.Add(Ms);
			// 비용 차이는 이 World Tick에 실행된 호출만 나타낸다. 상태 집계/로그는 측정 밖에서 수행한다.
			if (SlowTicks.Num() == 8 && Ms <= SlowTicks.Last().WorldMs) return;
			const FlightPathfinding::FTestCost After = FlightPathfinding::GetTestCost();
			FSearchLoadTick Tick;
			Tick.Frame = Frame;
			Tick.WorldMs = Ms;
			Tick.Cost.Paths = After.Paths - Before.Paths;
			Tick.Cost.Failures = After.Failures - Before.Failures;
			Tick.Cost.Budgets = After.Budgets - Before.Budgets;
			Tick.Cost.Checks = After.Checks - Before.Checks;
			Tick.Cost.Nodes = After.Nodes - Before.Nodes;
			Tick.Cost.TotalMs = After.TotalMs - Before.TotalMs;
			Tick.Cost.QueriesStarted = After.QueriesStarted - Before.QueriesStarted;
			Tick.Cost.QueriesFinished = After.QueriesFinished - Before.QueriesFinished;
			TMap<FName, int32> StateCounts;
			for (const AEnemyBase* Enemy : Enemies)
			{
				const TArray<FName> States = Enemy->GetStateTreeComponent()->GetActiveStateNames();
				++StateCounts.FindOrAdd(States.IsEmpty() ? FName(TEXT("Inactive")) : States.Last());
			}
			TArray<FString> States;
			for (const TPair<FName, int32>& Pair : StateCounts)
				States.Add(FString::Printf(TEXT("%s:%d"), *Pair.Key.ToString(), Pair.Value));
			States.Sort();
			Tick.StatesAfterTick = FString::Join(States, TEXT(","));
			SlowTicks.Add(MoveTemp(Tick));
			SlowTicks.Sort([](const FSearchLoadTick& A, const FSearchLoadTick& B) { return A.WorldMs > B.WorldMs; });
			if (SlowTicks.Num() > 8) SlowTicks.Pop();
		}

		void Report()
		{
			for (const FSearchLoadTick& Tick : SlowTicks)
			{
				// FlightMs는 경로/공간/구간 검사 전체다. 나머지는 EQS 자체, 이동/물리 등을 포함하며 단일 원인으로 해석하지 않는다.
				Test.AddInfo(FString::Printf(TEXT("[SearchLoad] SlowTick=%s Frame=%d SimSeconds=%.3f WorldMs=%.3f FlightMs=%.3f OtherWorldMs=%.3f Paths=%lld Failed=%lld BudgetExceeded=%lld Checks=%lld Nodes=%lld EQSStarted=%lld EQSFinished=%lld StatesAfterTick=%s"),
					*Name, Tick.Frame, (Tick.Frame + 1) / 30.0, Tick.WorldMs, Tick.Cost.TotalMs,
					FMath::Max(0.0, Tick.WorldMs - Tick.Cost.TotalMs), Tick.Cost.Paths, Tick.Cost.Failures,
					Tick.Cost.Budgets, Tick.Cost.Checks, Tick.Cost.Nodes, Tick.Cost.QueriesStarted,
					Tick.Cost.QueriesFinished, *Tick.StatesAfterTick));
			}
			TickTimes.Sort();
			double Total = 0;
			for (double Time : TickTimes) Total += Time;
			int32 Guards = 0;
			int32 Checked = 0;
			for (const AEnemyBase* Enemy : Enemies)
			{
				Guards += Enemy->GetSearchEpisode().bLocalGuard;
				Checked += Enemy->GetSearchEpisode().CheckedPoints.Num();
			}
			const FlightPathfinding::FTestCost Cost = FlightPathfinding::GetTestCost();
			Test.AddInfo(FString::Printf(TEXT("[SearchLoad] End=%s Spawned=%d SpawnMs=%.1f MeasuredFrames=%d TickAvgMs=%.3f TickP95Ms=%.3f TickMaxMs=%.3f ProcessPeakMiB=%.1f ProcessGrowthMiB=%.1f Paths=%lld Failed=%lld BudgetExceeded=%lld Checks=%lld Nodes=%lld CollisionTotalMs=%.3f PeakCallMs=%.3f Guards=%d CheckedPoints=%d"),
				*Name, Enemies.Num(), SpawnMs, TickTimes.Num(), Total / FMath::Max(TickTimes.Num(), 1),
				TickTimes.IsEmpty() ? 0 : TickTimes[FMath::Min(FMath::CeilToInt(TickTimes.Num() * 0.95) - 1, TickTimes.Num() - 1)],
				TickTimes.IsEmpty() ? 0 : TickTimes.Last(), PeakMemory / 1048576.0,
				(static_cast<double>(PeakMemory) - InitialMemory) / 1048576.0,
				Cost.Paths, Cost.Failures, Cost.Budgets, Cost.Checks, Cost.Nodes, Cost.TotalMs, Cost.PeakMs, Guards, Checked));
			Test.TestEqual(TEXT("Full requested population exists"), Enemies.Num(), Count);
			Test.TestTrue(TEXT("Real EQS/Fly path calls were measured"), Cost.Paths > 0);
		}
	};

	class FSearchLoadCommand : public IAutomationLatentCommand
	{
		TSharedPtr<FSearchLoadRun> Run;
	public:
		explicit FSearchLoadCommand(TSharedPtr<FSearchLoadRun> InRun) : Run(MoveTemp(InRun)) {}
		virtual bool Update() override
		{
			// 5000마리 생성도 여러 에디터 프레임으로 나눈다. 측정은 생성 완료 후 실제 월드 Tick만 포함한다.
			if (!Run->Fixture && !Run->Initialize()) return true;
			if (FPlatformTime::Seconds() - Run->Started > 300)
			{
				Run->Test.AddError(TEXT("Search load exceeded 300 wall seconds; aborted, not a passing measurement."));
				Run->Report();
				return true;
			}
			if (Run->Enemies.Num() < Run->Count)
			{
				if (!Run->SpawnBatch()) return true;
				Run->PeakMemory = FMath::Max(Run->PeakMemory, FPlatformMemory::GetStats().UsedPhysical);
				return false;
			}
			TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
			const FlightPathfinding::FTestCost Before = FlightPathfinding::GetTestCost();
			const double Started = FPlatformTime::Seconds();
			Run->Fixture->Tick(1.0f / 30.0f);
			const double Ms = (FPlatformTime::Seconds() - Started) * 1000;
			// 시작 폭주도 숨기지 않도록 모든 Tick을 기록한다. 약 20초의 수색과 Guard 구간을 포함한다.
			Run->RecordTick(Ms, Before);
			Run->PeakMemory = FMath::Max(Run->PeakMemory, FPlatformMemory::GetStats().UsedPhysical);
			if (Run->Frame % 120 == 0)
				Run->Test.AddInfo(FString::Printf(TEXT("[SearchLoad] Progress=%s Frame=%d/600 TickMs=%.3f ProcessMiB=%.1f"),
					*Run->Name, Run->Frame, Ms, Run->PeakMemory / 1048576.0));
			if (Ms > 5000)
			{
				Run->Test.AddError(TEXT("One world Tick exceeded 5000ms; aborted. Reduce population or profile this case alone."));
				Run->Report();
				return true;
			}
			if (++Run->Frame < 600) return false;
			Run->Report();
			return true;
		}
	};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemySearchLoadTest, "Outlier.Enemy.Load.Search",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::StressFilter)

void FEnemySearchLoadTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Type : {TEXT("Gun"), TEXT("Melee")})
		for (const TCHAR* Layout : {TEXT("Open"), TEXT("Walls")})
			for (int32 Count : {500, 1500, 2500, 5000})
			{
				const FString Name = FString::Printf(TEXT("%s.%s.%d"), Type, Layout, Count);
				Names.Add(Name);
				Commands.Add(Name);
			}
}

bool FEnemySearchLoadTest::RunTest(const FString& Parameters)
{
	if (CVarSearchLoadEnabled.GetValueOnGameThread() != 1)
	{
		AddError(TEXT("Explicit opt-in required: outlier.Enemy.SearchLoadTest 1. Run one population at a time, starting with 500."));
		return false;
	}
	TArray<FString> Parts;
	Parameters.ParseIntoArray(Parts, TEXT("."));
	if (Parts.Num() != 3) return false;
	const int32 Count = FCString::Atoi(*Parts[2]);
	if ((Parts[0] != TEXT("Gun") && Parts[0] != TEXT("Melee"))
		|| (Parts[1] != TEXT("Open") && Parts[1] != TEXT("Walls"))
		|| (Count != 500 && Count != 1500 && Count != 2500 && Count != 5000)) return false;
	TSharedPtr<FSearchLoadRun> Run = MakeShared<FSearchLoadRun>(*this, Parameters, Count,
		Parts[0] == TEXT("Melee"), Parts[1] == TEXT("Walls"));
	ADD_LATENT_AUTOMATION_COMMAND(FSearchLoadCommand(Run));
	return true;
}

namespace
{
AEnemyBase* SpawnPatrolEnemy(FEnemySearchIntegrationWorld& Fixture, const FVector& Location)
{
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AEnemyBase* Enemy = Fixture.World->SpawnActor<AEnemyBase>(AEnemyBase::StaticClass(), FTransform(Location), Parameters);
	if (!Enemy) return nullptr;
	Enemy->GetRoomTagComp()->AssignDefaultRoomTag(Fixture.SearchRoomTag);
	FEnumProperty* Behavior = FindFProperty<FEnumProperty>(AEnemyBase::StaticClass(), TEXT("NonCombatBehavior"));
	if (!Behavior) return nullptr;
	Behavior->GetUnderlyingProperty()->SetIntPropertyValue(Behavior->ContainerPtrToValuePtr<void>(Enemy),
		static_cast<int64>(EEnemyNonCombatBehavior::PatrolRoute));
	AEnemyAIController* Controller = Fixture.World->SpawnActor<AEnemyAIController>();
	if (!Controller) return nullptr;
	Controller->Possess(Enemy);
	Enemy->GetCapsuleComponent()->SetCapsuleSize(20.0f, 40.0f);
	return Enemy;
}

FEnemyPatrolTaskInstanceData MakePatrolData(AEnemyBase* Enemy)
{
	FEnemyPatrolTaskInstanceData Data;
	Data.Enemy = Enemy;
	Data.MaxSearchMilliseconds = 100.0f;
	Data.MaxSearchNodes = 4096;
	Data.MaxCollisionChecks = 16384;
	return Data;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyPatrolSelectionTest, "Outlier.Enemy.Patrol.SelectionAndResume",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyPatrolSelectionTest::RunTest(const FString& Parameters)
{
	FEnemySearchIntegrationWorld Fixture;
	if (!TestTrue(TEXT("Patrol room created"), Fixture.CreateRoom())) return false;
	AEnemyBase* Enemy = SpawnPatrolEnemy(Fixture, FVector::ZeroVector);
	if (!TestNotNull(TEXT("Patrol enemy created"), Enemy)) return false;
	FEnemyPatrolTaskInstanceData Data = MakePatrolData(Enemy);
	TestEqual(TEXT("Select B first, not the current spawn point A"), EnemyPatrolTesting::Enter(Data), EStateTreeRunStatus::Succeeded);
	TestTrue(TEXT("B is selected"), Data.bHasDestination && Data.Destination.Equals(Enemy->GetPatrolPointB()));
	TestTrue(TEXT("Selection supplies an initial A* path"), Data.SelectedWaypoints.Num() >= 2);
	TestEqual(TEXT("Reenter respects the shared path cooldown"), EnemyPatrolTesting::Enter(Data), EStateTreeRunStatus::Running);
	Enemy->SetNextFlightPathRequestTime(0.0);
	Enemy->SetActorLocation(Enemy->GetPatrolPointB());
	TestEqual(TEXT("After B, select A"), EnemyPatrolTesting::Tick(Data), EStateTreeRunStatus::Succeeded);
	TestTrue(TEXT("A is selected"), Data.Destination.Equals(Enemy->GetPatrolPointA()));
	Enemy->SetActorLocation(FVector::ZeroVector);
	AActor* Block = Fixture.SpawnBlockingBox(Enemy->GetPatrolPointB(), FVector(100.0));
	if (!TestNotNull(TEXT("B blocker created"), Block)) return false;
	Enemy->SetNextFlightPathRequestTime(0.0);
	TestEqual(TEXT("Blocked B and already-reached A enter guard"), EnemyPatrolTesting::Enter(Data), EStateTreeRunStatus::Failed);
	TestFalse(TEXT("Guard has no stale destination"), Data.bHasDestination);
	TestTrue(TEXT("Guard has no stale path"), Data.SelectedWaypoints.IsEmpty());
	Enemy->SetActorLocation(FVector(0.0, 300.0, 0.0));
	AActor* BlockA = Fixture.SpawnBlockingBox(Enemy->GetPatrolPointA(), FVector(100.0));
	if (!TestNotNull(TEXT("A blocker created"), BlockA)) return false;
	Enemy->SetNextFlightPathRequestTime(0.0);
	TestEqual(TEXT("Both blocked endpoints enter guard without movement"), EnemyPatrolTesting::Enter(Data), EStateTreeRunStatus::Failed);
	BlockA->SetActorEnableCollision(false);
	Enemy->SetNextFlightPathRequestTime(0.0);
	TestEqual(TEXT("Only reachable A is selected when B is blocked"), EnemyPatrolTesting::Enter(Data), EStateTreeRunStatus::Succeeded);
	TestTrue(TEXT("Fallback is A"), Data.Destination.Equals(Enemy->GetPatrolPointA()));
	Enemy->SetActorLocation(FVector::ZeroVector);
	Block->SetActorEnableCollision(false);
	Enemy->SetNextFlightPathRequestTime(0.0);
	TestEqual(TEXT("Opening B resumes patrol"), EnemyPatrolTesting::Enter(Data), EStateTreeRunStatus::Succeeded);
	Data.MaxCollisionChecks = 1;
	Enemy->SetNextFlightPathRequestTime(0.0);
	TestEqual(TEXT("Limited selection returns to guard"), EnemyPatrolTesting::Enter(Data), EStateTreeRunStatus::Failed);
	TestEqual(TEXT("Budget is not reported as no path"), Data.PathStatus, EFlightPathStatus::BudgetExceeded);
	Data.RetryInterval = 0.0f;
	TestEqual(TEXT("Invalid interval is rejected"), EnemyPatrolTesting::Enter(Data), EStateTreeRunStatus::Failed);
	TestEqual(TEXT("Invalid configuration is explicit"), Data.PathStatus, EFlightPathStatus::InvalidRequest);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyPatrolReservationTest, "Outlier.Enemy.Patrol.ReservationLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyPatrolReservationTest::RunTest(const FString& Parameters)
{
	FEnemySearchIntegrationWorld Fixture;
	if (!Fixture.CreateRoom()) return false;
	AEnemyBase* Enemy = SpawnPatrolEnemy(Fixture, FVector::ZeroVector);
	AEnemyBase* Other = SpawnPatrolEnemy(Fixture, FVector(0.0, 300.0, 0.0));
	if (!TestNotNull(TEXT("Patrol owner"), Enemy) || !TestNotNull(TEXT("Patrol observer"), Other)) return false;
	UEnemyRoomSubsystem* Rooms = Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
	FEnemyPatrolTaskInstanceData Session = MakePatrolData(Enemy);
	Session.Action = EEnemyPatrolAction::Session;
	TestEqual(TEXT("Parent session remains running"), EnemyPatrolTesting::Enter(Session), EStateTreeRunStatus::Running);
	const FVector Point = Enemy->GetPatrolPointB();
	TestTrue(TEXT("Reserve patrol point"), Rooms->ReservePatrolDestination(Enemy, Point));
	TestTrue(TEXT("Other drone deprioritizes the reserved point"), Rooms->IsPatrolDestinationUsed(Other, Point, 200.0f));
	TestTrue(TEXT("Reservation is soft, not an exclusion"), Rooms->ReservePatrolDestination(Other, Point));
	Rooms->ReleasePatrolDestination(Other);
	Rooms->ReleasePatrolDestination(Enemy, Session.Generation, Session.Lease + 1, Enemy->GetController());
	TestTrue(TEXT("Stale lease cannot release current reservation"), Rooms->IsPatrolDestinationUsed(Other, Point, 200.0f));
	EnemyPatrolTesting::Exit(Session);
	TestFalse(TEXT("Parent exit releases reservation"), Rooms->IsPatrolDestinationUsed(Other, Point, 200.0f));
	Enemy->SetActorLocation(FVector(0.0, -300.0, 0.0));
	Rooms->ReservePatrolDestination(Other, Point);
	FEnemyPatrolTaskInstanceData Select = MakePatrolData(Enemy);
	TestEqual(TEXT("Select a reachable non-shared point first"), EnemyPatrolTesting::Enter(Select), EStateTreeRunStatus::Succeeded);
	TestTrue(TEXT("Unused A precedes preferred but shared B"), Select.Destination.Equals(Enemy->GetPatrolPointA()));
	Rooms->ReleasePatrolDestination(Other);
	AEnemyAIController* Replacement = Fixture.World->SpawnActor<AEnemyAIController>();
	if (!TestNotNull(TEXT("Replacement controller"), Replacement)) return false;
	Replacement->Possess(Enemy);
	Rooms->ReservePatrolDestination(Enemy, Point);
	EnemyPatrolTesting::Exit(Session);
	TestTrue(TEXT("Old controller exit cannot clear new controller reservation"), Rooms->IsPatrolDestinationUsed(Other, Point, 200.0f));
	Rooms->ReservePatrolDestination(Enemy, Point);
	Enemy->EnterAlert(FVector::ZeroVector);
	TestFalse(TEXT("Alert releases patrol reservation"), Rooms->IsPatrolDestinationUsed(Other, Point, 200.0f));
	TestEqual(TEXT("Session stops on alert"), EnemyPatrolTesting::Tick(Session), EStateTreeRunStatus::Failed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyPatrolDetourTest, "Outlier.Enemy.Patrol.DetourAndBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyPatrolDetourTest::RunTest(const FString& Parameters)
{
	FEnemySearchIntegrationWorld Fixture;
	if (!Fixture.CreateRoom()) return false;
	AEnemyBase* Enemy = SpawnPatrolEnemy(Fixture, FVector::ZeroVector);
	if (!Enemy) return false;
	if (!Fixture.SpawnBlockingBox(FVector(500.0, 0.0, 0.0), FVector(60.0, 200.0, 200.0))) return false;
	FEnemyPatrolTaskInstanceData Data = MakePatrolData(Enemy);
	TestEqual(TEXT("Reachable B is selected around a wall"), EnemyPatrolTesting::Enter(Data), EStateTreeRunStatus::Succeeded);
	TestTrue(TEXT("Wall requires a detour"), Data.SelectedWaypoints.Num() > 2);
	// 초기 방 경계 밖의 B는 사용하지 않는다. 현재 위치 A도 후보가 아니므로 Guard로 간다.
	Enemy->SetNextFlightPathRequestTime(0.0);
	CastChecked<UBoxComponent>(Fixture.Room->GetRootComponent())->SetBoxExtent(FVector(300.0, 300.0, 700.0));
	TestEqual(TEXT("Outside-room B is rejected"), EnemyPatrolTesting::Enter(Data), EStateTreeRunStatus::Failed);
	TestFalse(TEXT("Boundary rejection produces no destination"), Data.bHasDestination);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyPatrolSpawnTransformTest, "Outlier.Enemy.Patrol.SpawnTransformReset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyPatrolSpawnTransformTest::RunTest(const FString& Parameters)
{
	FEnemySearchIntegrationWorld Fixture;
	if (!Fixture.CreateRoom()) return false;
	AEnemyBase* Enemy = SpawnPatrolEnemy(Fixture, FVector::ZeroVector);
	if (!Enemy) return false;
	const FTransform SpawnTransform(FRotator(0.0, 90.0, 0.0), FVector(200.0, 300.0, 100.0), FVector(2.0));
	Enemy->SetActorTransform(SpawnTransform);
	// 풀 대여에서도 호출하는 동일 초기화 경로다. 위치/회전만 적용하고 Actor Scale은 오프셋에 곱하지 않는다.
	Enemy->ResetPatrolPointsForTesting();
	TestTrue(TEXT("A follows the new spawn origin"), Enemy->GetPatrolPointA().Equals(SpawnTransform.GetLocation()));
	TestTrue(TEXT("B follows new spawn rotation without scaling"),
		Enemy->GetPatrolPointB().Equals(SpawnTransform.TransformPositionNoScale(FVector(1000.0, 0.0, 0.0))));
	FIntProperty* Lease = FindFProperty<FIntProperty>(AEnemyBase::StaticClass(), TEXT("PoolLeaseSerial"));
	if (!TestNotNull(TEXT("Pool lease reflection"), Lease)) return false;
	FEnemyPatrolTaskInstanceData Session = MakePatrolData(Enemy);
	Session.Action = EEnemyPatrolAction::Session;
	EnemyPatrolTesting::Enter(Session);
	UEnemyRoomSubsystem* Rooms = Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
	Rooms->ReservePatrolDestination(Enemy, Enemy->GetPatrolPointB());
	Lease->SetPropertyValue_InContainer(Enemy, Session.Lease + 1);
	TestEqual(TEXT("Old session cannot continue across pool lease"), EnemyPatrolTesting::Tick(Session), EStateTreeRunStatus::Failed);
	FEnemyPatrolTaskInstanceData Selection = MakePatrolData(Enemy);
	Selection.NextPointIndex = 0;
	TestEqual(TEXT("New lease starts from B"), EnemyPatrolTesting::Enter(Selection), EStateTreeRunStatus::Succeeded);
	TestTrue(TEXT("New lease does not reuse the old spawn target"), Selection.Destination.Equals(Enemy->GetPatrolPointB()));
	return true;
}

#if WITH_EDITOR
namespace
{
struct FEnemyCombatIntegrationWorld : FEnemySearchIntegrationWorld
{
	static constexpr float Step = 1.0f / 60.0f;
	FAutomationTestBase& Test;
	AEnemyBase* Enemy = nullptr;
	AShooterCharacter* Target = nullptr;
	AEnemyAIController* Controller = nullptr;
	UEnemyStateTreeComponent* Tree = nullptr;
	EnemyCombatTesting::FSnapshot Snapshot;
	FString LastStates;
	FString Scenario;
	bool bSawFiring = false;

	FEnemyCombatIntegrationWorld(FAutomationTestBase& InTest, const FString& InScenario)
		: FEnemySearchIntegrationWorld(true), Test(InTest), Scenario(InScenario) {}

	~FEnemyCombatIntegrationWorld()
	{
		if (Tree) Tree->StopLogic(TEXT("Combat integration test complete"));
	}

	bool Initialize(bool bMelee, const FVector& TargetOffset, bool bInitiallyVisible = true, bool bStartInCombat = true)
	{
		if (!Test.TestTrue(TEXT("Combat room created"), CreateRoom(FVector(6000, 6000, 2500)))) return false;
		if (!Test.TestNotNull(TEXT("Combat floor created"), SpawnBlockingBox(
			FVector(0, 0, -40), FVector(6000, 6000, 40)))) return false;
		UClass* EnemyClass = LoadClass<AEnemyBase>(nullptr, bMelee
			? TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Explosion.BP_VECDrone_Explosion_C")
			: TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"));
		UClass* TargetClass = LoadClass<AShooterCharacter>(nullptr,
			TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
		if (!Test.TestNotNull(TEXT("Combat BP loads"), EnemyClass)
			|| !Test.TestNotNull(TEXT("Shooter BP loads"), TargetClass)) return false;
		const AEnemyBase* Defaults = EnemyClass->GetDefaultObject<AEnemyBase>();
		const float Height = Defaults->FlightHeightSettings.bEnabled ? Defaults->GetPreferredFlightHeight() : 300.0f;
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Enemy = World->SpawnActor<AEnemyBase>(EnemyClass, FTransform(FVector(0, 0, Height)), Spawn);
		if (!Test.TestNotNull(TEXT("Combat enemy spawns"), Enemy)) return false;
		if (bMelee && !Test.TestNotNull(TEXT("Melee BP is a self-destruct drone"), Cast<ASelfDestructDrone>(Enemy))) return false;
		Tree = Cast<UEnemyStateTreeComponent>(Enemy->GetStateTreeComponent());
		Controller = Cast<AEnemyAIController>(Enemy->GetController());
		if (!Test.TestNotNull(TEXT("Actual enemy StateTree exists"), Tree)
			|| !Test.TestNotNull(TEXT("Actual AI controller exists"), Controller)) return false;
		Tree->StopLogic(TEXT("Controlled combat setup"));
		Controller->SetEnemyPerceptionEnabled(false);
		if (Enemy->FlightHeightSettings.bEnabled)
			Enemy->SetActorLocation(FVector(0, 0, Enemy->GetPreferredFlightHeight()));
		Enemy->GetRoomTagComp()->AssignDefaultRoomTag(SearchRoomTag);
		Target = World->SpawnActor<AShooterCharacter>(TargetClass,
			FTransform(Enemy->GetActorLocation() + TargetOffset), Spawn);
		if (!Test.TestNotNull(TEXT("Controlled shooter target spawns"), Target)) return false;
		// 감지 입력과 타깃 이동만 통제한다. 드론의 EQS, 이동, 공격, 전이는 실제 구현을 실행한다.
		Target->SetActorEnableCollision(false);
		Target->SetCanBeDamaged(false);
		Target->GetCharacterMovement()->DisableMovement();
		Target->SetActorTickEnabled(false);
		if (bStartInCombat) Enemy->EnterCombatFromRoom(Target->GetActorLocation(), false);
		Controller->SetEnemyPerceptionEnabled(false);
		Enemy->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
		if (bInitiallyVisible) SetSight(true);
		return Test.TestEqual(TEXT("Initial perception matches the controlled sight input"),
			Controller->GetPreferredVisibleTarget(), bInitiallyVisible ? static_cast<AActor*>(Target) : nullptr);
	}

	void SetSight(bool bVisible)
	{
		UAIPerceptionComponent* Perception = Controller->GetPerceptionComponent();
		if (!Test.TestNotNull(TEXT("Combat perception component exists"), Perception)) return;
		FAIStimulus Stimulus(*GetDefault<UAISense_Sight>(), 1.0f,
			Target->GetActorLocation(), Enemy->GetActorLocation());
		if (!bVisible) Stimulus.MarkNoLongerSensed();
		Perception->RegisterStimulus(Target, Stimulus);
		Perception->ProcessStimuli();
	}

	void Start()
	{
		Tree->StartLogic();
		Observe();
	}

	void Tick(float DeltaTime) const
	{
		if (IsValid(Controller) && IsValid(Enemy) && IsValid(Target) && Controller->IsTargetDirectlyVisible(Target))
		{
			// 이 fixture는 감지/Actor Tick을 통제한다. 시야 캐시만으로 사격을 허용하지 않도록
			// 실제 관측자 원점에서 TP 본을 검사하고 완료된 포즈로 관측을 갱신한다.
			FCanBeSeenFromContext Context;
			Context.IgnoreActor = Enemy;
			Context.ObserverLocation = Enemy->GetPawnViewLocation();
			FVector Seen;
			int32 Sync = 0, Async = 0;
			float Strength = 0.0f;
			Target->CanBeSeenFrom(Context, Seen, Sync, Async, Strength, nullptr, nullptr);
			Target->GetMesh()->RefreshBoneTransforms();
			Target->CanBeSeenFrom(Context, Seen, Sync, Async, Strength, nullptr, nullptr);
			// Controller 회전을 멈춘 테스트에서도 관측 좌표 갱신은 따로 진행한다. 기존 갱신 주기는 지킨다.
			Enemy->RefreshCombatAimPoint();
		}
		FEnemySearchIntegrationWorld::Tick(DeltaTime);
	}

	void Observe()
	{
		// Snapshot의 인스턴스 뷰는 다음 Tick까지만 유효하다. 비교할 값은 Tick 전에 복사한다.
		Snapshot = EnemyCombatTesting::ReadComponent(*Tree);
		bSawFiring |= Enemy->GetAttackPhase() == EEnemyAttackPhase::Firing;
		const FString States = Snapshot.Describe();
		if (States == LastStates) return;
		Test.AddInfo(FString::Printf(TEXT("[CombatWorld] Scenario=%s Time=%.3f States=%s Location=%s D2D=%.1f D3D=%.1f Attack=%s"),
			*Scenario, World->GetTimeSeconds(), *States, *Enemy->GetActorLocation().ToCompactString(),
			FVector::Dist2D(Enemy->GetActorLocation(), Target->GetActorLocation()),
			FVector::Dist(Enemy->GetActorLocation(), Target->GetActorLocation()), *UEnum::GetValueAsString(Enemy->GetAttackPhase())));
		if (const auto* Fly = Data<FEnemyFlyToLocationTask>())
			Test.AddInfo(FString::Printf(TEXT("[CombatMove] Destination=%s Path=%s Result=%s Waypoints=%d"),
				*Fly->Destination.ToCompactString(), *UEnum::GetValueAsString(Fly->PathStatus),
				*UEnum::GetValueAsString(Fly->MoveResult), Fly->PathWaypoints.Num()));
		LastStates = States;
	}

	bool Advance()
	{
		Tick(Step);
		// 자동화의 동기 프레임 루프에서는 에디터 외부 Tick이 돌지 않는다. 실제 비동기 EQS도 프레임마다 진행한다.
		if (UEnvQueryManager* Queries = UEnvQueryManager::GetCurrent(World)) Queries->Tick(Step);
		Observe();
		return !Enemy->IsDead() && Tree->GetStateTreeRunStatus() == EStateTreeRunStatus::Running;
	}

	template<typename TaskType>
	const typename TaskType::FInstanceDataType* Data() const
	{
		const auto* Task = Snapshot.FindTask<TaskType>();
		return Task ? Task->Data.template GetPtr<const typename TaskType::FInstanceDataType>() : nullptr;
	}

	template<typename TaskType>
	bool WaitForTask(int32 MaxFrames = 900)
	{
		for (int32 Frame = 0; Frame < MaxFrames; ++Frame)
		{
			if (Data<TaskType>()) return true;
			if (!Advance()) break;
		}
		Test.AddError(FString::Printf(TEXT("[CombatWorld] %s: task %s not reached. States=%s"),
			*Scenario, *TaskType::StaticStruct()->GetName(), *Snapshot.Describe()));
		return false;
	}
};

bool PrepareReusedPoolCombat(FEnemyCombatIntegrationWorld& Fixture, bool bKeepAutomaticStart = false)
{
	UEnemyPoolSubsystem* Pool = Fixture.World->GetSubsystem<UEnemyPoolSubsystem>();
	if (!Fixture.Test.TestNotNull(TEXT("Actual pool subsystem"), Pool)) return false;
	UClass* Class = Fixture.Enemy->GetClass();
	const FTransform SpawnTransform = Fixture.Enemy->GetActorTransform();
	Fixture.Tree->StopLogic(TEXT("Replace unmanaged setup actor with a pooled lease"));
	Fixture.Enemy->Destroy();
	Fixture.Tree = nullptr;
	Fixture.Controller = nullptr;
	UEnemyPoolDefinition* Definition = NewObject<UEnemyPoolDefinition>(Fixture.World);
	FEnemyPoolEntry& Entry = Definition->Entries.AddDefaulted_GetRef();
	Entry.EnemyClass = Class;
	Entry.PrewarmCount = Entry.MaxCount = 1;
	if (!Fixture.Test.TestTrue(TEXT("Production BP pool prewarms"), Pool->PrewarmPool(Definition))) return false;
	FEnemyPoolLeaseContext Lease;
	Lease.RoomTag = Fixture.SearchRoomTag;
	Lease.GameplayGeneration = 3100;
	AEnemyBase* First = Pool->LeaseEnemy(Class, SpawnTransform, Lease);
	if (!Fixture.Test.TestNotNull(TEXT("First real pooled lease"), First)) return false;
	First->SetPoolPresentationAutoCompleteForTesting(false);
	const int32 OldSerial = First->GetPoolLeaseSerial();
	First->CompletePoolSpawnPresentation(Lease.GameplayGeneration, OldSerial);
	First->SimulateDeathPresentationForPoolTesting();
	if (!Fixture.Test.TestTrue(TEXT("First lease returns after disabling its capsule"), Pool->ReturnEnemy(First, Lease.GameplayGeneration, OldSerial))) return false;
	++Lease.GameplayGeneration;
	Fixture.Enemy = Pool->LeaseEnemy(Class, SpawnTransform, Lease);
	if (!Fixture.Test.TestTrue(TEXT("Second lease reuses the same actor"), Fixture.Enemy == First)) return false;
	Fixture.Enemy->CompletePoolSpawnPresentation(Lease.GameplayGeneration - 1, OldSerial);
	for (int32 Frame = 0; Frame < 15; ++Frame) Fixture.Tick(Fixture.Step);
	Fixture.Test.TestEqual(TEXT("Stale callback and delayed ticks preserve presentation"), Fixture.Enemy->GetEnemyPoolState(), EEnemyPoolState::SpawnPresentation);
	Fixture.Test.TestFalse(TEXT("No charge while the pooled capsule is intentionally inactive"), CastChecked<ASelfDestructDrone>(Fixture.Enemy)->IsSelfDestructCommitted());
	Fixture.Enemy->CompletePoolSpawnPresentation(Lease.GameplayGeneration, Fixture.Enemy->GetPoolLeaseSerial());
	Fixture.Tree = Cast<UEnemyStateTreeComponent>(Fixture.Enemy->GetStateTreeComponent());
	Fixture.Controller = Cast<AEnemyAIController>(Fixture.Enemy->GetController());
	if (!Fixture.Test.TestNotNull(TEXT("Reused StateTree"), Fixture.Tree)
		|| !Fixture.Test.TestNotNull(TEXT("Reused AI controller"), Fixture.Controller)) return false;
	if (!bKeepAutomaticStart) Fixture.Tree->StopLogic(TEXT("Set up deterministic sight before running the actual pooled tree"));
	Fixture.Controller->SetEnemyPerceptionEnabled(false);
	Fixture.Enemy->EnterCombatFromRoom(Fixture.Target->GetActorLocation(), false);
	Fixture.SetSight(true);
	Fixture.Test.TestTrue(TEXT("Reused capsule collision restored before StateTree charge"), Fixture.Enemy->GetActorEnableCollision() && Fixture.Enemy->GetCapsuleComponent()->IsQueryCollisionEnabled());
	Fixture.Test.AddInfo(FString::Printf(TEXT("[PooledCombat] Generation=%d Lease=%d Pool=%d CapsuleCollision=%d"),
		Fixture.Enemy->GetPoolGameplayGeneration(), Fixture.Enemy->GetPoolLeaseSerial(), int32(Fixture.Enemy->GetEnemyPoolState()), int32(Fixture.Enemy->GetCapsuleComponent()->GetCollisionEnabled())));
	return true;
}

bool CheckGunMovingFire(FEnemyCombatIntegrationWorld& Fixture)
{
	ARangedWeaponBase* Weapon = Fixture.Enemy->GetCurrentWeapon();
	if (!Fixture.Test.TestNotNull(TEXT("Real gun is equipped"), Weapon)) return false;
	FBoolProperty* InfiniteAmmo = FindFProperty<FBoolProperty>(Weapon->GetClass(), TEXT("bInfiniteAmmo"));
	FFloatProperty* Range = FindFProperty<FFloatProperty>(Weapon->GetClass(), TEXT("EffectiveRange"));
	if (!Fixture.Test.TestNotNull(TEXT("Ammo mode is inspectable"), InfiniteAmmo)
		|| !Fixture.Test.TestNotNull(TEXT("Attack range is inspectable"), Range)) return false;
	// 접근 이동 구간을 충분히 남긴 채 사격 가능 조건만 고정한다. Firing 표시 대신 실제 탄약 소비를 검사한다.
	InfiniteAmmo->SetPropertyValue_InContainer(Weapon, false);
	Range->SetPropertyValue_InContainer(Weapon, 5000.0f);
	int32 InitialAmmo = Weapon->GetCurrentAmmo();
	const bool bRestoreSharedInput = !Fixture.Enemy->IsPlayerCurrentlyVisible();
	// 공유는 접근 단서만 제공한다. 실제 첫 탄은 자신의 직접 시야가 유지되는 동안 검증한다.
	if (bRestoreSharedInput)
	{
		Fixture.SetSight(true);
	}
	Fixture.Start();
	for (int32 Frame = 0; Frame < 180; ++Frame)
	{
		if (!Fixture.Advance())
		{
			break;
		}
		if (Weapon->GetCurrentAmmo() >= InitialAmmo)
		{
			continue;
		}
		const auto* Fly = Fixture.Data<FEnemyFlyToLocationTask>();
		if (!Fixture.Test.TestTrue(TEXT("Actual first shot happens before approach arrival"),
			Fly && Fixture.Enemy->GetVelocity().SizeSquared() > 1.0f
			&& FVector::Dist(Fixture.Enemy->GetActorLocation(), Fly->Destination) > 100.0f))
		{
			return false;
		}
		if (bRestoreSharedInput)
		{
			const int32 DirectAmmo = Weapon->GetCurrentAmmo();
			Fixture.SetSight(false);
			for (int32 HiddenFrame = 0; HiddenFrame < 3; ++HiddenFrame)
			{
				if (!Fixture.Advance())
				{
					return false;
				}
				if (!Fixture.Test.TestEqual(TEXT("Shared contact retains tracking without granting another hidden shot"),
					Fixture.Enemy->GetTrackedTarget(), static_cast<AActor*>(Fixture.Target))
					|| !Fixture.Test.TestEqual(TEXT("Losing own sight immediately prevents new ammunition consumption"),
						Weapon->GetCurrentAmmo(), DirectAmmo))
				{
					return false;
				}
			}
		}
		return true;
	}
	Fixture.Test.AddError(TEXT("No actual gun shot during the live approach movement"));
	return false;
}

bool CheckCombatDistanceBranch(FEnemyCombatIntegrationWorld& Fixture, float Distance, bool bMelee)
{
	Fixture.Start();
	// 첫 EQS를 이동 전에 관측한다. 이동 후 거리가 바뀐 결과를 경계 조건 성공으로 오인하지 않는다.
	if (bMelee && Distance <= 500)
	{
		if (!Fixture.WaitForTask<FEnemyChargeLastDirectionTask>(2)) return false;
		return Fixture.Test.TestTrue(TEXT("At 5m the real melee tree commits charge"),
			CastChecked<ASelfDestructDrone>(Fixture.Enemy)->IsSelfDestructCommitted());
	}
	if (!Fixture.WaitForTask<FEnemyRunEnvQueryTask>(2)) return false;
	const auto* Data = Fixture.Data<FEnemyRunEnvQueryTask>();
	Fixture.Test.TestEqual(TEXT("First query is combat, not search"), Data->SearchMode, EEnemySearchQueryMode::None);
	Fixture.Test.TestEqual(TEXT("Approach is selected only above 20m"), Data->bValidateReachability, !bMelee && Distance > 2000);
	Fixture.Test.TestEqual(TEXT("Orbit is selected above 10m through 20m"), Data->bValidateOrbitPath, !bMelee && Distance > 1000 && Distance <= 2000);
	Fixture.Test.TestEqual(TEXT("Retreat includes the 10m boundary"), Data->bValidateRetreatPath, !bMelee && Distance <= 1000);
	Fixture.Test.TestEqual(TEXT("Melee outside 5m keeps chasing"), Data->bValidateChasePath, bMelee);
	return true;
}

bool CheckRetreatArrival(FEnemyCombatIntegrationWorld& Fixture)
{
	Fixture.Start();
	if (!Fixture.WaitForTask<FEnemyFlyToLocationTask>()) return false;
	const auto* Fly = Fixture.Data<FEnemyFlyToLocationTask>();
	if (!Fixture.Test.TestTrue(TEXT("Retreat Fly runs with its half-space constraint"), Fly->bConstrainRetreatPath)) return false;
	const FVector Destination = Fly->Destination;
	const float Acceptance = Fly->AcceptanceRadius;
	bool bCrossedBoundary = false;
	bool bArrived = false;
	for (int32 Frame = 0; Frame < 900; ++Frame)
	{
		const float Remaining = FVector::Dist(Fixture.Enemy->GetActorLocation(), Destination);
		bArrived |= Remaining <= Acceptance + 5.0f;
		Fly = Fixture.Data<FEnemyFlyToLocationTask>();
		if (!Fly || !Fly->bConstrainRetreatPath || !Fly->Destination.Equals(Destination, 1)) break;
		bCrossedBoundary |= FVector::Dist2D(Fixture.Enemy->GetActorLocation(), Fixture.Target->GetActorLocation()) > 1000
			&& Remaining > Acceptance + 10;
		if (!Fixture.Advance()) break;
	}
	Fixture.Test.TestTrue(TEXT("Retreat crosses 10m while still moving to its original destination"), bCrossedBoundary);
	return Fixture.Test.TestTrue(TEXT("Retreat does not rebranch before arrival"), bArrived);
}

bool CheckOrbitHold(FEnemyCombatIntegrationWorld& Fixture)
{
	Fixture.Start();
	if (!Fixture.WaitForTask<FEnemyFlyToLocationTask>()) return false;
	if (!Fixture.Test.TestTrue(TEXT("Orbit follows validated arc waypoints"),
		Fixture.Data<FEnemyFlyToLocationTask>()->bUsePlannedPath)) return false;
	const FVector Start = Fixture.Enemy->GetActorLocation();
	if (!Fixture.WaitForTask<FEnemyStationaryFireTask>()) return false;
	const auto* Hold = Fixture.Data<FEnemyStationaryFireTask>();
	const float Duration = Hold->SelectedDuration;
	const float Elapsed = Hold->Elapsed;
	const float EnterTime = Fixture.World->GetTimeSeconds();
	const FVector HoldLocation = Fixture.Enemy->GetActorLocation();
	Fixture.Test.TestTrue(TEXT("Orbit actually moves before holding"), FVector::Dist(Start, HoldLocation) > 50);
	Fixture.Test.TestTrue(TEXT("Hold selects a duration within 0.5..1 seconds"), Duration >= 0.5f && Duration <= 1.0f);
	Fixture.Test.TestEqual(TEXT("Hold retains the visible shooter"), Hold->CapturedTarget.Get(), static_cast<AActor*>(Fixture.Target));
	bool bStopped = true;
	for (int32 Frame = 0; Frame < 120 && Fixture.Data<FEnemyStationaryFireTask>(); ++Frame)
	{
		bStopped &= Fixture.Enemy->GetActorLocation().Equals(HoldLocation, 10);
		if (!Fixture.Advance()) break;
	}
	const float HeldTime = Fixture.World->GetTimeSeconds() - EnterTime + Elapsed;
	Fixture.Test.TestTrue(TEXT("Hold does not drift"), bStopped);
	Fixture.Test.TestTrue(TEXT("Hold completes at its selected duration"),
		!Fixture.Data<FEnemyStationaryFireTask>() && FMath::Abs(HeldTime - Duration) <= Fixture.Step * 3);
	Fixture.Test.TestTrue(TEXT("Parallel fire cycle actually fires during the orbit/hold sequence"), Fixture.bSawFiring);
	return Fixture.WaitForTask<FEnemyFlyToLocationTask>();
}

bool CheckCombatReselection(FEnemyCombatIntegrationWorld& Fixture)
{
	Fixture.Start();
	if (!Fixture.WaitForTask<FEnemyFlyToLocationTask>()) return false;
	const auto* Guard = Fixture.Data<FEnemyCombatDestinationGuardTask>();
	if (!Fixture.Test.TestNotNull(TEXT("Moving combat has an actual destination guard"), Guard)) return false;
	const FVector Captured = Guard->SelectedTargetLocation;
	const float Threshold = Guard->TargetChangeDistance;
	Fixture.Target->SetActorLocation(Captured + FVector(0, Threshold * 0.1f, 0));
	if (!Fixture.Advance()) return false;
	Guard = Fixture.Data<FEnemyCombatDestinationGuardTask>();
	if (!Fixture.Test.TestTrue(TEXT("Small target movement preserves the captured destination"),
		Guard && !Guard->bShouldReselect && Guard->SelectedTargetLocation.Equals(Captured, 1))) return false;
	const FVector Changed = Captured + FVector(0, Threshold * 2, 0);
	Fixture.Target->SetActorLocation(Changed);
	for (int32 Frame = 0; Frame < 180; ++Frame)
	{
		if (!Fixture.Advance()) break;
		Guard = Fixture.Data<FEnemyCombatDestinationGuardTask>();
		if (Guard && Guard->SelectedTargetLocation.Equals(Changed, 1)) return true;
	}
	Fixture.Test.AddError(TEXT("Large target movement did not reach a newly selected Fly destination within 3 seconds"));
	return false;
}

bool CheckOrbitObstacles(FEnemyCombatIntegrationWorld& Fixture, bool bBothBlocked)
{
	TArray<FEnemyOrbitCandidate> Candidates;
	const FVector Start = Fixture.Enemy->GetActorLocation();
	EnemyOrbit::BuildCandidates(Start, Fixture.Target->GetActorLocation(), 400,
		Fixture.Enemy->PrefersCombatLeft(), Candidates);
	if (!Fixture.Test.TestTrue(TEXT("Orbit geometry has a preferred side"), !Candidates.IsEmpty())) return false;
	const double PreferredSign = FMath::Sign(Candidates[0].Destination.Y - Start.Y);
	const double InnerEdge = Fixture.Enemy->GetCapsuleComponent()->GetScaledCapsuleRadius() + 15;
	// 시작 캡슐과 시선은 비워 두고, 가장 짧은 1.5m 호까지 막는다. 높이를 바꿔도 벽을 넘지 못한다.
	if (!Fixture.SpawnBlockingBox(Start + FVector(0, PreferredSign * (InnerEdge + 300), 0), FVector(600, 300, 2500))) return false;
	if (bBothBlocked && !Fixture.SpawnBlockingBox(Start - FVector(0, PreferredSign * (InnerEdge + 300), 0), FVector(600, 300, 2500))) return false;
	Fixture.Start();
	for (int32 Frame = 0; Frame < 360; ++Frame)
	{
		const auto* Fly = Fixture.Data<FEnemyFlyToLocationTask>();
		if (Fly && Fly->bUsePlannedPath)
		{
			if (bBothBlocked)
			{
				Fixture.Test.AddError(TEXT("Both orbit sides are blocked but the tree entered orbit Fly"));
				return false;
			}
			return Fixture.Test.TestTrue(TEXT("One blocked side chooses the opposite arc, not precision fire"),
				(Fly->Destination.Y - Start.Y) * PreferredSign < 0);
		}
		if (Fixture.Snapshot.HasState(TEXT("PrecisionFire")))
		{
			Fixture.Test.TestTrue(TEXT("Precision fire requires both physical sides to be blocked"), bBothBlocked);
			return Fixture.Test.TestTrue(TEXT("Blocked orbit does not move"), Fixture.Enemy->GetActorLocation().Equals(Start, 10));
		}
		if (!Fixture.Advance()) break;
	}
	Fixture.Test.AddError(TEXT("Orbit obstacle scenario reached neither opposite-side Fly nor PrecisionFire"));
	return false;
}

bool CheckCombatSightLoss(FEnemyCombatIntegrationWorld& Fixture)
{
	Fixture.Start();
	if (!Fixture.WaitForTask<FEnemyFlyToLocationTask>()) return false;
	Fixture.SetSight(false);
	if (!Fixture.Advance()) return false;
	const auto* BeforeReacquire = Fixture.Data<FEnemyFlyToLocationTask>();
	if (!Fixture.Test.TestNotNull(TEXT("Sight loss retains the active flight during grace"), BeforeReacquire)) return false;
	const TArray<FVector> Route = BeforeReacquire->PathWaypoints;
	const FVector Destination = BeforeReacquire->Destination;
	Fixture.SetSight(true);
	if (!Fixture.Advance()) return false;
	const auto* AfterReacquire = Fixture.Data<FEnemyFlyToLocationTask>();
	if (!Fixture.Test.TestNotNull(TEXT("Same-player reacquire keeps the active flight"), AfterReacquire)) return false;
	Fixture.Test.TestTrue(TEXT("Reacquire does not replace the valid route"), AfterReacquire->PathWaypoints == Route);
	Fixture.Test.TestTrue(TEXT("Reacquire preserves the movement destination"), AfterReacquire->Destination.Equals(Destination));
	// 자폭형은 유예 중 5m 안에 도달하면 정상적으로 돌진을 확정한다.
	// 이 검사는 돌진이 아닌 유예 만료 -> 수색을 다루므로, 두 번째 상실 전에 충분한 거리를 둔다.
	const bool bMelee = Fixture.Enemy->IsA<ASelfDestructDrone>();
	Fixture.Target->AddActorWorldOffset(FVector(bMelee ? 2500 : 0, 20, 10));
	Fixture.SetSight(false);
	const float LostAt = Fixture.World->GetTimeSeconds();
	for (int32 Frame = 0; Frame < 240; ++Frame)
	{
		if (!Fixture.Advance()) break;
		const float SinceLoss = Fixture.World->GetTimeSeconds() - LostAt;
		const float Grace = Fixture.Enemy->GetRuntimeStat().TargetTrackingGraceSeconds;
		if (SinceLoss < Grace - Fixture.Step)
		{
			Fixture.Test.TestFalse(TEXT("Grace does not enter search"), Fixture.Enemy->GetSearchEpisode().bActive);
			Fixture.Test.TestEqual(TEXT("Grace retains the same moving target"), Fixture.Enemy->GetTrackedTarget(), static_cast<AActor*>(Fixture.Target));
		}
		if (Fixture.Snapshot.HasState(TEXT("SelectAnchor")) || Fixture.Snapshot.HasState(TEXT("MoveAnchor")))
		{
			Fixture.Test.TestTrue(TEXT("Search starts after the configured tracking grace"), SinceLoss >= Grace - Fixture.Step);
			Fixture.Test.TestTrue(TEXT("Tracking expiry skips the additional two-second LossWait"), SinceLoss < Grace + 0.5f);
			return Fixture.Test.TestEqual(TEXT("Lost target is no longer fired upon"), Fixture.Enemy->GetAttackPhase(), EEnemyAttackPhase::Idle);
		}
	}
	Fixture.Test.AddError(TEXT("Sight loss did not finish tracking grace and enter search without an additional wait"));
	return false;
}

bool CheckCombatMovementStun(FEnemyCombatIntegrationWorld& Fixture)
{
	Fixture.Start();
	if (!Fixture.WaitForTask<FEnemyFlyToLocationTask>()) return false;
	Fixture.Enemy->EnterStun();
	Fixture.Advance();
	Fixture.Test.TestEqual(TEXT("Combat movement yields to Stun"), Fixture.Enemy->GetCombatState(), EEnemyCombatState::Stun);
	Fixture.Test.TestTrue(TEXT("Stun exits combat Fly"), !Fixture.Data<FEnemyFlyToLocationTask>());
	return Fixture.Test.TestTrue(TEXT("Stun leaves no movement velocity"), Fixture.Enemy->GetVelocity().IsNearlyZero(1));
}

bool CheckMeleeChargeRejection(FEnemyCombatIntegrationWorld& Fixture, bool bBlocked)
{
	if (bBlocked && !Fixture.SpawnBlockingBox(Fixture.Enemy->GetActorLocation() + FVector(200, 0, 0),
		FVector(30, 600, 1000))) return false;
	const EEnemyChargeEligibility Expected = bBlocked ? EEnemyChargeEligibility::BlockedPath : EEnemyChargeEligibility::OutOfRange;
	Fixture.Test.TestEqual(TEXT("Entry geometry independently rejects charge"), EnemyCharge::Assess(
		CastChecked<ASelfDestructDrone>(Fixture.Enemy), Fixture.Controller, Fixture.Target, 500, 5), Expected);
	Fixture.Start();
	if (!Fixture.WaitForTask<FEnemyRunEnvQueryTask>(2)) return false;
	Fixture.Test.TestTrue(TEXT("Rejected charge keeps the actual chase branch"), Fixture.Data<FEnemyRunEnvQueryTask>()->bValidateChasePath);
	return Fixture.Test.TestFalse(TEXT("Rejected entry never commits charge"),
		CastChecked<ASelfDestructDrone>(Fixture.Enemy)->IsSelfDestructCommitted());
}

bool CheckMeleeUnreachable(FEnemyCombatIntegrationWorld& Fixture)
{
	if (!Fixture.SpawnBlockingBox(FVector(700, 0, 0), FVector(50, 6000, 2500))) return false;
	Fixture.Start();
	int32 Attempts = 0;
	double LastStarted = -1;
	bool bSawRetry = false;
	bool bSearchStarted = false;
	const FVector Start = Fixture.Enemy->GetActorLocation();
	for (int32 Frame = 0; Frame < 480; ++Frame)
	{
		const auto* Query = Fixture.Data<FEnemyRunEnvQueryTask>();
		if (Query && Query->bActive && Query->bValidateChasePath && Query->QueryStartedAt != LastStarted)
		{
			LastStarted = Query->QueryStartedAt;
			++Attempts;
		}
		bSawRetry |= Fixture.Snapshot.HasState(TEXT("RetryWait"));
		bSearchStarted |= Fixture.Enemy->GetSearchEpisode().bActive;
		if (!Fixture.Advance()) break;
	}
	Fixture.Test.AddInfo(FString::Printf(TEXT("[CombatWorld] Unreachable Attempts=%d Retry=%d Search=%d"), Attempts, bSawRetry, bSearchStarted));
	Fixture.Test.TestTrue(TEXT("Visible unreachable target retries, without a per-frame request loop"), Attempts >= 2 && Attempts <= 20 && bSawRetry);
	Fixture.Test.TestFalse(TEXT("Path failure alone never enters search or LocalGuard"), bSearchStarted || Fixture.Enemy->GetSearchEpisode().bLocalGuard);
	Fixture.Test.TestTrue(TEXT("Failed query does not move toward an invalid destination"), Fixture.Enemy->GetActorLocation().Equals(Start, 10));
	return Fixture.Test.TestTrue(TEXT("Unreachable chase remains running"), Fixture.Tree->GetStateTreeRunStatus() == EStateTreeRunStatus::Running);
}

bool CheckChargeWallTimeout(FEnemyCombatIntegrationWorld& Fixture)
{
	Fixture.Start();
	if (!Fixture.WaitForTask<FEnemyChargeLastDirectionTask>()) return false;
	ASelfDestructDrone* Drone = CastChecked<ASelfDestructDrone>(Fixture.Enemy);
	const auto* Charge = Fixture.Data<FEnemyChargeLastDirectionTask>();
	UExplosionComponent* Explosion = Drone->GetExplosionComponent();
	if (!Fixture.Test.TestTrue(TEXT("Real charge commits before the wall exists"), Drone->IsSelfDestructCommitted())
		|| !Fixture.Test.TestNotNull(TEXT("Charge explosion component exists"), Explosion)) return false;
	const float Duration = Charge->TelegraphDuration;
	const double StartedAt = Fixture.World->GetTimeSeconds() - Drone->GetCommittedChargeElapsedTime();
	const double Deadline = StartedAt + Duration;
	if (!Fixture.Test.TestTrue(TEXT("Charge window permits observing wall contact"), Duration > 0.5f)) return false;

	// 진입 때의 직선 경로 검사를 통과한 뒤 벽을 만든다. 이후 접촉은 실제 CharacterMovement Sweep이 처리한다.
	const FVector Start = Drone->GetActorLocation();
	const FVector Direction = Drone->GetActorForwardVector();
	if (!Fixture.Test.TestTrue(TEXT("Controlled charge heads along the wall normal"), Direction.Equals(FVector::ForwardVector, 0.001f))) return false;
	const UCapsuleComponent* Capsule = Drone->GetCapsuleComponent();
	const double CapsuleExtentX = Capsule->CalcBounds(Capsule->GetComponentTransform()).BoxExtent.X;
	const double ContactCenterX = Start.X + 100.0;
	if (!Fixture.Test.TestNotNull(TEXT("Wall appears after commitment"), Fixture.SpawnBlockingBox(
		FVector(ContactCenterX + CapsuleExtentX + 20.0, Start.Y, Start.Z), FVector(20, 500, 500)))) return false;

	bool bTouchedWall = false;
	bool bClockPreserved = true;
	bool bCommitPreserved = true;
	bool bStayedBehindWall = true;
	int32 ContactFrames = 0;
	const int32 MaxFrames = FMath::CeilToInt(Duration / Fixture.Step) + 6;
	for (int32 Frame = 0; Frame < MaxFrames; ++Frame)
	{
		const bool bRunning = Fixture.Advance();
		const double Now = Fixture.World->GetTimeSeconds();
		if (Drone->IsDead())
		{
			Fixture.Test.TestTrue(TEXT("Wall contact cannot detonate before the original deadline"), Now + 0.001 >= Deadline);
			break;
		}
		bCommitPreserved &= Drone->IsSelfDestructCommitted();
		bClockPreserved &= FMath::IsNearlyEqual(
			static_cast<double>(Drone->GetCommittedChargeElapsedTime()), Now - StartedAt, 0.002);
		// 캡슐 중심이 벽 접촉 경계까지 실제 이동한 뒤 여러 프레임 머무는지 확인한다.
		// 벽 너머로 위치를 옮기거나 NotifyHit를 직접 호출하면 이 검증을 통과할 수 없다.
		const double CenterX = Drone->GetActorLocation().X;
		bStayedBehindWall &= CenterX <= ContactCenterX + 2.0;
		if (FMath::Abs(CenterX - ContactCenterX) <= 2.0)
		{
			bTouchedWall = true;
			++ContactFrames;
		}
		if (!bRunning) break;
	}
	Fixture.Test.TestTrue(TEXT("Actual charge movement reaches the wall"), bTouchedWall);
	Fixture.Test.TestTrue(TEXT("Charge survives repeated wall contact"), ContactFrames >= 3 && bCommitPreserved);
	Fixture.Test.TestTrue(TEXT("Blocking wall prevents the capsule crossing"), bStayedBehindWall);
	Fixture.Test.TestTrue(TEXT("Wall contact never restarts the committed clock"), bClockPreserved);
	Fixture.Test.TestTrue(TEXT("Original charge timeout still enters death"), Drone->IsDead());
	Fixture.Test.TestTrue(TEXT("Timeout uses the existing actual explosion path"), Explosion->HasDetonated());
	Fixture.Test.TestTrue(TEXT("Repeated wall contact cannot extend the original deadline"),
		Fixture.World->GetTimeSeconds() <= Deadline + Fixture.Step * 2.0 + 0.001);
	Fixture.Test.AddInfo(FString::Printf(TEXT("[ChargeWallTimeout] Duration=%.3f Elapsed=%.3f ContactFrames=%d"),
		Duration, Fixture.World->GetTimeSeconds() - StartedAt, ContactFrames));
	return !Fixture.Test.HasAnyErrors();
}

bool CheckScheduledChargeWall(FEnemyCombatIntegrationWorld& Fixture, const FString& Scenario, bool bStartTree = true)
{
	if (bStartTree) Fixture.Start();
	else Fixture.Observe(); // Keep the StateTree started by CombatActive without restarting it.
	if (!Fixture.WaitForTask<FEnemyChargeLastDirectionTask>()) return false;
	ASelfDestructDrone* Drone = CastChecked<ASelfDestructDrone>(Fixture.Enemy);
	const float Duration = Fixture.Data<FEnemyChargeLastDirectionTask>()->TelegraphDuration;
	const float DeltaTime = Scenario.EndsWith(TEXT("Hitch")) ? 0.25f
		: Scenario.EndsWith(TEXT("LowRate")) ? 1.0f / 15.0f : Fixture.Step;
	const FVector Start = Drone->GetActorLocation();
	const float Radius = Drone->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const bool bOverlapFront = Scenario.EndsWith(TEXT("OverlapFront"));
	const bool bOverlapBehind = Scenario.EndsWith(TEXT("OverlapBehind"));
	const bool bPhysicsBody = Scenario.EndsWith(TEXT("PhysicsBody"));
	const bool bRemoveCollision = Scenario.EndsWith(TEXT("Removed"));
	const bool bLateCollision = Scenario.EndsWith(TEXT("LateRegister"));
	const bool bMissingCollision = Scenario.EndsWith(TEXT("Missing"));
	const bool bExpectedCrossing = bOverlapBehind || bPhysicsBody || bRemoveCollision || bMissingCollision;
	const float HalfThickness = Scenario.EndsWith(TEXT("Thin")) ? 1.5f : 20.0f;
	const double WallX = bOverlapFront ? Start.X + 10.0 : bOverlapBehind ? Start.X - 10.0
		: Start.X + 100.0 + Radius + HalfThickness;
	AActor* Wall = Fixture.SpawnBlockingBox(FVector(WallX, Start.Y, Start.Z), FVector(HalfThickness, 500, 500));
	if (!Fixture.Test.TestNotNull(TEXT("Scheduled wall fixture"), Wall)) return false;
	UBoxComponent* Box = CastChecked<UBoxComponent>(Wall->GetRootComponent());
	if (bPhysicsBody) Box->SetCollisionObjectType(ECC_PhysicsBody);
	if (bLateCollision || bMissingCollision) Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	bool bTouched = false;
	bool bCrossed = false;
	bool bRemoved = false;
	bool bCollisionStayedActive = true;
	bool bSawChargeTask = false;
	double MaxX = Start.X;
	const int32 MaxFrames = FMath::CeilToInt(Duration / DeltaTime) + 4;
	for (int32 Frame = 0; Frame < MaxFrames; ++Frame)
	{
		if (bLateCollision && Frame == 2) Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		if (bRemoveCollision && bTouched && !bRemoved)
		{
			Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			bRemoved = true;
		}
		Fixture.Tick(DeltaTime);
		Fixture.Observe();
		// Death disables collision and may start return presentation. Only inspect
		// live charge frames; those runtime lifecycle changes are tested separately.
		if (Drone->IsDead()) break;
		const double X = Drone->GetActorLocation().X;
		MaxX = FMath::Max(MaxX, X);
		bCrossed |= X > WallX + HalfThickness + Radius;
		bTouched |= FMath::Abs(X - (WallX - HalfThickness - Radius)) <= 3.0;
		bCollisionStayedActive &= Drone->GetActorEnableCollision() && Drone->GetCapsuleComponent()->IsQueryCollisionEnabled();
		bSawChargeTask |= Fixture.Data<FEnemyChargeLastDirectionTask>() != nullptr;
		if (Frame < 3 || bCrossed || Frame % 15 == 0)
			Fixture.Test.AddInfo(FString::Printf(TEXT("[ChargeScheduled] Scenario=%s Frame=%d Time=%.3f Dt=%.4f NetMode=%d Pool=%d ActorCollision=%d CapsuleCollision=%d WallCollision=%d Committed=%d ChargeTask=%d X=%.2f WallX=%.2f Velocity=%s States=%s"),
				*Scenario, Frame, Fixture.World->GetTimeSeconds(), DeltaTime, int32(Fixture.World->GetNetMode()), int32(Drone->GetEnemyPoolState()),
				Drone->GetActorEnableCollision(), int32(Drone->GetCapsuleComponent()->GetCollisionEnabled()), int32(Box->GetCollisionEnabled()),
				Drone->IsSelfDestructCommitted(), Fixture.Data<FEnemyChargeLastDirectionTask>() != nullptr, X, WallX,
				*Drone->GetVelocity().ToCompactString(), *Fixture.Snapshot.Describe()));
	}
	Fixture.Test.TestTrue(TEXT("Real StateTree charge executed through world Tick"), bSawChargeTask);
	Fixture.Test.TestTrue(TEXT("Live drone retains its capsule collision"), bCollisionStayedActive);
	Fixture.Test.TestEqual(TEXT("World-scheduled crossing matches the controlled collision condition"), bCrossed, bExpectedCrossing);
	if (!bExpectedCrossing) Fixture.Test.TestTrue(TEXT("Blocking wall was reached"), bTouched);
	if (bRemoveCollision) Fixture.Test.TestTrue(TEXT("Wall removal happened after a real blocked contact"), bRemoved);
	Fixture.Test.TestTrue(TEXT("Scheduled charge reaches its original death path"), Drone->IsDead());
	Fixture.Test.TestTrue(TEXT("Actual explosion executes"), Drone->GetExplosionComponent()->HasDetonated());
	if (IsRunningDedicatedServer()) Fixture.Test.TestEqual(TEXT("Dedicated world with a real server driver"), Fixture.World->GetNetMode(), NM_DedicatedServer);
	Fixture.Test.AddInfo(FString::Printf(TEXT("[ChargeScheduledSummary] Scenario=%s Dt=%.4f MaxX=%.2f WallX=%.2f Touched=%d Crossed=%d ExpectedCrossing=%d"),
		*Scenario, DeltaTime, MaxX, WallX, bTouched, bCrossed, bExpectedCrossing));
	return !Fixture.Test.HasAnyErrors();
}

bool CheckCommittedCharge(FEnemyCombatIntegrationWorld& Fixture, const FString& Scenario)
{
	Fixture.Start();
	if (!Fixture.WaitForTask<FEnemyChargeLastDirectionTask>()) return false;
	ASelfDestructDrone* Drone = CastChecked<ASelfDestructDrone>(Fixture.Enemy);
	if (!Fixture.Test.TestTrue(TEXT("Real charge task commits before stimulus changes"), Drone->IsSelfDestructCommitted())) return false;
	const FVector Direction = Drone->GetActorForwardVector();
	const FVector Start = Drone->GetActorLocation();
	if (Scenario == TEXT("ChargeStun"))
	{
		Drone->EnterStun();
		Fixture.Advance();
		Fixture.Test.TestFalse(TEXT("Stun cancels committed charge"), Drone->IsSelfDestructCommitted());
		Fixture.Test.TestTrue(TEXT("Stun exits the actual charge task"), !Fixture.Data<FEnemyChargeLastDirectionTask>());
		Fixture.Test.TestTrue(TEXT("Stun stops charge movement"), Drone->GetVelocity().IsNearlyZero(1));
		return Fixture.Test.TestFalse(TEXT("Canceled charge does not detonate"), Drone->IsDead());
	}
	if (Scenario == TEXT("ChargeSightLoss")) Fixture.SetSight(false);
	else if (Scenario == TEXT("ChargeTargetSwitch"))
	{
		Fixture.SetSight(false);
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Fixture.Target = Fixture.World->SpawnActor<AShooterCharacter>(Fixture.Target->GetClass(),
			FTransform(Start + FVector(0, 1500, 0)), Spawn);
		if (!Fixture.Test.TestNotNull(TEXT("Replacement shooter spawns"), Fixture.Target)) return false;
		Fixture.Target->SetActorEnableCollision(false);
		Fixture.Target->SetCanBeDamaged(false);
		Fixture.Target->GetCharacterMovement()->DisableMovement();
		Fixture.Target->SetActorTickEnabled(false);
		Fixture.SetSight(true);
		if (!Fixture.Test.TestEqual(TEXT("Perception switches to the replacement target"),
			Fixture.Controller->GetPreferredVisibleTarget(), static_cast<AActor*>(Fixture.Target))) return false;
	}
	else Fixture.Target->SetActorLocation(Start + FVector(0, 1500, 0));
	bool bDirectionStable = true;
	bool bEnteredSearch = false;
	int32 CommittedFrames = 0;
	for (int32 Frame = 0; Frame < 240; ++Frame)
	{
		if (!Fixture.Advance()) break;
		if (!Drone->IsSelfDestructCommitted()) break;
		++CommittedFrames;
		bDirectionStable &= FVector::DotProduct(Direction, Drone->GetActorForwardVector()) > 0.999;
		bEnteredSearch |= Drone->GetSearchEpisode().bActive;
	}
	Fixture.Test.TestTrue(TEXT("Committed charge survives stimulus changes over multiple frames"), CommittedFrames > 2);
	Fixture.Test.TestTrue(TEXT("Committed direction does not track the moved or lost player"), bDirectionStable);
	Fixture.Test.TestFalse(TEXT("Committed sight loss cannot start search"), bEnteredSearch);
	Fixture.Test.TestTrue(TEXT("Committed charge physically moves"), FVector::Dist(Start, Drone->GetActorLocation()) > 50);
	return Fixture.Test.TestTrue(TEXT("Charge completes in detonation rather than silently canceling"), Drone->IsDead());
}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyCombatWorldTest, "Outlier.Enemy.Combat.Integration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

void FEnemyCombatWorldTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("Gun.Boundary999"), TEXT("Gun.Boundary1000"), TEXT("Gun.Boundary1001"),
		TEXT("Gun.Boundary1999"), TEXT("Gun.Boundary2000"), TEXT("Gun.Boundary2001"),
		TEXT("Gun.MovingFire"), TEXT("Gun.RetreatArrival"), TEXT("Gun.OrbitHold"), TEXT("Gun.Reselection"), TEXT("Gun.SightLoss"),
		TEXT("Gun.OrbitOneSideBlocked"), TEXT("Gun.OrbitBothSidesBlocked"), TEXT("Gun.MovementStun"),
		TEXT("Melee.Boundary500"), TEXT("Melee.Boundary501"), TEXT("Melee.Unreachable"), TEXT("Melee.SightLoss"),
		TEXT("Melee.SpatialRange"), TEXT("Melee.BlockedChargeEntry"), TEXT("Melee.MovementStun"),
		TEXT("Melee.ChargeSightLoss"), TEXT("Melee.ChargeTargetMove"), TEXT("Melee.ChargeTargetSwitch"), TEXT("Melee.ChargeStun"),
		TEXT("Melee.ChargeWallTimeout"), TEXT("Melee.ChargeScheduledWallHitch"), TEXT("Melee.ChargeScheduledWallLowRate"),
		TEXT("Melee.ChargeScheduledWallThin"), TEXT("Melee.ChargeScheduledWallLateRegister"), TEXT("Melee.ChargeScheduledWallOverlapFront"),
		TEXT("Melee.ChargeScheduledWallOverlapBehind"), TEXT("Melee.ChargeScheduledWallPhysicsBody"),
		TEXT("Melee.ChargeScheduledWallRemoved"), TEXT("Melee.ChargeScheduledWallMissing"),
		TEXT("Melee.PoolChargeScheduledWallThin"), TEXT("Melee.PoolChargeScheduledWallHitch"), TEXT("Melee.PoolChargeScheduledWallLateRegister"),
		TEXT("Melee.PoolAutoChargeWallThin"), TEXT("Melee.PoolAutoChargeWallHitch"), TEXT("Melee.PoolAutoChargeWallLateRegister")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyCombatWorldTest::RunTest(const FString& Parameters)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	FString Type, Scenario;
	if (!Parameters.Split(TEXT("."), &Type, &Scenario)) return false;
	const bool bMelee = Type == TEXT("Melee");
	float Distance = 1500;
	if (Scenario.StartsWith(TEXT("Boundary"))) Distance = FCString::Atof(*Scenario.Mid(8));
	else if (Scenario == TEXT("RetreatArrival")) Distance = 900;
	else if (Scenario == TEXT("Reselection") || Scenario == TEXT("MovingFire")) Distance = 3500;
	else if (Scenario.StartsWith(TEXT("Charge")) || Scenario.StartsWith(TEXT("PoolCharge")) || Scenario.StartsWith(TEXT("PoolAutoCharge"))) Distance = 400;
	FEnemyCombatIntegrationWorld Fixture(*this, Parameters);
	const FVector Offset = Scenario == TEXT("SpatialRange") ? FVector(300, 0, 450)
		: Scenario == TEXT("BlockedChargeEntry") ? FVector(400, 0, 0) : FVector(Distance, 0, 0);
	if (!Fixture.Initialize(bMelee, Offset)) return false;
	if (Scenario.StartsWith(TEXT("Boundary"))) return CheckCombatDistanceBranch(Fixture, Distance, bMelee);
	if (Scenario == TEXT("MovingFire")) return CheckGunMovingFire(Fixture);
	if (Scenario == TEXT("RetreatArrival")) return CheckRetreatArrival(Fixture);
	if (Scenario == TEXT("OrbitHold")) return CheckOrbitHold(Fixture);
	if (Scenario == TEXT("Reselection")) return CheckCombatReselection(Fixture);
	if (Scenario.StartsWith(TEXT("OrbitOneSide"))) return CheckOrbitObstacles(Fixture, false);
	if (Scenario.StartsWith(TEXT("OrbitBothSides"))) return CheckOrbitObstacles(Fixture, true);
	if (Scenario == TEXT("SightLoss")) return CheckCombatSightLoss(Fixture);
	if (Scenario == TEXT("MovementStun")) return CheckCombatMovementStun(Fixture);
	if (Scenario == TEXT("SpatialRange")) return CheckMeleeChargeRejection(Fixture, false);
	if (Scenario == TEXT("BlockedChargeEntry")) return CheckMeleeChargeRejection(Fixture, true);
	if (Scenario == TEXT("Unreachable")) return CheckMeleeUnreachable(Fixture);
	if (Scenario == TEXT("ChargeWallTimeout")) return CheckChargeWallTimeout(Fixture);
	if (Scenario.StartsWith(TEXT("PoolChargeScheduledWall")))
		return PrepareReusedPoolCombat(Fixture) && CheckScheduledChargeWall(Fixture, Scenario);
	if (Scenario.StartsWith(TEXT("PoolAutoChargeWall")))
		return PrepareReusedPoolCombat(Fixture, true) && CheckScheduledChargeWall(Fixture, Scenario, false);
	if (Scenario.StartsWith(TEXT("ChargeScheduledWall"))) return CheckScheduledChargeWall(Fixture, Scenario);
	if (Scenario.StartsWith(TEXT("Charge"))) return CheckCommittedCharge(Fixture, Scenario);
	AddError(TEXT("Unknown combat integration scenario"));
	return false;
}

namespace
{
struct FEnemyRoomCombatIntegrationWorld : FEnemyCombatIntegrationWorld
{
	UEnemyRoomSubsystem* Rooms = nullptr;
	AEnemyBase* Observer = nullptr;

	FEnemyRoomCombatIntegrationWorld(FAutomationTestBase& InTest, const FString& InScenario)
		: FEnemyCombatIntegrationWorld(InTest, InScenario) {}

	AEnemyBase* CreateObserver()
	{
		// BeginPlay가 있는 월드에서는 스탯 행이 없는 기본 EnemyBase가 첫 Tick에 사망한다.
		// 수신자와 같은 실제 BP를 사용해 정상 생존 상태를 만들고, 관측 입력 외의 행동만 중단한다.
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AEnemyBase* Result = World->SpawnActor<AEnemyBase>(Enemy->GetClass(),
			FTransform(FVector(-3000, 2000, 300)), Spawn);
		if (!Test.TestNotNull(TEXT("Room observer spawns"), Result)) return nullptr;
		if (UStateTreeComponent* ObserverTree = Result->GetStateTreeComponent())
			ObserverTree->StopLogic(TEXT("Controlled room observation input"));
		Result->GetRoomTagComp()->AssignDefaultRoomTag(SearchRoomTag);
		Result->EnterCombatFromRoom(Target->GetActorLocation(), false);
		Result->SetCanBeDamaged(false);
		Result->SetActorEnableCollision(false);
		Result->SpawnDefaultController();
		AEnemyAIController* AI = Cast<AEnemyAIController>(Result->GetController());
		if (!Test.TestNotNull(TEXT("Room observer has its own AI controller"), AI)) return nullptr;
		// 관측 입력만 보고 API로 통제한다. 수신 드론은 실제 Controller Tick으로 공유를 받아야 한다.
		AI->SetEnemyPerceptionEnabled(false);
		AI->SetActorTickEnabled(false);
		Result->GetCharacterMovement()->DisableMovement();
		if (!Test.TestTrue(TEXT("Actual BP observer has living combat stats"),
			!Result->IsDead() && Result->GetCurrentHealth() > 0.0f)) return nullptr;
		return Result;
	}

	bool InitializeShared(bool bMelee, const FVector& TargetOffset, bool bReportTarget = true)
	{
		if (!Initialize(bMelee, TargetOffset, false)) return false;
		Rooms = World->GetSubsystem<UEnemyRoomSubsystem>();
		if (!Test.TestNotNull(TEXT("Room sharing subsystem exists"), Rooms)) return false;
		// Arena 로드/웨이브 진행은 이 테스트의 입력이다. 실제 Room 조회와 관측 만료는 그대로 실행한다.
		Rooms->SetActiveRoomTargetForTesting(SearchRoomTag, Target->GetActorLocation(), false);
		Observer = CreateObserver();
		if (!Observer) return false;
		if (!Test.TestTrue(TEXT("Actual combat Room permits sharing"), Rooms->IsRoomInCombat(SearchRoomTag))) return false;
		if (bReportTarget)
		{
			Rooms->ReportRoomTargetContact(Observer, Target, Target->GetActorLocation());
			// 전투 시작 전 한 프레임만 진행한다. 수동 Refresh 없이 실제 Controller 갱신으로 대상을 채운다.
			Tick(Step);
			if (!Test.TestFalse(TEXT("Direct observer survives the first world tick"), Observer->IsDead())) return false;
			FEnemyRoomTargetObservationSnapshot Observation;
			if (!Test.TestTrue(TEXT("Living direct observation remains available after world tick"),
				Rooms->TryGetRoomTargetObservation(Enemy, Target, Observation) && Observation.bDirectlyObserved)) return false;
			if (!Test.TestEqual(TEXT("Controller update adopts the never-seen shared player"), Enemy->GetTrackedTarget(), static_cast<AActor*>(Target))) return false;
		}
		return Test.TestFalse(TEXT("Shared receiver has never directly seen the player"), Enemy->IsPlayerCurrentlyVisible());
	}
};

bool CheckSharedSourceContinuity(FEnemyRoomCombatIntegrationWorld& Fixture, bool bObserverHandoff)
{
	Fixture.Start();
	if (!Fixture.WaitForTask<FEnemyFlyToLocationTask>()) return false;
	const auto* Fly = Fixture.Data<FEnemyFlyToLocationTask>();
	const TArray<FVector> Route = Fly->PathWaypoints;
	const FVector Destination = Fly->Destination;
	AEnemyBase* NextObserver = bObserverHandoff ? Fixture.CreateObserver() : nullptr;
	if (bObserverHandoff && !NextObserver) return false;
	for (int32 Transition = 0; Transition < 2; ++Transition)
	{
		if (bObserverHandoff)
		{
			Fixture.Rooms->RemoveRoomTargetObserver(Transition == 0 ? Fixture.Observer : NextObserver);
			Fixture.Rooms->ReportRoomTargetContact(Transition == 0 ? NextObserver : Fixture.Observer,
				Fixture.Target, Fixture.Target->GetActorLocation());
		}
		else Fixture.SetSight(Transition == 0);
		if (!Fixture.Advance()) return false;
		Fly = Fixture.Data<FEnemyFlyToLocationTask>();
		if (!Fixture.Test.TestNotNull(TEXT("Same-target source change keeps the live Fly task"), Fly)) return false;
		Fixture.Test.TestTrue(TEXT("Source change preserves the selected route"), Fly->PathWaypoints == Route);
		Fixture.Test.TestTrue(TEXT("Source change preserves the movement destination"), Fly->Destination.Equals(Destination));
		Fixture.Test.TestEqual(TEXT("Source change retains the same player"), Fixture.Enemy->GetTrackedTarget(), static_cast<AActor*>(Fixture.Target));
		Fixture.Test.TestFalse(TEXT("Source change never enters search"), Fixture.Enemy->GetSearchEpisode().bActive);
	}
	// 보고를 새로 중계하지 않아도 남아 있는 직접 관측자는 기존 3초 기한 뒤에도 유효하다.
	for (int32 Frame = 0; Frame < 240; ++Frame)
	{
		if (!Fixture.Advance()) return false;
		if (!Fixture.Test.TestEqual(TEXT("Live shared observer survives the old grace deadline"),
			Fixture.Enemy->GetTrackedTarget(), static_cast<AActor*>(Fixture.Target))) return false;
		if (!Fixture.Test.TestFalse(TEXT("Shared tracking never fabricates receiver sight"), Fixture.Enemy->IsPlayerCurrentlyVisible())) return false;
	}
	return true;
}

bool CheckSharedTrackingExpiry(FEnemyRoomCombatIntegrationWorld& Fixture, bool bAlternative, bool bReacquire)
{
	if (bReacquire)
	{
		if (!CheckGunMovingFire(Fixture)) return false;
		// 첫 실제 사격으로 준비 기록을 만든 뒤, 수색 복귀 검사가 탄창 소진에 좌우되지 않게 한다.
		FBoolProperty* InfiniteAmmo = FindFProperty<FBoolProperty>(Fixture.Enemy->GetCurrentWeapon()->GetClass(), TEXT("bInfiniteAmmo"));
		if (!Fixture.Test.TestNotNull(TEXT("Reacquire ammo mode is inspectable"), InfiniteAmmo)) return false;
		InfiniteAmmo->SetPropertyValue_InContainer(Fixture.Enemy->GetCurrentWeapon(), true);
	}
	else Fixture.Start();
	if (!Fixture.WaitForTask<FEnemyFlyToLocationTask>()) return false;
	AActor* Alternative = nullptr;
	if (bAlternative)
	{
		AEnemyBase* OtherObserver = Fixture.CreateObserver();
		if (!OtherObserver) return false;
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AShooterCharacter* Other = Fixture.World->SpawnActor<AShooterCharacter>(Fixture.Target->GetClass(),
			FTransform(Fixture.Target->GetActorLocation() + FVector(0, 500, 0)), Spawn);
		if (!Fixture.Test.TestNotNull(TEXT("Alternative player spawns"), Other)) return false;
		Other->SetActorEnableCollision(false);
		Other->SetCanBeDamaged(false);
		Other->GetCharacterMovement()->DisableMovement();
		Other->SetActorTickEnabled(false);
		Alternative = Other;
		Fixture.Rooms->ReportRoomTargetContact(OtherObserver, Other, Other->GetActorLocation());
	}
	// 기한은 제거/수신 시각이 아니라 이 마지막 직접 관측 보고 시각부터 계산한다.
	Fixture.Rooms->ReportRoomTargetContact(Fixture.Observer, Fixture.Target, Fixture.Target->GetActorLocation());
	const double ReportedAt = Fixture.World->GetTimeSeconds();
	const float Grace = Fixture.Enemy->GetRuntimeStat().TargetTrackingGraceSeconds;
	Fixture.Rooms->RemoveRoomTargetObserver(Fixture.Observer);
	FVector LastValid = Fixture.Enemy->GetLastKnownPlayerLocation();
	bool bReachedSearch = false;
	for (int32 Frame = 0; Frame < FMath::CeilToInt((Grace + 0.5f) / Fixture.Step); ++Frame)
	{
		// 숨은 대상은 이동시켜도 마지막 관측 단서만 유지한다. 만료 후에도 실시간 좌표를 수색 중심에 섞지 않는다.
		Fixture.Target->AddActorWorldOffset(FVector(0, 1, 0));
		if (!Fixture.Advance()) return false;
		const double SinceReport = Fixture.World->GetTimeSeconds() - ReportedAt;
		if (SinceReport < Grace - Fixture.Step)
		{
			Fixture.Test.TestEqual(TEXT("Shared grace keeps the original target despite alternatives"),
				Fixture.Enemy->GetTrackedTarget(), static_cast<AActor*>(Fixture.Target));
			if (!Fixture.Test.TestFalse(TEXT("Shared grace does not enter search"), Fixture.Enemy->GetSearchEpisode().bActive)) return false;
			LastValid = Fixture.Enemy->GetLastKnownPlayerLocation();
		}
		if (bAlternative)
		{
			if (!Fixture.Test.TestFalse(TEXT("Alternative selection never opens a search episode"), Fixture.Enemy->GetSearchEpisode().bActive)) return false;
			if (Fixture.Enemy->GetTrackedTarget() == Alternative)
				return Fixture.Test.TestTrue(TEXT("Alternative is selected at expiry, not before"), SinceReport >= Grace - Fixture.Step);
		}
		else if (Fixture.Enemy->GetSearchEpisode().bActive)
		{
			Fixture.Test.TestTrue(TEXT("Search starts at shared expiry without another LossWait"),
				SinceReport >= Grace - Fixture.Step && SinceReport < Grace + 0.25f);
			// 한 프레임 경계의 최종 유효 샘플 차이만 허용한다.
			Fixture.Test.TestTrue(TEXT("Search center is the last valid tracked position"),
				Fixture.Enemy->GetSearchEpisode().Center.Equals(LastValid, 2.0f));
			Fixture.Test.TestNull(TEXT("Search cannot keep firing at the expired live Actor"), Fixture.Enemy->GetTrackedTarget());
			Fixture.Test.TestEqual(TEXT("Shared expiry stops the live attack before search"), Fixture.Enemy->GetAttackPhase(), EEnemyAttackPhase::Idle);
			bReachedSearch = true;
			break;
		}
	}
	if (bAlternative || !bReachedSearch)
	{
		Fixture.Test.AddError(TEXT("Shared expiry reached neither the expected alternative nor search"));
		return false;
	}
	if (!bReacquire) return true;
	const float RecoveryRemaining = Fixture.Enemy->GetTargetAttackRecoveryRemaining();
	Fixture.Rooms->ReportRoomTargetContact(Fixture.Observer, Fixture.Target, Fixture.Target->GetActorLocation());
	const int32 SharedAmmo = Fixture.Enemy->GetCurrentWeapon()->GetCurrentAmmo();
	for (int32 Frame = 0; Frame < 3; ++Frame)
	{
		if (!Fixture.Advance())
		{
			return false;
		}
		if (!Fixture.Test.TestEqual(TEXT("Shared reacquisition resumes tracking without fabricating own sight"),
			Fixture.Enemy->GetTrackedTarget(), static_cast<AActor*>(Fixture.Target))
			|| !Fixture.Test.TestEqual(TEXT("A new room report cannot grant hidden firing"),
				Fixture.Enemy->GetCurrentWeapon()->GetCurrentAmmo(), SharedAmmo)
			|| !Fixture.Test.TestFalse(TEXT("Shared reacquisition cannot authorize a target shot"),
				Fixture.Enemy->CanAttackTrackedTarget(Fixture.Target))
			|| !Fixture.Test.TestFalse(TEXT("Room report leaves receiver sight false"), Fixture.Enemy->IsPlayerCurrentlyVisible()))
		{
			return false;
		}
	}
	// 공유 재획득은 수색만 끝낸다. 자신의 재노출 뒤에만 기존 준비와 회복을 이어 사격한다.
	Fixture.SetSight(true);
	for (int32 Frame = 0; Frame < FMath::CeilToInt((RecoveryRemaining + 0.5f) / Fixture.Step); ++Frame)
	{
		if (!Fixture.Advance()) return false;
		if (!Fixture.Test.TestTrue(TEXT("Shared reacquisition never repeats attack telegraph"),
			Fixture.Enemy->GetAttackPhase() != EEnemyAttackPhase::Telegraph)) return false;
		const auto* Attack = Fixture.Data<FEnemyAttackTargetTask>();
		if (Attack && Attack->bAttackStarted && Fixture.Enemy->GetTrackedTarget() == Fixture.Target)
		{
			Fixture.Test.TestFalse(TEXT("Shared reacquisition leaves the real search episode"), Fixture.Enemy->GetSearchEpisode().bActive);
			return Fixture.Test.TestTrue(TEXT("Actual reacquisition resumes fire only after own direct exposure"), Fixture.Enemy->IsPlayerCurrentlyVisible());
		}
	}
	Fixture.Test.AddError(TEXT("A new direct report did not resume fire within the remaining recovery window"));
	return false;
}

bool CheckGunBurstActiveTime(FEnemyRoomCombatIntegrationWorld& Fixture)
{
	ARangedWeaponBase* Weapon = Fixture.Enemy->GetCurrentWeapon();
	FFloatProperty* Interval = Weapon ? FindFProperty<FFloatProperty>(Weapon->GetClass(), TEXT("AttackInterval")) : nullptr;
	if (!Fixture.Test.TestNotNull(TEXT("Burst-clock test has a configurable real gun"), Interval)) return false;
	// 탄 사이에 여러 Task 평가가 들어가도록 실제 무기 cadence만 늘린다. 저장 Tree의 버스트 길이는 그대로 둔다.
	Interval->SetPropertyValue_InContainer(Weapon, 1.0f);
	// 공유 준비 인계 뒤의 두 번째 탄은 버스트 끝에 걸칠 수 있다. 이 검사는 첫 버스트의 첫 탄부터 시간을 잰다.
	Fixture.SetSight(true);
	if (!CheckGunMovingFire(Fixture)) return false;
	const auto* Attack = Fixture.Data<FEnemyAttackTargetTask>();
	if (!Fixture.Test.TestNotNull(TEXT("Saved Tree starts the live target attack task"), Attack)) return false;
	const float Duration = Attack->SelectedAttackDuration;
	const float FirstElapsed = Attack->ElapsedTime;
	const int32 FirstAmmo = Weapon->GetCurrentAmmo();
	if (!Fixture.Advance()) return false;
	Attack = Fixture.Data<FEnemyAttackTargetTask>();
	if (!Fixture.Test.TestNotNull(TEXT("Normal cadence keeps the original attack task"), Attack)) return false;
	Fixture.Test.TestTrue(TEXT("Normal inter-shot cooldown still consumes active burst time"), Attack->ElapsedTime > FirstElapsed);
	Fixture.Test.TestEqual(TEXT("Active burst time advances without requiring a new ammunition frame"), Weapon->GetCurrentAmmo(), FirstAmmo);
	Fixture.Test.TestFalse(TEXT("The cadence assertion is made during the real weapon cooldown"), Weapon->CanAttack());
	float HeldElapsed = Attack->ElapsedTime;
	const FVector Origin = Fixture.Enemy->GetCombatShotOrigin();
	const FVector CurrentDirection = (Fixture.Target->GetActorLocation() - Origin).GetSafeNormal();
	const FVector SideDirection(-CurrentDirection.Y, CurrentDirection.X, CurrentDirection.Z);
	Fixture.Target->SetActorLocation(Origin + SideDirection * 3500.0f);
	// 조준 입력만 멈춰 회전 지연을 만든다. Tree/무기 Timer는 계속 실행하므로 실제 보류 연결을 검증한다.
	Fixture.Controller->SetActorTickEnabled(false);
	Fixture.Enemy->GetCharacterMovement()->DisableMovement();
	// 조준은 숨은 Actor 좌표를 즉시 읽지 않는다. 실제 TP 관측의 0.1초 갱신 경계를 지난 뒤 보류 시간을 잰다.
	for (int32 Frame = 0; Frame < 12 && Fixture.Enemy->IsTargetAttackAimReady(); ++Frame)
	{
		if (!Fixture.Advance())
		{
			return false;
		}
	}
	Attack = Fixture.Data<FEnemyAttackTargetTask>();
	if (!Fixture.Test.TestNotNull(TEXT("Observation refresh retains the original burst task"), Attack))
	{
		return false;
	}
	HeldElapsed = Attack->ElapsedTime;
	if (!Fixture.Test.TestFalse(TEXT("Sudden target motion exceeds the firing angle"), Fixture.Enemy->IsTargetAttackAimReady()))
	{
		return false;
	}
	for (int32 Frame = 0; Frame < FMath::CeilToInt((Duration + 0.25f) / Fixture.Step); ++Frame)
	{
		if (!Fixture.Advance()) return false;
		Attack = Fixture.Data<FEnemyAttackTargetTask>();
		if (!Fixture.Test.TestNotNull(TEXT("Angle hold retains the live saved-Tree attack task"), Attack)) return false;
		if (!Fixture.Test.TestEqual(TEXT("Angle hold preserves accumulated active burst time"), Attack->ElapsedTime, HeldElapsed)
			|| !Fixture.Test.TestEqual(TEXT("Angle hold preserves the selected burst duration"), Attack->SelectedAttackDuration, Duration)
			|| !Fixture.Test.TestEqual(TEXT("Angle hold does not consume ammunition"), Weapon->GetCurrentAmmo(), FirstAmmo)
			|| !Fixture.Test.TestEqual(TEXT("Angle hold cannot begin burst recovery"), Fixture.Enemy->GetTargetAttackRecoveryRemaining(), 0.0f)) return false;
	}
	Fixture.Controller->SetActorTickEnabled(true);
	bool bResumed = false;
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		if (!Fixture.Advance()) return false;
		Attack = Fixture.Data<FEnemyAttackTargetTask>();
		if (!Fixture.Test.TestNotNull(TEXT("Bounded rotation resumes the same attack task"), Attack)) return false;
		if (!Attack->bAttackStarted || Weapon->GetCurrentAmmo() >= FirstAmmo) continue;
		Fixture.Test.TestEqual(TEXT("Resumed burst retains its original duration"), Attack->SelectedAttackDuration, Duration);
		bResumed = true;
		break;
	}
	if (!Fixture.Test.TestTrue(TEXT("Actual bounded Controller rotation resumes a real shot"), bResumed)) return false;
	const float ResumedElapsed = Attack->ElapsedTime;
	const int32 ResumedAmmo = Weapon->GetCurrentAmmo();
	if (!Fixture.Advance()) return false;
	Attack = Fixture.Data<FEnemyAttackTargetTask>();
	if (!Fixture.Test.TestNotNull(TEXT("Resumed burst remains active between shots"), Attack)) return false;
	Fixture.Test.TestTrue(TEXT("Resumed cadence continues consuming active burst time"), Attack->ElapsedTime > ResumedElapsed);
	Fixture.Test.TestEqual(TEXT("Resumed inter-shot frame does not need another bullet"), Weapon->GetCurrentAmmo(), ResumedAmmo);
	return !Fixture.Test.HasAnyErrors();
}

bool CheckGunTargetSwitchRecovery(FEnemyRoomCombatIntegrationWorld& Fixture)
{
	if (!CheckGunMovingFire(Fixture)) return false;
	Fixture.SetSight(true);
	bool bRecovering = false;
	for (int32 Frame = 0; Frame < 180; ++Frame)
	{
		const auto* Wait = Fixture.Data<FEnemyAttackPhaseWaitTask>();
		if (Wait && Wait->Phase == EEnemyAttackPhase::Recover
			&& Fixture.Enemy->GetTargetAttackRecoveryRemaining() > 0.3f)
		{
			bRecovering = true;
			break;
		}
		if (!Fixture.Advance()) return false;
	}
	if (!Fixture.Test.TestTrue(TEXT("The saved FireCycle begins actual burst recovery"), bRecovering)) return false;
	ARangedWeaponBase* Weapon = Fixture.Enemy->GetCurrentWeapon();
	const double Deadline = Fixture.World->GetTimeSeconds() + Fixture.Enemy->GetTargetAttackRecoveryRemaining();
	const int32 RecoveryAmmo = Weapon->GetCurrentAmmo();
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* NextTarget = Fixture.World->SpawnActor<AShooterCharacter>(Fixture.Target->GetClass(),
		FTransform(Fixture.Target->GetActorLocation() + FVector(0, 200, 0)), Spawn);
	if (!Fixture.Test.TestNotNull(TEXT("New hostile target spawns during recovery"), NextTarget)) return false;
	NextTarget->SetActorEnableCollision(false);
	NextTarget->SetCanBeDamaged(false);
	NextTarget->GetCharacterMovement()->DisableMovement();
	NextTarget->SetActorTickEnabled(false);
	// 대상 입력만 교체한다. 새 전조 완료와 회복 종료는 저장 Tree의 실제 Task가 처리한다.
	Fixture.SetSight(false);
	Fixture.Target = NextTarget;
	Fixture.SetSight(true);
	// 직접 발견과 전투 목표 선택은 별도다. 기존 대상 유지 정책을 우회하지 않고 선택 입력만 명시한다.
	Fixture.Enemy->ObserveTrackedTarget(NextTarget);
	Fixture.Enemy->GetCharacterMovement()->DisableMovement();
	bool bPreparedDuringRecovery = false;
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		if (!Fixture.Advance()) return false;
		const double Now = Fixture.World->GetTimeSeconds();
		if (Now < Deadline - Fixture.Step)
		{
			if (!Fixture.Test.TestEqual(TEXT("Recovery keeps the newly visible target"), Fixture.Enemy->GetTrackedTarget(), static_cast<AActor*>(NextTarget))
				|| !Fixture.Test.TestEqual(TEXT("New-target preparation never skips the original burst recovery"), Weapon->GetCurrentAmmo(), RecoveryAmmo)
				|| !Fixture.Test.TestTrue(TEXT("Target replacement and reselection preserve the original recovery deadline"),
					FMath::IsNearlyEqual(Now + Fixture.Enemy->GetTargetAttackRecoveryRemaining(), Deadline, 0.01))) return false;
			bPreparedDuringRecovery |= Fixture.Enemy->IsTargetAttackPreparationComplete();
			if (Frame == 2)
			{
				// 같은 대상의 감지 재선택도 준비·회복을 재시작하지 않아야 한다.
				Fixture.SetSight(false);
				Fixture.SetSight(true);
			}
		}
		if (Weapon->GetCurrentAmmo() < RecoveryAmmo)
		{
			Fixture.Test.TestTrue(TEXT("Recover Task advances new-target telegraph before recovery finishes"), bPreparedDuringRecovery);
			return Fixture.Test.TestTrue(TEXT("The actual new-target shot respects the original recovery deadline"), Now >= Deadline - Fixture.Step);
		}
	}
	Fixture.Test.AddError(TEXT("New-target preparation did not resume a real shot after the original recovery deadline"));
	return false;
}

bool CheckSharedAttackContinuity(FEnemyRoomCombatIntegrationWorld& Fixture)
{
	if (!CheckGunMovingFire(Fixture)) return false;
	const auto* Attack = Fixture.Data<FEnemyAttackTargetTask>();
	if (!Fixture.Test.TestNotNull(TEXT("Real parallel fire task is active after the first shot"), Attack)) return false;
	float Elapsed = Attack->ElapsedTime;
	const float Duration = Attack->SelectedAttackDuration;
	ARangedWeaponBase* Weapon = Fixture.Enemy->GetCurrentWeapon();
	for (int32 Transition = 0; Transition < 2; ++Transition)
	{
		const int32 AmmoBefore = Weapon->GetCurrentAmmo();
		Fixture.SetSight(Transition == 0);
		if (!Fixture.Advance()) return false;
		Attack = Fixture.Data<FEnemyAttackTargetTask>();
		if (!Fixture.Test.TestNotNull(TEXT("Same-target sight change keeps the parallel fire task"), Attack)) return false;
		Fixture.Test.TestTrue(TEXT("Same-target source change does not reset firing elapsed time"), Attack->ElapsedTime >= Elapsed);
		Fixture.Test.TestEqual(TEXT("Same-target source change preserves the attack duration"), Attack->SelectedAttackDuration, Duration);
		Fixture.Test.TestTrue(TEXT("Same-target source change preserves completed preparation"), Fixture.Enemy->IsTargetAttackPreparationComplete());
		if (Transition == 1)
		{
			Fixture.Test.TestFalse(TEXT("Shared-only tracking pauses the actual target attack"), Attack->bAttackStarted);
			Fixture.Test.TestEqual(TEXT("Sight loss does not consume new ammunition"), Weapon->GetCurrentAmmo(), AmmoBefore);
		}
		Elapsed = Attack->ElapsedTime;
	}
	return true;
}

bool CheckNoiseAttackContinuity(FEnemyRoomCombatIntegrationWorld& Fixture)
{
	ARangedWeaponBase* Weapon = Fixture.Enemy->GetCurrentWeapon();
	if (!Fixture.Test.TestNotNull(TEXT("Investigation continuity has a real equipped gun"), Weapon)) return false;
	FFloatProperty* Interval = FindFProperty<FFloatProperty>(Weapon->GetClass(), TEXT("AttackInterval"));
	if (!Fixture.Test.TestNotNull(TEXT("Real shot interval is configurable"), Interval)) return false;
	// 첫 발 직후 조사/재발견을 넣어 실제 무기 쿨다운을 건너뛰는 회귀를 탄약 소비로 구분한다.
	Interval->SetPropertyValue_InContainer(Weapon, 1.0f);
	if (!CheckGunMovingFire(Fixture)) return false;
	const int32 AmmoAfterFirstShot = Weapon->GetCurrentAmmo();
	const FVector NoiseLocation = Fixture.Enemy->GetActorLocation() + FVector(0, 300, 0);
	if (!Fixture.Test.TestTrue(TEXT("Nearby player sound is accepted while a farther shared target exists"),
		Fixture.Enemy->RecordHeardNoise(Fixture.Target, NoiseLocation, Fixture.World->GetTimeSeconds()))) return false;
	FVector Destination;
	if (!Fixture.Test.TestTrue(TEXT("Closer sound starts investigation during real firing"),
		Fixture.Enemy->BeginNoiseInvestigation(Destination))) return false;
	Fixture.Test.TestFalse(TEXT("Investigation pauses the actual weapon"), Weapon->IsAttacking());
	Fixture.Test.TestFalse(TEXT("Investigation blocks starting another target attack"), Fixture.Enemy->StartAttackTarget(Fixture.Target));
	Fixture.Test.TestEqual(TEXT("Investigation retains shared target knowledge"),
		Fixture.Enemy->GetTrackedTarget(), static_cast<AActor*>(Fixture.Target));
	const double ReadyAt = Fixture.World->GetTimeSeconds() + Weapon->GetAttackRetryDelay();
	if (!Fixture.Test.TestTrue(TEXT("First shot leaves a measurable preserved cooldown"),
		ReadyAt - Fixture.World->GetTimeSeconds() > 0.5)) return false;
	// Slice 3는 네이티브 흐름을 검증한다. 에셋의 조사 State 연결 대신 기존 공격 Task의 실제 Exit를 실행한다.
	Fixture.Tree->StopLogic(TEXT("Controlled investigation transition"));
	for (int32 Frame = 0; Frame < 6; ++Frame) Fixture.Tick(Fixture.Step);
	Fixture.Test.TestEqual(TEXT("Paused weapon consumes no ammunition during investigation"), Weapon->GetCurrentAmmo(), AmmoAfterFirstShot);
	Fixture.SetSight(true);
	Fixture.Test.TestFalse(TEXT("Direct reacquisition cancels the investigation"), Fixture.Enemy->IsNoiseInvestigationActive());
	Fixture.Test.TestFalse(TEXT("Reacquisition does not clear the actual cooldown"), Weapon->CanAttack());
	Fixture.Start();
	const double Deadline = ReadyAt + Fixture.Enemy->GetTargetAttackRecoveryRemaining() + 1.0;
	while (Fixture.World->GetTimeSeconds() < Deadline)
	{
		if (!Fixture.Advance()) return false;
		if (!Fixture.Test.TestTrue(TEXT("Same-target reacquisition never repeats preparation"),
			Fixture.Enemy->GetAttackPhase() != EEnemyAttackPhase::Telegraph)) return false;
		if (Fixture.World->GetTimeSeconds() < ReadyAt - 2.0 * Fixture.Step)
			if (!Fixture.Test.TestEqual(TEXT("Real gun cannot fire before its original cooldown expires"),
				Weapon->GetCurrentAmmo(), AmmoAfterFirstShot)) return false;
		if (Weapon->GetCurrentAmmo() < AmmoAfterFirstShot)
			return Fixture.Test.TestTrue(TEXT("Actual resumed shot respects the preserved fire deadline"),
				Fixture.World->GetTimeSeconds() >= ReadyAt - 2.0 * Fixture.Step);
	}
	Fixture.Test.AddError(TEXT("Same-target reacquisition did not resume actual fire after the preserved cooldown"));
	return false;
}

bool CheckSharedChargeContinuity(FEnemyRoomCombatIntegrationWorld& Fixture)
{
	Fixture.Start();
	if (!Fixture.WaitForTask<FEnemyChargeLastDirectionTask>()) return false;
	ASelfDestructDrone* Drone = CastChecked<ASelfDestructDrone>(Fixture.Enemy);
	const FVector Direction = Drone->GetActorForwardVector();
	const float Duration = Fixture.Data<FEnemyChargeLastDirectionTask>()->TelegraphDuration;
	float Elapsed = Drone->GetCommittedChargeElapsedTime();
	for (int32 Transition = 0; Transition < 2; ++Transition)
	{
		Fixture.SetSight(Transition == 0);
		if (Transition == 1) Fixture.Rooms->RemoveRoomTargetObserver(Fixture.Observer);
		if (!Fixture.Advance()) return false;
		const auto* Charge = Fixture.Data<FEnemyChargeLastDirectionTask>();
		if (!Fixture.Test.TestNotNull(TEXT("Shared/direct/grace transitions retain the actual charge task"), Charge)) return false;
		Fixture.Test.TestTrue(TEXT("Source changes preserve committed charge"), Drone->IsSelfDestructCommitted());
		Fixture.Test.TestTrue(TEXT("Source changes do not reset charge elapsed time"), Drone->GetCommittedChargeElapsedTime() > Elapsed);
		Fixture.Test.TestEqual(TEXT("Source changes preserve the original charge duration"), Charge->TelegraphDuration, Duration);
		Fixture.Test.TestTrue(TEXT("Source changes preserve the committed direction"), FVector::DotProduct(Direction, Drone->GetActorForwardVector()) > 0.999);
		Fixture.Test.TestFalse(TEXT("Committed source changes never start search"), Drone->GetSearchEpisode().bActive);
		Elapsed = Drone->GetCommittedChargeElapsedTime();
	}
	return true;
}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyRoomCombatWorldTest, "Outlier.Enemy.RoomTracking.Integration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyRoomCombatWorldTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("Gun.MovingFire"), TEXT("Gun.SourceContinuity"), TEXT("Gun.AttackContinuity"), TEXT("Gun.BurstActiveTime"), TEXT("Gun.TargetSwitchRecovery"),
		TEXT("Gun.ObserverHandoff"), TEXT("Gun.Expiry"), TEXT("Gun.Alternative"), TEXT("Gun.SearchReacquire"),
		TEXT("Gun.ForcedHintOnly"), TEXT("Gun.NoiseAttackContinuity"), TEXT("Melee.Expiry"), TEXT("Melee.ChargeEntry"),
		TEXT("Melee.BlockedChargeEntry"), TEXT("Melee.ChargeSourceContinuity"), TEXT("Melee.ChargeWallTimeout")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyRoomCombatWorldTest::RunTest(const FString& Parameters)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	FString Type, Scenario;
	if (!Parameters.Split(TEXT("."), &Type, &Scenario)) return false;
	const bool bMelee = Type == TEXT("Melee");
	const float Distance = Scenario == TEXT("ChargeEntry") || Scenario == TEXT("BlockedChargeEntry")
		|| Scenario == TEXT("ChargeSourceContinuity") || Scenario == TEXT("ChargeWallTimeout") ? 400.0f : 3500.0f;
	FEnemyRoomCombatIntegrationWorld Fixture(*this, Parameters);
	if (!Fixture.InitializeShared(bMelee, FVector(Distance, 0, 0), Scenario != TEXT("ForcedHintOnly"))) return false;
	if (Scenario == TEXT("MovingFire")) return CheckGunMovingFire(Fixture);
	if (Scenario == TEXT("AttackContinuity")) return CheckSharedAttackContinuity(Fixture);
	if (Scenario == TEXT("BurstActiveTime")) return CheckGunBurstActiveTime(Fixture);
	if (Scenario == TEXT("TargetSwitchRecovery")) return CheckGunTargetSwitchRecovery(Fixture);
	if (Scenario == TEXT("NoiseAttackContinuity")) return CheckNoiseAttackContinuity(Fixture);
	if (Scenario == TEXT("SourceContinuity") || Scenario == TEXT("ObserverHandoff"))
		return CheckSharedSourceContinuity(Fixture, Scenario == TEXT("ObserverHandoff"));
	if (Scenario == TEXT("Expiry") || Scenario == TEXT("Alternative") || Scenario == TEXT("SearchReacquire"))
		return CheckSharedTrackingExpiry(Fixture, Scenario == TEXT("Alternative"), Scenario == TEXT("SearchReacquire"));
	if (Scenario == TEXT("BlockedChargeEntry")) return CheckMeleeChargeRejection(Fixture, true);
	if (Scenario == TEXT("ChargeWallTimeout")) return CheckChargeWallTimeout(Fixture);
	if (Scenario == TEXT("ChargeSourceContinuity")) return CheckSharedChargeContinuity(Fixture);
	if (Scenario == TEXT("ChargeEntry")) return CheckCombatDistanceBranch(Fixture, Distance, true);
	if (Scenario == TEXT("ForcedHintOnly"))
	{
		// 강제 좌표 핑은 기존 수색/위협 사격 힌트다. 직접 관측이 없으면 live Actor 권한을 만들지 않는다.
		Fixture.Rooms->TriggerForcedShareForTesting(Fixture.SearchRoomTag);
		Fixture.Enemy->ApplySharedTargetContact(Fixture.Target->GetActorLocation(), true, 10000);
		Fixture.Start();
		for (int32 Frame = 0; Frame < 60; ++Frame)
		{
			if (!Fixture.Advance()) return false;
			if (!TestNull(TEXT("Coordinate hints never become live tracking"), Fixture.Enemy->GetTrackedTarget())) return false;
			if (!TestFalse(TEXT("Coordinate hints do not grant target attack permission"), Fixture.Enemy->CanAttackTrackedTarget(Fixture.Target))) return false;
		}
		return true;
	}
	AddError(TEXT("Unknown shared combat integration scenario"));
	return false;
}

namespace
{
bool HearInvestigationNoise(FEnemyCombatIntegrationWorld& Fixture, const FVector& Location)
{
	UAIPerceptionComponent* Perception = Fixture.Controller->GetPerceptionComponent();
	if (!Fixture.Test.TestNotNull(TEXT("Noise input reaches the real perception component"), Perception)) return false;
	// 감지 입력만 통제한다. 조사 시작/이동/수색은 저장된 StateTree의 전이가 직접 실행해야 한다.
	FAIStimulus Stimulus(*GetDefault<UAISense_Hearing>(), 1.0f, Location, Fixture.Enemy->GetActorLocation());
	Perception->RegisterStimulus(Fixture.Target, Stimulus);
	Perception->ProcessStimuli();
	FEnemyHeardNoise Heard;
	return Fixture.Test.TestTrue(TEXT("The hearing callback stores the occurrence snapshot"),
		Fixture.Enemy->TryGetHeardNoise(Heard) && Heard.Location.Equals(Location));
}

bool WaitForNoiseState(FEnemyCombatIntegrationWorld& Fixture, bool bSearch, int32 MaxFrames = 600)
{
	for (int32 Frame = 0; Frame < MaxFrames; ++Frame)
	{
		if (bSearch)
		{
			const auto* Session = Fixture.Data<FEnemySearchSessionTask>();
			if (Session && Session->bNoiseInvestigationSearch && Fixture.Enemy->IsNoiseSearchActive()) return true;
		}
		else
		{
			const auto* Fly = Fixture.Data<FEnemyFlyToLocationTask>();
			if (Fly && Fly->bInvestigateNoise && Fixture.Enemy->IsNoiseInvestigationActive()) return true;
		}
		if (!Fixture.Advance()) break;
	}
	Fixture.Test.AddError(FString::Printf(TEXT("Saved tree did not enter noise %s. States=%s"),
		bSearch ? TEXT("search") : TEXT("flight"), *Fixture.Snapshot.Describe()));
	return false;
}

bool CheckSavedNoiseFlow(FEnemyCombatIntegrationWorld& Fixture, bool bAlert)
{
	Fixture.Start();
	if (!Fixture.Advance()) return false;
	const FVector Occurrence = Fixture.Enemy->GetActorLocation() + FVector(700, 0, 0);
	const FVector Start = Fixture.Enemy->GetActorLocation();
	if (!HearInvestigationNoise(Fixture, Occurrence) || !WaitForNoiseState(Fixture, false)) return false;
	const auto* Fly = Fixture.Data<FEnemyFlyToLocationTask>();
	if (!Fixture.Test.TestTrue(TEXT("Saved noise flight enables bounded AStar movement"),
		Fly && Fly->bUsePathfinding && Fly->MoveTimeout > 0.0f && Fly->NoProgressTimeout > 0.0f)) return false;
	Fixture.Test.TestTrue(TEXT("Saved flight follows the heard snapshot"), Fly->Destination.Equals(Occurrence));
	if (bAlert && !Fixture.Test.TestEqual(TEXT("Sound alone enters Alert rather than starting Room combat"),
		Fixture.Enemy->GetCombatState(), EEnemyCombatState::Alert)) return false;
	if (!WaitForNoiseState(Fixture, true)) return false;
	const auto* Session = Fixture.Data<FEnemySearchSessionTask>();
	if (!Fixture.Test.TestNotNull(TEXT("Saved noise search owns the timed session"), Session)) return false;
	const double FinishAt = Session->NoiseSearchStartedAt + Session->NoiseSearchDuration;
	const FVector Center = Fixture.Enemy->GetSearchEpisode().Center;
	Fixture.Test.TestTrue(TEXT("World movement reaches the sound before entering search"),
		FVector::Distance(Start, Center) > 400.0 && Center.Equals(Occurrence, 100.0));
	Fixture.Test.TestFalse(TEXT("A sound never grants actor attack permission"), Fixture.Enemy->CanAttackTrackedTarget(Fixture.Target));
	// 조사 이동 시간과 별개인 수색 제한을 확인한다. Alert의 기존 미발견 타이머가 먼저 종료하면 실패한다.
	for (int32 Frame = 0; Frame < 900 && Fixture.World->GetTimeSeconds() < FinishAt + 1.0; ++Frame)
	{
		if (!Fixture.Advance()) return false;
		if (Fixture.World->GetTimeSeconds() < FinishAt - 2.0 * Fixture.Step)
		{
			if (!Fixture.Test.TestTrue(TEXT("Noise search lasts for its own configured duration"), Fixture.Enemy->IsNoiseSearchActive())) return false;
			if (bAlert && !Fixture.Test.TestEqual(TEXT("Alert loss timeout cannot interrupt active investigation"),
				Fixture.Enemy->GetCombatState(), EEnemyCombatState::Alert)) return false;
		}
		if (Fixture.Enemy->IsNoiseInvestigationActive()) continue;
		Fixture.Test.TestTrue(TEXT("Saved completion does not finish the search early"),
			Fixture.World->GetTimeSeconds() >= FinishAt - 2.0 * Fixture.Step);
		if (bAlert)
			return Fixture.Test.TestEqual(TEXT("Unobserved Alert investigation returns to noncombat inside home Room"),
				Fixture.Enemy->GetCombatState(), EEnemyCombatState::NonCombat);
		return Fixture.Test.TestEqual(TEXT("An individual investigation cannot end an active Room combat"),
			Fixture.Enemy->GetCombatState(), EEnemyCombatState::Combat);
	}
	Fixture.Test.AddError(TEXT("Saved noise search did not consume and finish its investigation"));
	return false;
}

bool CheckSearchSightHandoff(FEnemyRoomCombatIntegrationWorld& Fixture, bool bNoiseSearch, bool bBlocked)
{
	if (!CheckGunMovingFire(Fixture)) return false;
	if (bNoiseSearch)
	{
		// 지속 중인 다른 관측자의 공유로 소리 조사 자체가 중단되지 않게 입력을 종료한다.
		Fixture.Rooms->RemoveRoomTargetObserver(Fixture.Observer);
		if (!HearInvestigationNoise(Fixture, Fixture.Enemy->GetActorLocation() + FVector(0, 700, 0))
			|| !WaitForNoiseState(Fixture, true)) return false;
	}
	else
	{
		Fixture.Rooms->ReportRoomTargetContact(Fixture.Observer, Fixture.Target, Fixture.Target->GetActorLocation());
		Fixture.Rooms->RemoveRoomTargetObserver(Fixture.Observer);
	}
	// 실제 저장된 트리에서 두리번거리기가 실행된 뒤 재발견한다. 이동 중 재발견만 검사하지 않는다.
	if (!Fixture.WaitForTask<FEnemyLookAroundTask>(1800)) return false;
	if (!Fixture.Test.TestTrue(TEXT("Reacquisition starts during an actual active search episode"),
		Fixture.Enemy->GetSearchEpisode().bActive)) return false;
	const FVector EnemyLocation = Fixture.Enemy->GetActorLocation();
	Fixture.Target->SetActorLocation(EnemyLocation + FVector(0, 1200, 0));
	AActor* Wall = bBlocked ? Fixture.SpawnBlockingBox(EnemyLocation + FVector(0, 600, 0), FVector(2000, 80, 1000)) : nullptr;
	if (bBlocked && !Fixture.Test.TestNotNull(TEXT("Attack-path obstruction exists"), Wall)) return false;
	// 시각 관측 입력은 별도 감지 경로에서 전달하되 사격 경로는 실제 월드 충돌로 차단한다.
	// 조준이 수색 회전을 덮어 가려 주는지와 무관하게 트리 자체가 수색을 종료해야 한다.
	ARangedWeaponBase* Weapon = Fixture.Enemy->GetCurrentWeapon();
	Weapon->RefillMagazine();
	Fixture.SetSight(true);
	if (!Fixture.Test.TestFalse(TEXT("Sight immediately releases the old search episode"), Fixture.Enemy->GetSearchEpisode().bActive)
		|| !Fixture.Test.TestFalse(TEXT("Sight immediately releases noise investigation ownership"), Fixture.Enemy->IsNoiseInvestigationActive())) return false;
	const int32 Ammo = Weapon->GetCurrentAmmo();
	if (bBlocked && !Fixture.Test.TestFalse(TEXT("Physical wall blocks the target attack path"), Fixture.Enemy->CanAttackTrackedTarget(Fixture.Target))) return false;
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		if (!Fixture.Advance()) return false;
		if (!Fixture.Test.TestNull(TEXT("Combat has no stale look-around task"), Fixture.Data<FEnemyLookAroundTask>())
			|| !Fixture.Test.TestNull(TEXT("Combat has no stale search-session task"), Fixture.Data<FEnemySearchSessionTask>())
			|| !Fixture.Test.TestFalse(TEXT("Combat cannot reopen the old search episode"), Fixture.Enemy->GetSearchEpisode().bActive)
			|| !Fixture.Test.TestEqual(TEXT("Search exit preserves the reacquired combat target"), Fixture.Enemy->GetTrackedTarget(), static_cast<AActor*>(Fixture.Target))) return false;
		const auto* Fly = Fixture.Data<FEnemyFlyToLocationTask>();
		if (!Fixture.Test.TestTrue(TEXT("Combat movement is not the old investigation flight"), !Fly || !Fly->bInvestigateNoise)) return false;
		if (bBlocked && !Fixture.Test.TestEqual(TEXT("Blocked attack path cannot consume ammunition"), Weapon->GetCurrentAmmo(), Ammo)) return false;
	}
	if (Wall) Wall->SetActorEnableCollision(false);
	const int32 ResumeAmmo = Weapon->GetCurrentAmmo();
	for (int32 Frame = 0; Frame < 300; ++Frame)
	{
		if (!Fixture.Advance()) return false;
		if (Weapon->GetCurrentAmmo() < ResumeAmmo)
			return Fixture.Test.TestEqual(TEXT("An actual shot resumes after search handoff"), Fixture.Enemy->GetCombatState(), EEnemyCombatState::Combat);
	}
	Fixture.Test.AddError(FString::Printf(TEXT("Search handoff did not resume a real shot: Ammo=%d CanAttack=%d Aim=%d Recovery=%.3f Phase=%d States=%s"),
		Weapon->GetCurrentAmmo(), Fixture.Enemy->CanAttackTrackedTarget(Fixture.Target), Fixture.Controller->IsAttackAimActive(),
		Fixture.Enemy->GetTargetAttackRecoveryRemaining(), int32(Fixture.Enemy->GetAttackPhase()), *Fixture.Snapshot.Describe()));
	return false;
}

bool CheckSavedNoiseFarRetarget(FEnemyRoomCombatIntegrationWorld& Fixture, const bool bGeneralSearch = false)
{
	if (!CheckGunMovingFire(Fixture)) return false;
	Fixture.Rooms->RemoveRoomTargetObserver(Fixture.Observer);
	if (bGeneralSearch)
	{
		if (!Fixture.WaitForTask<FEnemyLookAroundTask>(1800)) return false;
		if (!Fixture.Test.TestTrue(TEXT("New hearing starts during general lost-contact search"),
			Fixture.Enemy->GetSearchEpisode().bActive && !Fixture.Enemy->IsLocationInvestigationActive())) return false;
	}
	else if (!HearInvestigationNoise(Fixture, Fixture.Enemy->GetActorLocation() + FVector(0, 700, 0))
		|| !WaitForNoiseState(Fixture, true)) return false;
	const uint64 PreviousSerial = Fixture.Enemy->GetNoiseInvestigationSerial();
	if (!bGeneralSearch)
	{
		for (int32 Frame = 0; Frame < 60; ++Frame) if (!Fixture.Advance()) return false;
		if (!Fixture.Test.TestTrue(TEXT("Retarget begins during the actual search"), Fixture.Enemy->IsNoiseSearchActive())) return false;
	}
	const FVector NewLocation = Fixture.Enemy->GetActorLocation() + FVector(0, -1200, 0);
	UAIPerceptionComponent* Perception = Fixture.Controller->GetPerceptionComponent();
	FAIStimulus Stimulus(*GetDefault<UAISense_Hearing>(), 1.0f, NewLocation, Fixture.Enemy->GetActorLocation());
	Perception->RegisterStimulus(Fixture.Target, Stimulus);
	Perception->ProcessStimuli();
	if (!Fixture.Test.TestTrue(TEXT("Search accepts a farther new occurrence"), Fixture.Enemy->GetPerceptionGoal().Location.Equals(NewLocation))) return false;
	// 실제 저장된 트리가 수색에서 조사 이동으로 빠져나와야 한다. BeginNoiseInvestigation을 직접 호출하지 않는다.
	const double HandoffDeadline = Fixture.World->GetTimeSeconds() + 0.5;
	while (Fixture.World->GetTimeSeconds() < HandoffDeadline)
	{
		if (!Fixture.Advance()) return false;
		const auto* Fly = Fixture.Data<FEnemyFlyToLocationTask>();
		if (Fly && Fly->bInvestigateNoise && Fly->Destination.Equals(NewLocation))
		{
			Fixture.Test.TestTrue(TEXT("New movement owns a new episode"), Fixture.Enemy->GetNoiseInvestigationSerial() != PreviousSerial);
			return Fixture.Test.TestFalse(TEXT("Old search arrival cannot replace the new movement"),
				Fixture.Enemy->BeginNoiseInvestigationSearch(PreviousSerial, Fixture.Enemy->GetActorLocation()));
		}
	}
	Fixture.Test.AddError(TEXT("Saved StateTree did not leave search for the farther noise"));
	return false;
}

bool CheckSavedNoiseReacquire(FEnemyRoomCombatIntegrationWorld& Fixture)
{
	ARangedWeaponBase* Weapon = Fixture.Enemy->GetCurrentWeapon();
	if (!Fixture.Test.TestNotNull(TEXT("Saved noise continuity has a real gun"), Weapon)) return false;
	FFloatProperty* Interval = FindFProperty<FFloatProperty>(Weapon->GetClass(), TEXT("AttackInterval"));
	if (!Fixture.Test.TestNotNull(TEXT("Actual fire interval exists"), Interval)) return false;
	Interval->SetPropertyValue_InContainer(Weapon, 1.0f);
	if (!CheckGunMovingFire(Fixture)) return false;
	const int32 Ammo = Weapon->GetCurrentAmmo();
	const double ReadyAt = Fixture.World->GetTimeSeconds() + Weapon->GetAttackRetryDelay();
	if (!HearInvestigationNoise(Fixture, Fixture.Enemy->GetActorLocation() + FVector(0, 700, 0))
		|| !WaitForNoiseState(Fixture, false, 15)) return false;
	if (!Fixture.Test.TestFalse(TEXT("Saved investigation transition pauses the actual gun"), Weapon->IsAttacking())) return false;
	Fixture.Test.TestEqual(TEXT("Saved transition preserves ammunition during the cooldown"), Weapon->GetCurrentAmmo(), Ammo);
	Fixture.SetSight(true);
	if (!Fixture.Test.TestFalse(TEXT("Direct sight releases investigation ownership"), Fixture.Enemy->IsNoiseInvestigationActive())) return false;
	const double Deadline = ReadyAt + Fixture.Enemy->GetTargetAttackRecoveryRemaining() + 1.0;
	for (int32 Frame = 0; Frame < 300 && Fixture.World->GetTimeSeconds() < Deadline; ++Frame)
	{
		if (!Fixture.Advance()) return false;
		if (!Fixture.Test.TestTrue(TEXT("Saved reacquisition never repeats same-target preparation"),
			Fixture.Enemy->GetAttackPhase() != EEnemyAttackPhase::Telegraph)) return false;
		if (Fixture.World->GetTimeSeconds() < ReadyAt - 2.0 * Fixture.Step)
			if (!Fixture.Test.TestEqual(TEXT("Saved transition cannot fire before the original cooldown"), Weapon->GetCurrentAmmo(), Ammo)) return false;
		if (Weapon->GetCurrentAmmo() < Ammo)
			return Fixture.Test.TestTrue(TEXT("Saved transition resumes a real shot after the preserved cooldown"),
				Fixture.World->GetTimeSeconds() >= ReadyAt - 2.0 * Fixture.Step);
	}
	Fixture.Test.AddError(TEXT("Saved combat tree did not resume actual fire after direct reacquisition"));
	return false;
}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyNoiseStateTreeIntegrationTest,
	"Outlier.Enemy.Perception.Investigation.StateTree", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyNoiseStateTreeIntegrationTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("Gun.SoundToSearch"), TEXT("Melee.SoundToSearch"), TEXT("Gun.SharedExpiry"),
		TEXT("Gun.DirectReacquire"), TEXT("Gun.SearchSightClear"), TEXT("Gun.SearchSightBlocked"),
		TEXT("Gun.NoiseSearchSightClear"), TEXT("Gun.NoiseSearchSightBlocked"), TEXT("Gun.SearchFarRetarget"),
		TEXT("Gun.GeneralSearchNoiseHandoff"),
		TEXT("Alert.SoundToReturn"), TEXT("Melee.CommittedNoise")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyNoiseStateTreeIntegrationTest::RunTest(const FString& Parameters)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	FString Type, Scenario;
	if (!Parameters.Split(TEXT("."), &Type, &Scenario)) return false;
	const bool bAlert = Type == TEXT("Alert");
	const bool bMelee = Type == TEXT("Melee");
	FEnemyRoomCombatIntegrationWorld Fixture(*this, Parameters);
	if (bAlert)
	{
		if (!Fixture.Initialize(false, FVector(3500, 0, 0), false, false)) return false;
		if (!TestEqual(TEXT("Alert-only setup begins outside combat"), Fixture.Enemy->GetCombatState(), EEnemyCombatState::NonCombat)) return false;
		return CheckSavedNoiseFlow(Fixture, true);
	}
	if (!Fixture.InitializeShared(bMelee, FVector(Scenario == TEXT("CommittedNoise") ? 400.0f : 3500.0f, 0, 0),
		Scenario != TEXT("SoundToSearch"))) return false;
	if (Scenario == TEXT("SoundToSearch")) return CheckSavedNoiseFlow(Fixture, false);
	if (Scenario == TEXT("DirectReacquire")) return CheckSavedNoiseReacquire(Fixture);
	if (Scenario == TEXT("SearchFarRetarget")) return CheckSavedNoiseFarRetarget(Fixture);
	if (Scenario == TEXT("GeneralSearchNoiseHandoff")) return CheckSavedNoiseFarRetarget(Fixture, true);
	if (Scenario == TEXT("SearchSightClear") || Scenario == TEXT("SearchSightBlocked")
		|| Scenario == TEXT("NoiseSearchSightClear") || Scenario == TEXT("NoiseSearchSightBlocked"))
		return CheckSearchSightHandoff(Fixture, Scenario.StartsWith(TEXT("Noise")), Scenario.EndsWith(TEXT("Blocked")));
	Fixture.Start();
	if (Scenario == TEXT("CommittedNoise"))
	{
		if (!Fixture.WaitForTask<FEnemyChargeLastDirectionTask>()) return false;
		ASelfDestructDrone* Drone = CastChecked<ASelfDestructDrone>(Fixture.Enemy);
		const float Elapsed = Drone->GetCommittedChargeElapsedTime();
		const FVector Direction = Drone->GetActorForwardVector();
		if (!HearInvestigationNoise(Fixture, Fixture.Enemy->GetActorLocation() + FVector(0, 10, 0))) return false;
		for (int32 Frame = 0; Frame < 4; ++Frame)
		{
			if (!Fixture.Advance()) return false;
			if (!TestNotNull(TEXT("Closer sound cannot replace a committed charge task"), Fixture.Data<FEnemyChargeLastDirectionTask>())) return false;
			if (!TestFalse(TEXT("Committed charge never starts sound investigation"), Fixture.Enemy->IsNoiseInvestigationActive())) return false;
		}
		TestTrue(TEXT("Sound does not reset committed elapsed time"), Drone->GetCommittedChargeElapsedTime() > Elapsed);
		return TestTrue(TEXT("Sound does not redirect the committed charge"), FVector::DotProduct(Direction, Drone->GetActorForwardVector()) > 0.999);
	}
	if (Scenario == TEXT("SharedExpiry"))
	{
		if (!HearInvestigationNoise(Fixture, Fixture.Enemy->GetActorLocation() + FVector(700, 0, 0))
			|| !WaitForNoiseState(Fixture, false)) return false;
		const double Deadline = Fixture.World->GetTimeSeconds() + Fixture.Enemy->HeardNoiseLifetime + 1.0;
		for (int32 Frame = 0; Frame < 300 && Fixture.World->GetTimeSeconds() < Deadline; ++Frame)
		{
			if (!Fixture.Advance()) return false;
			if (Fixture.Enemy->IsNoiseInvestigationActive()) continue;
			TestEqual(TEXT("Expired sound restores the continuously observed shared target"),
				Fixture.Enemy->GetPerceptionGoal().Type, EEnemyPerceptionGoalType::Combat);
			return TestEqual(TEXT("Noise selection never erases the real shared player"),
				Fixture.Enemy->GetTrackedTarget(), static_cast<AActor*>(Fixture.Target));
		}
		AddError(TEXT("Saved tree did not return to live shared combat after sound expiry"));
		return false;
	}
	AddError(TEXT("Unknown saved noise StateTree scenario"));
	return false;
}

namespace
{
bool ApplyInvestigationHit(FEnemyCombatIntegrationWorld& Fixture, const FVector& Origin)
{
	FOutlierDamageRequest Request;
	Request.DamageAmount = 1.0f;
	Request.DamageTag = OutlierGameplayTags::Damage::Ranged();
	Request.bHasDamageOrigin = true;
	Request.DamageOrigin = Origin;
	Request.DamageCauser = Fixture.Target;
	Request.HitResult.ImpactPoint = Fixture.Enemy->GetActorLocation();
	return Fixture.Test.TestTrue(TEXT("Actual nonlethal damage is accepted"), Fixture.Enemy->ReceiveOutlierDamage(Request) > 0.0f);
}

bool WaitForHitState(FEnemyCombatIntegrationWorld& Fixture, bool bSearch, int32 MaxFrames = 1200)
{
	for (int32 Frame = 0; Frame < MaxFrames; ++Frame)
	{
		if (Fixture.Enemy->HasHitResponse())
		{
			if (bSearch)
			{
				if (Fixture.Data<FEnemySearchSessionTask>() && Fixture.Enemy->IsLocationSearchActive()) return true;
			}
			else if (const auto* Fly = Fixture.Data<FEnemyFlyToLocationTask>(); Fly && Fly->bInvestigateNoise
				&& Fixture.Enemy->IsLocationInvestigationActive() && !Fixture.Enemy->IsHitDirectionConfirmationActive()) return true;
		}
		if (!Fixture.Advance()) break;
	}
	Fixture.Test.AddError(FString::Printf(TEXT("Saved tree did not enter hit %s: %s"),
		bSearch ? TEXT("search") : TEXT("approach"), *Fixture.Snapshot.Describe()));
	return false;
}

bool CheckPistolHitRecovery(FEnemyRoomCombatIntegrationWorld& Fixture, const FString& Scenario)
{
	const bool bVisible = Scenario == TEXT("PistolVisibleRecovery");
	const float StunDuration = Scenario == TEXT("PistolNoStun") ? 0.0f : 0.3f;
	if (!Fixture.Initialize(false, FVector(bVisible ? 1200.0 : 3500.0, 0, 0), bVisible, bVisible)) return false;
	FEnemyStat& Stat = const_cast<FEnemyStat&>(Fixture.Enemy->GetRuntimeStat());
	Stat.PistolHitClueGraceSeconds = 1.0f;
	Fixture.Start();
	if (!Fixture.Advance()) return false;
	const FVector Origin = Fixture.Enemy->GetActorLocation() + FVector(-700, 0, 0);
	FOutlierDamageRequest Request;
	Request.DamageAmount = 1.0f;
	Request.DamageTag = OutlierGameplayTags::Damage::Ranged();
	Request.AdaptationDamageCategory = EOutlierAdaptationDamageCategory::Pistol;
	Request.StunDurationSeconds = StunDuration;
	Request.bHasDamageOrigin = true;
	Request.DamageOrigin = Origin;
	Request.DamageCauser = Fixture.Target;
	Request.HitResult.ImpactPoint = Fixture.Enemy->GetActorLocation();
	if (!Fixture.Test.TestTrue(TEXT("Actual nonlethal pistol damage is accepted"), Fixture.Enemy->ReceiveOutlierDamage(Request) > 0.0f)) return false;
	UEnemyRoomSubsystem* Rooms = Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
	if (!Fixture.Test.TestNotNull(TEXT("Pistol recovery uses the real room subsystem"), Rooms)) return false;
	if (StunDuration > 0.0f)
	{
		if (!Fixture.Test.TestEqual(TEXT("Pistol damage applies actual stun"), Fixture.Enemy->GetCombatState(), EEnemyCombatState::Stun)) return false;
		if (!bVisible)
		{
			FEnemyHitClue Pending;
			if (!Fixture.Test.TestTrue(TEXT("Stun retains the pistol hit snapshot for recovery"), Fixture.Enemy->TryGetPendingHitClue(Pending)
				&& Pending.Location.Equals(Origin, 1.0))
				|| !Fixture.Test.TestTrue(TEXT("Pistol clue includes the applied stun and configured grace"),
					FMath::IsNearlyEqual(Pending.ExpiresAt - Pending.OccurredAt, static_cast<double>(StunDuration + Stat.PistolHitClueGraceSeconds), 0.01))) return false;
		}
		// 상태를 직접 복구하지 않는다. 실제 월드 시간으로 GAS 효과가 만료되어 복귀하는 경로를 검증한다.
		int32 StunnedFrames = 0;
		for (; StunnedFrames < 120 && Fixture.Enemy->GetCombatState() == EEnemyCombatState::Stun; ++StunnedFrames)
		{
			if (!Fixture.Test.TestFalse(TEXT("Stun delays pistol direction confirmation"), Fixture.Enemy->HasHitResponse())) return false;
			if (!bVisible && !Fixture.Test.TestFalse(TEXT("A hidden pistol hit cannot start room combat during stun"), Rooms->IsRoomInCombat(Fixture.SearchRoomTag))) return false;
			if (!Fixture.Advance()) return false;
		}
		if (!Fixture.Test.TestTrue(TEXT("GAS stun expires through world ticking"), StunnedFrames > 0 && StunnedFrames < 120)) return false;
	}
	if (bVisible)
	{
		FEnemyHitClue Pending;
		if (!Fixture.Test.TestTrue(TEXT("Normal sight survives pistol stun recovery"), Fixture.Enemy->IsPlayerCurrentlyVisible())
			|| !Fixture.Test.TestEqual(TEXT("Visible recovery preserves combat"), Fixture.Enemy->GetCombatState(), EEnemyCombatState::Combat)
			|| !Fixture.Test.TestEqual(TEXT("Visible recovery keeps the actual visible target"), Fixture.Enemy->GetTrackedTarget(), static_cast<AActor*>(Fixture.Target))
			|| !Fixture.Test.TestFalse(TEXT("Visible recovery does not start a behind-hit investigation"), Fixture.Enemy->HasHitResponse())
			|| !Fixture.Test.TestFalse(TEXT("Visible recovery consumes the deferred pistol clue"), Fixture.Enemy->TryGetPendingHitClue(Pending))) return false;
		Fixture.SetSight(false);
		for (int32 Frame = 0; Frame < 30; ++Frame)
		{
			if (!Fixture.Advance()) return false;
			if (!Fixture.Test.TestFalse(TEXT("Sight loss cannot replay the consumed pistol clue"), Fixture.Enemy->HasHitResponse())) return false;
		}
		return true;
	}
	if (!Fixture.Test.TestEqual(TEXT("Unseen pistol recovery enters alert without discovery"), Fixture.Enemy->GetCombatState(), EEnemyCombatState::Alert)
		|| !Fixture.Test.TestTrue(TEXT("Unseen pistol recovery starts direction confirmation"), Fixture.Enemy->HasHitResponse())
		|| !Fixture.Test.TestFalse(TEXT("Pistol recovery cannot manufacture direct sight"), Fixture.Enemy->IsPlayerCurrentlyVisible())
		|| !Fixture.Test.TestFalse(TEXT("Pistol recovery cannot grant live actor tracking"), Fixture.Enemy->HasTrackableTarget())
		|| !Fixture.Test.TestFalse(TEXT("Pistol recovery cannot start room combat"), Rooms->IsRoomInCombat(Fixture.SearchRoomTag))) return false;
	if (!WaitForHitState(Fixture, false)) return false;
	const auto* Fly = Fixture.Data<FEnemyFlyToLocationTask>();
	if (!Fixture.Test.TestTrue(TEXT("Recovered pistol investigation flies toward the hit-time snapshot"), Fly && Fly->Destination.Equals(Origin, 1.0))) return false;
	Fixture.Target->SetActorLocation(Origin + FVector(3000, 1000, 0));
	if (!WaitForHitState(Fixture, true)) return false;
	return Fixture.Test.TestTrue(TEXT("Pistol investigation reaches the snapshot before searching"), Fixture.Enemy->GetSearchEpisode().Center.Equals(Origin, 120.0))
		&& Fixture.Test.TestEqual(TEXT("Pistol snapshot search remains alert"), Fixture.Enemy->GetCombatState(), EEnemyCombatState::Alert)
		&& Fixture.Test.TestFalse(TEXT("Pistol snapshot search cannot promote room combat"), Rooms->IsRoomInCombat(Fixture.SearchRoomTag))
		&& Fixture.Test.TestFalse(TEXT("Snapshot search does not follow the hidden attacker's current position"), Fixture.Enemy->HasTrackableTarget());
}

bool CheckHitSightHandoff(FEnemyCombatIntegrationWorld& Fixture, bool bBlocked, bool bPartial)
{
	const uint64 OldSerial = Fixture.Enemy->GetLocationInvestigationSerial();
	const FVector Location = Fixture.Enemy->GetActorLocation();
	const FVector Observed = Location + FVector(0, 1200, 0);
	Fixture.Target->SetActorLocation(Observed);
	AActor* Wall = bBlocked ? Fixture.SpawnBlockingBox(Location + FVector(0, 600, 0), FVector(2000, 80, 1000)) : nullptr;
	if (bBlocked && !Fixture.Test.TestNotNull(TEXT("Physical attack-path blocker exists"), Wall)) return false;
	ARangedWeaponBase* Weapon = Fixture.Enemy->GetCurrentWeapon();
	if (!Fixture.Test.TestNotNull(TEXT("Actual attack weapon exists"), Weapon)) return false;
	FBoolProperty* InfiniteAmmo = FindFProperty<FBoolProperty>(Weapon->GetClass(), TEXT("bInfiniteAmmo"));
	FFloatProperty* Range = FindFProperty<FFloatProperty>(Weapon->GetClass(), TEXT("EffectiveRange"));
	if (!Fixture.Test.TestNotNull(TEXT("Hit handoff ammo mode is inspectable"), InfiniteAmmo)
		|| !Fixture.Test.TestNotNull(TEXT("Hit handoff attack range is inspectable"), Range)) return false;
	// 적 BP의 무한 탄약 설정을 꺼야 Firing 상태 표시가 아닌 실제 발사를 탄약 감소로 확인할 수 있다.
	InfiniteAmmo->SetPropertyValue_InContainer(Weapon, false);
	Range->SetPropertyValue_InContainer(Weapon, 5000.0f);
	Weapon->RefillMagazine();
	const int32 Ammo = Weapon->GetCurrentAmmo();
	// 감지 입력만 전달한다. 피격 조사 해제와 저장된 StateTree의 전투 전이는 런타임이 수행한다.
	Fixture.SetSight(true);
	if (!Fixture.Test.TestFalse(TEXT("Direct sight immediately invalidates the old investigation"),
		Fixture.Enemy->IsLocationInvestigationCurrent(OldSerial))) return false;
	if (bPartial)
	{
		Fixture.SetSight(false);
		Fixture.Target->SetActorLocation(Observed + FVector(2000, 0, 0));
		if (!Fixture.Test.TestTrue(TEXT("Brief sight keeps the actual observed snapshot"),
			Fixture.Enemy->GetPerceptionGoal().Location.Equals(Observed, 1.0))) return false;
	}
	else if (!Fixture.Test.TestFalse(TEXT("Confirmed combat sight releases hit ownership"), Fixture.Enemy->HasHitResponse())) return false;
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		if (!Fixture.Advance()) return false;
		if (!Fixture.Test.TestFalse(TEXT("Old movement callbacks never restore the hit investigation"),
			Fixture.Enemy->IsLocationInvestigationCurrent(OldSerial))) return false;
		if (bPartial)
		{
			if (!Fixture.Test.TestTrue(TEXT("Partial observation cannot force combat"),
				Fixture.Enemy->GetCombatState() != EEnemyCombatState::Combat)) return false;
			continue;
		}
		if (!Fixture.Test.TestNull(TEXT("Combat releases the old search session"), Fixture.Data<FEnemySearchSessionTask>())
			|| !Fixture.Test.TestNull(TEXT("Combat releases the old look-around task"), Fixture.Data<FEnemyLookAroundTask>())
			|| !Fixture.Test.TestFalse(TEXT("Combat cannot restore the old search episode"), Fixture.Enemy->GetSearchEpisode().bActive)) return false;
		const auto* Fly = Fixture.Data<FEnemyFlyToLocationTask>();
		if (!Fixture.Test.TestTrue(TEXT("Combat does not resume the old investigation flight"), !Fly || !Fly->bInvestigateNoise)) return false;
		if (bBlocked && !Fixture.Test.TestEqual(TEXT("A blocked shot consumes no ammunition"), Weapon->GetCurrentAmmo(), Ammo)) return false;
	}
	if (bPartial) return true;
	if (Wall) Wall->SetActorEnableCollision(false);
	if (Weapon->GetCurrentAmmo() < Ammo) return true;
	for (int32 Frame = 0; Frame < 300; ++Frame)
	{
		if (!Fixture.Advance()) return false;
		if (Weapon->GetCurrentAmmo() < Ammo) return true;
	}
	Fixture.Test.AddError(TEXT("Combat did not resume an actual shot after hit investigation handoff"));
	return false;
}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyHitStateTreeIntegrationTest,
	"Outlier.Enemy.HitResponse.StateTree", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyHitStateTreeIntegrationTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("AlertToSearch"), TEXT("CombatWithTracking"), TEXT("SearchSightClear"),
		TEXT("SearchSightBlocked"), TEXT("MoveSight"), TEXT("PartialSight"), TEXT("FailedPath"),
		TEXT("RetargetApproach"), TEXT("RetargetSearch"), TEXT("DirectVisiblePriority"), TEXT("AlertHearingPriority"),
		TEXT("PistolStunToSearch"), TEXT("PistolNoStun"), TEXT("PistolVisibleRecovery")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyHitStateTreeIntegrationTest::RunTest(const FString& Parameters)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	const bool bAlert = Parameters == TEXT("AlertToSearch") || Parameters == TEXT("PartialSight")
		|| Parameters == TEXT("AlertHearingPriority");
	FEnemyRoomCombatIntegrationWorld Fixture(*this, Parameters);
	if (Parameters.StartsWith(TEXT("Pistol"))) return CheckPistolHitRecovery(Fixture, Parameters);
	if (Parameters == TEXT("DirectVisiblePriority"))
	{
		if (!Fixture.Initialize(false, FVector(1200, 0, 0), true, true)) return false;
		Fixture.Start();
		if (!Fixture.Advance() || !ApplyInvestigationHit(Fixture, Fixture.Enemy->GetActorLocation() + FVector(-700, 0, 0))) return false;
		FEnemyHitClue Pending;
		if (!TestFalse(TEXT("Directly visible combat suppresses the behind-hit response"), Fixture.Enemy->HasHitResponse())
			|| !TestFalse(TEXT("Suppressed hit cannot be replayed after sight loss"), Fixture.Enemy->TryGetPendingHitClue(Pending))) return false;
		return TestEqual(TEXT("Behind hit retains the directly visible combat target"),
			Fixture.Enemy->GetTrackedTarget(), static_cast<AActor*>(Fixture.Target));
	}
	if (bAlert)
	{
		if (!Fixture.Initialize(false, FVector(3500, 0, 0), false, false)) return false;
	}
	else if (!Fixture.InitializeShared(false, FVector(3500, 0, 0), Parameters == TEXT("CombatWithTracking"))) return false;
	if (Parameters == TEXT("CombatWithTracking")
		&& !TestEqual(TEXT("A shared target remains valid before the hit"), Fixture.Enemy->GetTrackedTarget(), static_cast<AActor*>(Fixture.Target))) return false;
	const FVector Start = Fixture.Enemy->GetActorLocation();
	const FVector Origin = Start + FVector(-700, 0, 0);
	AActor* Blocker = Parameters == TEXT("FailedPath")
		? Fixture.SpawnBlockingBox(Start + FVector(-350, 0, 0), FVector(80, 6000, 2500)) : nullptr;
	if (Parameters == TEXT("FailedPath") && !TestNotNull(TEXT("Sealed route blocker exists"), Blocker)) return false;
	Fixture.Start();
	if (!ApplyInvestigationHit(Fixture, Origin) || !WaitForHitState(Fixture, false)) return false;
	const auto* Fly = Fixture.Data<FEnemyFlyToLocationTask>();
	if (!TestTrue(TEXT("Saved investigation flight targets the hit snapshot"), Fly && Fly->Destination.Equals(Origin, 1.0))
		|| !TestFalse(TEXT("Hit does not create hearing evidence"), Fixture.Enemy->IsNoiseInvestigationActive())
		|| !TestFalse(TEXT("Hit does not grant a shot at the hidden actor"), Fixture.Enemy->CanAttackTrackedTarget(Fixture.Target))) return false;
	Fixture.Target->SetActorLocation(Start + FVector(4500, 1000, 0));
	if (Parameters == TEXT("MoveSight")) return CheckHitSightHandoff(Fixture, false, false);
	if (Parameters == TEXT("RetargetApproach") || Parameters == TEXT("RetargetSearch"))
	{
		if (Parameters == TEXT("RetargetSearch") && !WaitForHitState(Fixture, true)) return false;
		const uint64 PreviousSerial = Fixture.Enemy->GetLocationInvestigationSerial();
		const FVector Replacement = Fixture.Enemy->GetActorLocation() + FVector(0, 700, 0);
		if (!ApplyInvestigationHit(Fixture, Replacement) || !WaitForHitState(Fixture, false)) return false;
		const auto* ReplacementFlight = Fixture.Data<FEnemyFlyToLocationTask>();
		if (!TestTrue(TEXT("A fresh hit replaces the active approach/search with a new flight"),
			ReplacementFlight && ReplacementFlight->Destination.Equals(Replacement, 1.0))
			|| !TestTrue(TEXT("Replacement owns a new investigation serial"),
				Fixture.Enemy->GetLocationInvestigationSerial() != PreviousSerial)) return false;
		if (!WaitForHitState(Fixture, true)) return false;
		return TestTrue(TEXT("Replacement flight really reaches its own source before search"),
			Fixture.Enemy->GetSearchEpisode().Center.Equals(Replacement, 120.0));
	}
	if (!WaitForHitState(Fixture, true)) return false;
	const FVector Center = Fixture.Enemy->GetSearchEpisode().Center;
	if (Parameters == TEXT("FailedPath"))
	{
		if (!TestTrue(TEXT("Failed route searches only the physically reached side of the wall"), Center.X > Start.X - 350.0)
			|| !TestFalse(TEXT("Blocked origin is not reported as reached"), Center.Equals(Origin, 100.0))) return false;
	}
	else if (!TestTrue(TEXT("A hit investigation actually approaches its source before search"),
		FVector::Distance(Start, Center) > 400.0 && Center.Equals(Origin, 120.0))) return false;
	if (Parameters == TEXT("SearchSightClear") || Parameters == TEXT("SearchSightBlocked") || Parameters == TEXT("PartialSight"))
	{
		if (!Fixture.WaitForTask<FEnemyLookAroundTask>()) return false;
		return CheckHitSightHandoff(Fixture, Parameters == TEXT("SearchSightBlocked"), Parameters == TEXT("PartialSight"));
	}
	if (Parameters == TEXT("AlertHearingPriority"))
	{
		FEnemyStat& Stat = const_cast<FEnemyStat&>(Fixture.Enemy->GetRuntimeStat());
		Stat.AwarenessFillDuration = 0.1f;
		Stat.HearingAwarenessRatio = 0.25f;
		Stat.HearingAwarenessHoldDuration = 0.5f;
		// 이번 검증은 만료가 아닌 우선순위다. 수색 종료 시에도 소리 후보가 유효하도록 수명만 늘린다.
		Fixture.Enemy->HeardNoiseLifetime = 10.0f;
		const FVector HeardLocation = Center + FVector(0, 700, 0);
		const uint64 HitSerial = Fixture.Enemy->GetLocationInvestigationSerial();
		UEnemyRoomSubsystem* Rooms = Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
		if (!TestNotNull(TEXT("Hearing priority has a real room perception subsystem"), Rooms)) return false;
		// 0.1초 충전·25% 청각이면 0.4초에 인지가 완료된다. 1초 동안 실제 청각 자극을 반복한다.
		for (int32 Frame = 0; Frame < 60; ++Frame)
		{
			if (Frame % 6 == 0 && !HearInvestigationNoise(Fixture, HeardLocation)) return false;
			if (!Fixture.Advance()) return false;
			if (!TestTrue(TEXT("Completed hearing awareness cannot interrupt the active hit search"),
				Fixture.Enemy->HasHitResponse() && Fixture.Enemy->IsLocationSearchActive())
				|| !TestEqual(TEXT("Repeated hearing keeps the active hit serial"), Fixture.Enemy->GetLocationInvestigationSerial(), HitSerial)
				|| !TestTrue(TEXT("Repeated hearing cannot move the hit search center"), Fixture.Enemy->GetSearchEpisode().Center.Equals(Center, 1.0))
				|| !TestEqual(TEXT("Hearing completion cannot promote Alert during hit search"), Fixture.Enemy->GetCombatState(), EEnemyCombatState::Alert)
				|| !TestFalse(TEXT("Buffered hearing cannot start room combat during hit search"), Rooms->IsRoomInCombat(Fixture.SearchRoomTag))) return false;
		}
		FEnemyHeardNoise Buffered;
		if (!TestTrue(TEXT("Lower priority hearing remains buffered"), Fixture.Enemy->TryGetHeardNoise(Buffered)
			&& Buffered.Location.Equals(HeardLocation, 1.0))) return false;
		for (int32 Frame = 0; Frame < 900; ++Frame)
		{
			if (!Fixture.Advance()) return false;
			if (Fixture.Enemy->HasHitResponse())
			{
				if (!TestEqual(TEXT("No further sound restarts the hit investigation"), Fixture.Enemy->GetLocationInvestigationSerial(), HitSerial)) return false;
				continue;
			}
			const FEnemyPerceptionGoal& Goal = Fixture.Enemy->GetPerceptionGoal();
			if (!TestTrue(TEXT("Hit completion reevaluates the still-valid buffered hearing"),
				Goal.Type == EEnemyPerceptionGoalType::InvestigateNoise || Fixture.Enemy->GetCombatState() == EEnemyCombatState::Combat)
				|| !TestTrue(TEXT("Hearing handoff uses the heard snapshot"), Goal.Location.Equals(HeardLocation, 1.0)
					|| Fixture.Enemy->GetLastKnownPlayerLocation().Equals(HeardLocation, 1.0))
				|| !TestFalse(TEXT("Hearing handoff cannot manufacture direct sight"), Fixture.Enemy->IsPlayerCurrentlyVisible())) return false;
			return TestFalse(TEXT("Hearing handoff cannot grant live actor tracking"), Fixture.Enemy->HasTrackableTarget());
		}
		AddError(TEXT("Hit search did not finish and reevaluate buffered hearing"));
		return false;
	}
	const uint64 Serial = Fixture.Enemy->GetLocationInvestigationSerial();
	for (int32 Frame = 0; Frame < 900; ++Frame)
	{
		if (!Fixture.Advance()) return false;
		if (Fixture.Enemy->HasHitResponse()) continue;
		if (!TestFalse(TEXT("Completed hit investigation cannot be completed twice"), Fixture.Enemy->IsLocationInvestigationCurrent(Serial))) return false;
		if (bAlert) return TestEqual(TEXT("Unobserved alert investigation returns to noncombat"), Fixture.Enemy->GetCombatState(), EEnemyCombatState::NonCombat);
		return TestEqual(TEXT("Individual hit search does not end room combat"), Fixture.Enemy->GetCombatState(), EEnemyCombatState::Combat);
	}
	AddError(TEXT("Saved hit investigation did not finish within its bounded search duration"));
	return false;
}

namespace
{
bool CheckCommittedHitException(FEnemyRoomCombatIntegrationWorld& Fixture)
{
	FAutomationTestBase& Test = Fixture.Test;
	Fixture.Start();
	if (!Fixture.WaitForTask<FEnemyChargeLastDirectionTask>()) return false;
	ASelfDestructDrone* Drone = CastChecked<ASelfDestructDrone>(Fixture.Enemy);
	const FVector Direction = Drone->GetActorForwardVector();
	const double StartedAt = Fixture.World->GetTimeSeconds() - Drone->GetCommittedChargeElapsedTime();
	Fixture.SetSight(false);
	if (!ApplyInvestigationHit(Fixture, Drone->GetActorLocation() + FVector(0, -700, 0))) return false;
	int32 Frames = 0;
	for (; Frames < 360; ++Frames)
	{
		if (!Fixture.Advance()) break;
		if (!Test.TestTrue(TEXT("Nonlethal damage preserves committed charge"), Drone->IsSelfDestructCommitted())
			|| !Test.TestTrue(TEXT("Hit cannot steer committed charge"), FVector::DotProduct(Direction, Drone->GetActorForwardVector()) > 0.999)
			|| !Test.TestFalse(TEXT("Committed charge cannot start hit investigation"), Drone->HasHitResponse())
			|| !Test.TestTrue(TEXT("Hit cannot restart the charge clock"), FMath::IsNearlyEqual(
				static_cast<double>(Drone->GetCommittedChargeElapsedTime()), Fixture.World->GetTimeSeconds() - StartedAt, 0.002))) return false;
	}
	return Test.TestTrue(TEXT("The original charge completes rather than silently canceling"), Frames > 2 && Drone->IsDead());
}

bool CheckReturningHitException(FEnemyRoomCombatIntegrationWorld& Fixture, const FString& Parameters)
{
	FAutomationTestBase& Test = Fixture.Test;
	FEnemyStat& Stat = const_cast<FEnemyStat&>(Fixture.Enemy->GetRuntimeStat());
	// 실제 방 밖에서 기존 귀환 전이를 시작한다. 피격마다 경로가 다시 만들어지는지도 함께 확인한다.
	CastChecked<UBoxComponent>(Fixture.Room->GetRootComponent())->SetBoxExtent(FVector(500, 500, 2500));
	// 귀환 이동은 같은 그룹의 등록된 방들만 통과한다. 실제 형제 방을 연결해 경로를 준비한다.
	FRoomCombatRoomDefinition& SiblingDefinition = Fixture.CombatDefinition->RoomDefinitions.AddDefaulted_GetRef();
	SiblingDefinition.RoomTag = EnemyRoomReturnTestTags::ReturnTreeSibling;
	SiblingDefinition.CombatPhases.AddDefaulted();
	ARoomVolume* Sibling = Fixture.World->SpawnActor<ARoomVolume>();
	if (!Test.TestNotNull(TEXT("Returning sibling room exists"), Sibling)) return false;
	Sibling->SetActorLocation(FVector(1250, 0, 0));
	CastChecked<UBoxComponent>(Sibling->GetRootComponent())->SetBoxExtent(FVector(850, 500, 2500));
	if (!Test.TestTrue(TEXT("Returning sibling room registers"), Fixture.World->GetSubsystem<URoomCombatSubsystem>()
		->RegisterRoom(Sibling, EnemyRoomReturnTestTags::ReturnTreeSibling))) return false;
	Fixture.Enemy->SetActorLocation(Fixture.Enemy->GetActorLocation() + FVector(1800, 0, 0));
	Stat.HitDirectionMinDuration = 0.1f;
	Stat.HitDirectionMaxDuration = 0.2f;
	Fixture.Enemy->EnterAlertFromPerception(Fixture.Target->GetActorLocation());
	Fixture.Enemy->ClearAlertClue();
	if (!Test.TestTrue(TEXT("Outside-home alert transitions through the production return rule"), Fixture.Enemy->CommitAlertToNonCombat())) return false;
	Fixture.Start();
	if (!Fixture.WaitForTask<FEnemyFlyToLocationTask>()) return false;
	const auto* Fly = Fixture.Data<FEnemyFlyToLocationTask>();
	if (!Test.TestTrue(TEXT("Actual return flight is active"), Fly && Fly->bReturnHome)) return false;
	const FVector Destination = Fly->Destination;
	const TArray<FVector> Route = Fly->PathWaypoints;
	float Elapsed = Fly->MoveElapsed;
	const FVector Start = Fixture.Enemy->GetActorLocation();
	bool bSawConfirmation = false;
	for (int32 Frame = 0; Frame < 24; ++Frame)
	{
		if (Frame % 6 == 0 && !ApplyInvestigationHit(Fixture, Start + FVector(0, -700, 0))) return false;
		bSawConfirmation |= Fixture.Enemy->IsHitDirectionConfirmationActive();
		if (!Fixture.Advance()) return false;
		Fly = Fixture.Data<FEnemyFlyToLocationTask>();
		if (!Test.TestTrue(TEXT("Hits preserve the actual return task and route"), Fly && Fly->bReturnHome
			&& Fly->Destination.Equals(Destination) && Fly->PathWaypoints == Route)
			|| !Test.TestTrue(TEXT("Return movement clock keeps progressing through repeated hits"), Fly->MoveElapsed > Elapsed)
			|| !Test.TestFalse(TEXT("Returning hits cannot create a search episode"), Fixture.Enemy->GetSearchEpisode().bActive)) return false;
		Elapsed = Fly->MoveElapsed;
	}
	if (!Test.TestTrue(TEXT("Returning damage actually starts direction confirmation"), bSawConfirmation)
		|| !Test.TestFalse(TEXT("Direction-only return response finishes without search"), Fixture.Enemy->HasHitResponse())
		|| !Test.TestTrue(TEXT("Return physically progresses while checking hit direction"), FVector::Dist(Start, Fixture.Enemy->GetActorLocation()) > 30)) return false;
	if (Parameters == TEXT("ReturningSight"))
	{
		Fixture.Target->SetActorLocation(Fixture.Enemy->GetActorLocation() + FVector(400, 0, 0));
		// Arena 레벨 없는 임시 월드에서는 방 전투만 입력으로 준비한다. 실제 재발견의 합류·귀환 중단은 트리가 처리한다.
		Fixture.World->GetSubsystem<UEnemyRoomSubsystem>()->SetActiveRoomTargetForTesting(
			Fixture.SearchRoomTag, Fixture.Target->GetActorLocation(), false);
		Fixture.SetSight(true);
		for (int32 Frame = 0; Frame < 180; ++Frame)
		{
			if (!Fixture.Advance()) return false;
			Fly = Fixture.Data<FEnemyFlyToLocationTask>();
			if (Fixture.Enemy->GetCombatState() == EEnemyCombatState::Combat && (!Fly || !Fly->bReturnHome))
				return Test.TestFalse(TEXT("Rediscovery releases return hit ownership"), Fixture.Enemy->HasHitResponse());
		}
		Test.AddError(TEXT("Rediscovery did not interrupt actual return flight"));
		return false;
	}
	for (int32 Frame = 0; Frame < 900; ++Frame)
	{
		if (!Fixture.Advance()) return false;
		if (Fixture.Enemy->GetCombatState() == EEnemyCombatState::NonCombat)
			return Test.TestTrue(TEXT("Hit-interrupted return ends only after physical home arrival"), Fixture.Enemy->IsInsideDefaultRoom());
	}
	Test.AddError(TEXT("Return did not finish after repeated hits"));
	return false;
}

bool CheckPooledHitException(FEnemyRoomCombatIntegrationWorld& Fixture)
{
	FAutomationTestBase& Test = Fixture.Test;
	UEnemyPoolSubsystem* Pool = Fixture.World->GetSubsystem<UEnemyPoolSubsystem>();
	if (!Test.TestNotNull(TEXT("Real pool subsystem"), Pool)) return false;
	UClass* Class = Fixture.Enemy->GetClass();
	const FTransform Transform = Fixture.Enemy->GetActorTransform();
	Fixture.Enemy->Destroy();
	Fixture.Tree = nullptr;
	UEnemyPoolDefinition* Definition = NewObject<UEnemyPoolDefinition>(Fixture.World);
	FEnemyPoolEntry& Entry = Definition->Entries.AddDefaulted_GetRef();
	Entry.EnemyClass = Class;
	Entry.PrewarmCount = Entry.MaxCount = 1;
	if (!Test.TestTrue(TEXT("Hit response pool prewarms production BP"), Pool->PrewarmPool(Definition))) return false;
	FEnemyPoolLeaseContext Lease;
	Lease.RoomTag = Fixture.SearchRoomTag;
	Lease.GameplayGeneration = 4100;
	Fixture.Enemy = Pool->LeaseEnemy(Class, Transform, Lease);
	if (!Test.TestNotNull(TEXT("First hit-response lease"), Fixture.Enemy)) return false;
	Fixture.Enemy->SetPoolPresentationAutoCompleteForTesting(false);
	const int32 OldLease = Fixture.Enemy->GetPoolLeaseSerial();
	Fixture.Enemy->CompletePoolSpawnPresentation(Lease.GameplayGeneration, OldLease);
	Fixture.Controller = CastChecked<AEnemyAIController>(Fixture.Enemy->GetController());
	Fixture.Tree = CastChecked<UEnemyStateTreeComponent>(Fixture.Enemy->GetStateTreeComponent());
	Fixture.Controller->SetEnemyPerceptionEnabled(false);
	if (!ApplyInvestigationHit(Fixture, Transform.GetLocation() + FVector(-700, 0, 0)) || !WaitForHitState(Fixture, false)) return false;
	const uint64 OldInvestigation = Fixture.Enemy->GetLocationInvestigationSerial();
	AEnemyBase* First = Fixture.Enemy;
	First->SimulateDeathPresentationForPoolTesting();
	if (!Test.TestTrue(TEXT("Actual pool accepts the old investigating lease"), Pool->ReturnEnemy(First, Lease.GameplayGeneration, OldLease))) return false;
	++Lease.GameplayGeneration;
	Fixture.Enemy = Pool->LeaseEnemy(Class, Transform, Lease);
	if (!Test.TestTrue(TEXT("Actual pool reuses the investigating actor"), Fixture.Enemy == First)) return false;
	Fixture.Enemy->CompletePoolSpawnPresentation(Lease.GameplayGeneration - 1, OldLease);
	if (!Test.TestFalse(TEXT("Old investigation callback cannot survive actual reuse"), Fixture.Enemy->IsLocationInvestigationCurrent(OldInvestigation))
		|| !Test.TestFalse(TEXT("New lease has no old hit response"), Fixture.Enemy->HasHitResponse())) return false;
	Fixture.Enemy->CompletePoolSpawnPresentation(Lease.GameplayGeneration, Fixture.Enemy->GetPoolLeaseSerial());
	Fixture.Controller = CastChecked<AEnemyAIController>(Fixture.Enemy->GetController());
	Fixture.Tree = CastChecked<UEnemyStateTreeComponent>(Fixture.Enemy->GetStateTreeComponent());
	Fixture.Controller->SetEnemyPerceptionEnabled(false);
	if (!ApplyInvestigationHit(Fixture, Transform.GetLocation() + FVector(0, 700, 0)) || !WaitForHitState(Fixture, true)) return false;
	return Test.TestFalse(TEXT("Fresh investigation never revives the previous lease callback"), Fixture.Enemy->IsLocationInvestigationCurrent(OldInvestigation));
}

bool CheckHitUpperStateHandoff(FEnemyRoomCombatIntegrationWorld& Fixture, const FString& Parameters, uint64 Serial)
{
	FAutomationTestBase& Test = Fixture.Test;
	if (Parameters.EndsWith(TEXT("Death")))
	{
		FOutlierDamageRequest LethalDamage;
		LethalDamage.DamageAmount = 10000.0f;
		LethalDamage.DamageTag = OutlierGameplayTags::Damage::Ranged();
		Fixture.Enemy->ReceiveOutlierDamage(LethalDamage);
		if (!Test.TestTrue(TEXT("Actual damage terminates the investigating enemy"), Fixture.Enemy->IsDead())) return false;
	}
	else if (Parameters.EndsWith(TEXT("Possession")))
	{
		APlayerController* Player = Fixture.World->SpawnActor<APlayerController>();
		if (!Test.TestNotNull(TEXT("Player controller for real possession"), Player)) return false;
		Player->Possess(Fixture.Enemy);
		if (!Test.TestTrue(TEXT("Real possession suppresses enemy AI"), Fixture.Enemy->IsEnemyPossessed())) return false;
	}
	else
	{
		AEnemyAIController* NextAI = Fixture.World->SpawnActor<AEnemyAIController>();
		if (!Test.TestNotNull(TEXT("Replacement live AI"), NextAI)) return false;
		NextAI->Possess(Fixture.Enemy);
		Fixture.Controller = NextAI;
		NextAI->SetEnemyPerceptionEnabled(false);
	}
	// 종료·빙의는 트리를 멈출 수 있으므로 Running을 요구하지 않고 실제 월드를 진행해 지연 정리까지 확인한다.
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		Fixture.Tick(Fixture.Step);
		Fixture.Observe();
		if (!Test.TestFalse(TEXT("Upper-state handoff cannot restore the old investigation"), Fixture.Enemy->IsLocationInvestigationCurrent(Serial))
			|| !Test.TestFalse(TEXT("Upper-state handoff releases hit ownership"), Fixture.Enemy->HasHitResponse())
			|| !Test.TestFalse(TEXT("Upper-state handoff releases the old search episode"), Fixture.Enemy->GetSearchEpisode().bActive)) return false;
	}
	if (Parameters == TEXT("ControllerReplacement"))
		return ApplyInvestigationHit(Fixture, Fixture.Enemy->GetActorLocation() + FVector(0, 700, 0)) && WaitForHitState(Fixture, false);
	return true;
}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyHitExceptionIntegrationTest,
	"Outlier.Enemy.HitResponse.Exceptions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyHitExceptionIntegrationTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("MeleeApproachSight"), TEXT("MeleeSearchSight"), TEXT("CommittedHit"),
		TEXT("ApproachStun"), TEXT("SearchStun"), TEXT("ActiveExpiry"), TEXT("SharedValid"), TEXT("SharedExpired"),
		TEXT("Returning"), TEXT("ReturningSight"), TEXT("PoolReuse"), TEXT("ApproachDeath"), TEXT("SearchDeath"),
		TEXT("ApproachPossession"), TEXT("SearchPossession"), TEXT("ControllerReplacement")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyHitExceptionIntegrationTest::RunTest(const FString& Parameters)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	FEnemyRoomCombatIntegrationWorld Fixture(*this, Parameters);
	const bool bMelee = Parameters.StartsWith(TEXT("Melee")) || Parameters == TEXT("CommittedHit");
	const bool bReturning = Parameters.StartsWith(TEXT("Returning"));
	const bool bShared = Parameters.StartsWith(TEXT("Shared")) || Parameters.StartsWith(TEXT("Melee"));
	if (bReturning)
	{
		Fixture.SearchRoomTag = EnemyRoomReturnTestTags::ReturnTreeHome;
		Fixture.CombatDefinition->RoomDefinitions[0].RoomTag = Fixture.SearchRoomTag;
	}
	if (bShared)
	{
		if (!Fixture.InitializeShared(bMelee, FVector(3500, 0, 0), true)) return false;
	}
	else if (!Fixture.Initialize(bMelee, FVector(Parameters == TEXT("CommittedHit") ? 400 : 3500, 0, 0),
		Parameters == TEXT("CommittedHit"), Parameters == TEXT("CommittedHit"))) return false;

	FEnemyStat& Stat = const_cast<FEnemyStat&>(Fixture.Enemy->GetRuntimeStat());
	Stat.HitSearchDuration = 1.0f;
	if (Parameters == TEXT("ActiveExpiry"))
	{
		// 대기 수명은 새 조사 허용에만 사용한다. 실제 이동 전에 만료되어도 시작한 조사는 끝까지 수행한다.
		Stat.HitClueLifetime = 0.15f;
		Stat.HitDirectionMinDuration = 0.05f;
		Stat.HitDirectionMaxDuration = 0.1f;
	}
	if (Parameters == TEXT("CommittedHit")) return CheckCommittedHitException(Fixture);
	if (bReturning) return CheckReturningHitException(Fixture, Parameters);
	if (Parameters == TEXT("PoolReuse")) return CheckPooledHitException(Fixture);

	if (Parameters == TEXT("SharedExpired")) Fixture.Rooms->RemoveRoomTargetObserver(Fixture.Observer);
	Fixture.Start();
	const FVector Origin = Fixture.Enemy->GetActorLocation() + FVector(-700, 0, 0);
	if (!ApplyInvestigationHit(Fixture, Origin) || !WaitForHitState(Fixture, false)) return false;
	if (Parameters.StartsWith(TEXT("Search")) || Parameters == TEXT("MeleeSearchSight"))
		if (!WaitForHitState(Fixture, true)) return false;
	const uint64 Serial = Fixture.Enemy->GetLocationInvestigationSerial();
	if (Parameters.EndsWith(TEXT("Death")) || Parameters.EndsWith(TEXT("Possession")) || Parameters == TEXT("ControllerReplacement"))
		return CheckHitUpperStateHandoff(Fixture, Parameters, Serial);
	if (Parameters.EndsWith(TEXT("Stun")))
	{
		// 실제 상위 StateTree 전이가 조사 Task를 내보낸 뒤에도 이전 회차가 다시 실행되지 않아야 한다.
		Fixture.Enemy->EnterStun();
		if (!Fixture.Advance()) return false;
		if (!TestFalse(TEXT("Stun cancels the active hit action"), Fixture.Enemy->HasHitResponse())
			|| !TestNull(TEXT("Stun exits the old search session"), Fixture.Data<FEnemySearchSessionTask>())
			|| !TestNull(TEXT("Stun exits the old look-around task"), Fixture.Data<FEnemyLookAroundTask>())
			|| !TestNull(TEXT("Stun exits the old flight"), Fixture.Data<FEnemyFlyToLocationTask>())) return false;
		Fixture.Enemy->RestoreStateAfterStun();
		for (int32 Frame = 0; Frame < 60; ++Frame)
		{
			if (!Fixture.Advance()) return false;
			if (!TestFalse(TEXT("Recovery cannot restore interrupted investigation callbacks"), Fixture.Enemy->IsLocationInvestigationCurrent(Serial))) return false;
		}
		return true;
	}
	if (bMelee)
	{
		ASelfDestructDrone* Drone = CastChecked<ASelfDestructDrone>(Fixture.Enemy);
		Fixture.Target->SetActorLocation(Drone->GetActorLocation() + FVector(400, 0, 0));
		for (int32 Frame = 0; Frame < 12; ++Frame)
		{
			if (!Fixture.Advance()) return false;
			if (!TestFalse(TEXT("Old shared target in range cannot start charge during hit investigation"), Drone->IsSelfDestructCommitted())) return false;
		}
		Fixture.SetSight(true);
		if (!Fixture.WaitForTask<FEnemyChargeLastDirectionTask>()) return false;
		return TestTrue(TEXT("Actual rediscovery permits normal charge without waiting for search completion"), Drone->IsSelfDestructCommitted())
			&& TestFalse(TEXT("Charge handoff consumes the old hit action"), Drone->HasHitResponse())
			&& TestNull(TEXT("Charge handoff releases look-around rotation"), Fixture.Data<FEnemyLookAroundTask>());
	}
	bool bObservedExpiredActive = false;
	for (int32 Frame = 0; Frame < 1500; ++Frame)
	{
		if (!Fixture.Advance()) return false;
		if (Fixture.Enemy->HasHitResponse())
		{
			bObservedExpiredActive |= Parameters == TEXT("ActiveExpiry") && Fixture.Enemy->IsLocationSearchActive();
			if (!TestEqual(TEXT("Old shared tracking cannot replace hit ownership"), Fixture.Enemy->GetLocationInvestigationSerial(), Serial)) return false;
			continue;
		}
		if (!TestFalse(TEXT("Natural completion consumes the event once"), Fixture.Enemy->IsLocationInvestigationCurrent(Serial))) return false;
		if (Parameters == TEXT("ActiveExpiry")) return TestTrue(TEXT("Started investigation reaches real search after its clue TTL"), bObservedExpiredActive);
		return TestEqual(TEXT("Only still-valid shared tracking survives hit investigation"), Fixture.Enemy->HasTrackableTarget(), Parameters == TEXT("SharedValid"));
	}
	AddError(TEXT("Hit exception scenario exceeded its finite investigation duration"));
	return false;
}

namespace
{
bool SetCombatTestRange(FEnemyCombatIntegrationWorld& Fixture, float Range)
{
	ARangedWeaponBase* Weapon = Fixture.Enemy->GetCurrentWeapon();
	if (!Fixture.Test.TestNotNull(TEXT("Range response uses the actual equipped weapon"), Weapon)) return false;
	FFloatProperty* Property = FindFProperty<FFloatProperty>(Weapon->GetClass(), TEXT("EffectiveRange"));
	if (!Fixture.Test.TestNotNull(TEXT("Weapon range can be configured for the scenario"), Property)) return false;
	Property->SetPropertyValue_InContainer(Weapon, Range);
	return true;
}

bool ReadCombatShotDistance(FEnemyCombatIntegrationWorld& Fixture, FVector& OutMuzzle, float& OutDistance)
{
	if (!Fixture.Test.TestNotNull(TEXT("Actual enemy provides the firing origin"), Fixture.Enemy)) return false;
	OutMuzzle = Fixture.Enemy->GetCombatShotOrigin();
	OutDistance = FVector::Dist(OutMuzzle, Fixture.Enemy->GetCombatAimPoint(Fixture.Target));
	return true;
}

bool WaitForObservedRangeExit(FEnemyCombatIntegrationWorld& Fixture)
{
	// 대상 이동은 새 TP 관측과 기존 조준점 갱신 주기를 거쳐 사거리 판정에 반영된다.
	for (int32 Frame = 0; Frame < 12; ++Frame)
	{
		if (Fixture.Enemy->IsTargetOutsideWeaponRange(Fixture.Target))
		{
			return true;
		}
		if (!Fixture.Advance())
		{
			return false;
		}
	}
	return Fixture.Enemy->IsTargetOutsideWeaponRange(Fixture.Target);
}

bool WaitForRangeApproach(FEnemyCombatIntegrationWorld& Fixture, int32 MaxFrames = 90)
{
	for (int32 Frame = 0; Frame < MaxFrames; ++Frame)
	{
		const auto* Query = Fixture.Data<FEnemyRunEnvQueryTask>();
		if (Query && Query->SearchMode == EEnemySearchQueryMode::None && Query->bValidateReachability) return true;
		if (!Fixture.Advance()) return false;
	}
	Fixture.Test.AddError(TEXT("Out-of-range target did not select the real Approach query"));
	return false;
}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyRangeResponseIntegrationTest,
	"Outlier.Enemy.Combat.RangeResponse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyRangeResponseIntegrationTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("MidBandApproach"), TEXT("HoldExitRange"), TEXT("PrecisionExitRange"), TEXT("VerticalRangeReselect"),
		TEXT("ShortRangeLegacy"), TEXT("ShortRangeHeight")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyRangeResponseIntegrationTest::RunTest(const FString& Parameters)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	FEnemyCombatIntegrationWorld Fixture(*this, Parameters);
	if (!Fixture.Initialize(false, FVector(1500, 0, 0))) return false;
	const bool bShortRange = Parameters.StartsWith(TEXT("ShortRange"));
	if (Parameters == TEXT("VerticalRangeReselect"))
	{
		// 이 검사는 같은 조준점의 수직 사거리 이탈을 분리한다. 더 가까운 다른 노출 부위로
		// 정상 대체되는 경우는 ExposedAim 회귀가 검사하므로 여기서는 실제 head 본 하나만 등록한다.
		FArrayProperty* Bones = FindFProperty<FArrayProperty>(Fixture.Target->GetClass(), TEXT("SightBones"));
		if (!TestNotNull(TEXT("Vertical range fixture can register a single actual TP bone"), Bones))
		{
			return false;
		}
		*Bones->ContainerPtrToValuePtr<TArray<FName>>(Fixture.Target) = {TEXT("head")};
	}
	Fixture.Enemy->FlightHeightSettings.bEnabled = Parameters != TEXT("ShortRangeLegacy");
	if (!SetCombatTestRange(Fixture, bShortRange ? 700.0f : Parameters == TEXT("MidBandApproach") ? 1200.0f : 5000.0f)) return false;
	ARangedWeaponBase* Weapon = Fixture.Enemy->GetCurrentWeapon();
	FBoolProperty* InfiniteAmmo = FindFProperty<FBoolProperty>(Weapon->GetClass(), TEXT("bInfiniteAmmo"));
	if (!TestNotNull(TEXT("Actual ammunition consumption can be observed"), InfiniteAmmo)) return false;
	InfiniteAmmo->SetPropertyValue_InContainer(Weapon, false);
	const int32 InitialAmmo = Weapon->GetCurrentAmmo();
	if (Parameters == TEXT("PrecisionExitRange"))
	{
		// 양쪽 선회를 막되 전방 접근 경로와 사선은 비워 둔다. 실제 후보 실패로 정밀 사격에 진입한다.
		const FVector Start = Fixture.Enemy->GetActorLocation();
		const double InnerEdge = Fixture.Enemy->GetCapsuleComponent()->GetScaledCapsuleRadius() + 15.0;
		for (const double Sign : {-1.0, 1.0})
			if (!Fixture.SpawnBlockingBox(Start + FVector(0, Sign * (InnerEdge + 300.0), 0), FVector(600, 300, 2500))) return false;
	}
	Fixture.Start();

	if (Parameters == TEXT("MidBandApproach"))
	{
		if (!Fixture.WaitForTask<FEnemyRunEnvQueryTask>(2)) return false;
		const auto* Query = Fixture.Data<FEnemyRunEnvQueryTask>();
		return TestTrue(TEXT("A target inside the 10..20m tactical band but outside weapon range selects Approach"),
			Query->bValidateReachability && !Query->bValidateOrbitPath);
	}
	if (bShortRange)
	{
		if (!WaitForRangeApproach(Fixture)) return false;
		for (int32 Frame = 0; Frame < 900; ++Frame)
		{
			if (!Fixture.Advance()) return false;
			if (Weapon->GetCurrentAmmo() >= InitialAmmo) continue;
			const auto* Fly = Fixture.Data<FEnemyFlyToLocationTask>();
			return TestTrue(TEXT("A real short-range shot is fired before the approach destination is reached"),
				Fly && Fixture.Enemy->GetVelocity().SizeSquared() > 1.0f
				&& FVector::Dist(Fixture.Enemy->GetActorLocation(), Fly->Destination) > Fly->AcceptanceRadius + 5.0f);
		}
		AddError(TEXT("Short-range Approach did not produce an actual shot within its bounded movement window"));
		return false;
	}
	if (Parameters == TEXT("PrecisionExitRange"))
	{
		for (int32 Frame = 0; Frame < 360 && !Fixture.Snapshot.HasState(TEXT("PrecisionFire")); ++Frame)
			if (!Fixture.Advance()) return false;
		if (!TestTrue(TEXT("Both blocked orbit sides select the real PrecisionFire state"),
			Fixture.Snapshot.HasState(TEXT("PrecisionFire")))) return false;
		FVector Muzzle;
		float Distance;
		if (!ReadCombatShotDistance(Fixture, Muzzle, Distance) || !SetCombatTestRange(Fixture, Distance + 20.0f)) return false;
		if (!TestFalse(TEXT("Precision fire begins inside the actual weapon range"),
			Fixture.Enemy->IsTargetOutsideWeaponRange(Fixture.Target))) return false;
		const FVector Previous = Fixture.Target->GetActorLocation();
		Fixture.Target->SetActorLocation(Previous + (Previous - Muzzle).GetSafeNormal() * 100.0f);
		if (!TestTrue(TEXT("A small retreat crosses the weapon range during precision fire"),
			WaitForObservedRangeExit(Fixture)))
		{
			return false;
		}
		// 공격 Task가 Running으로 대기하더라도 전술 분기는 접근을 다시 선택해야 한다.
		return WaitForRangeApproach(Fixture);
	}
	if (Parameters == TEXT("HoldExitRange"))
	{
		if (!Fixture.WaitForTask<FEnemyStationaryFireTask>()) return false;
		FVector Muzzle;
		float Distance;
		if (!ReadCombatShotDistance(Fixture, Muzzle, Distance) || !SetCombatTestRange(Fixture, Distance + 20.0f)) return false;
		if (!TestFalse(TEXT("The stationary target begins inside weapon range"),
			Fixture.Enemy->IsTargetOutsideWeaponRange(Fixture.Target))) return false;
		const FVector Previous = Fixture.Target->GetActorLocation();
		const FVector Away = (Previous - Muzzle).GetSafeNormal();
		Fixture.Target->SetActorLocation(Previous + Away * 100.0f);
		if (!TestTrue(TEXT("Retreat input is below the old 200cm destination threshold"),
			FVector::Dist(Previous, Fixture.Target->GetActorLocation()) < 200.0f)
			|| !TestTrue(TEXT("The short retreat crosses the actual weapon range boundary"),
				WaitForObservedRangeExit(Fixture)))
		{
			return false;
		}
		// EQS가 세 프레임 안에 끝나도 실제 접근 쿼리 선택을 놓치지 않는다.
		bool bSawApproachQuery = false;
		for (int32 Frame = 0; Frame < 3; ++Frame)
		{
			if (!Fixture.Advance())
			{
				return false;
			}
			const auto* Query = Fixture.Data<FEnemyRunEnvQueryTask>();
			bSawApproachQuery |= Query && Query->SearchMode == EEnemySearchQueryMode::None && Query->bValidateReachability;
		}
		if (!TestNull(TEXT("Range loss interrupts the selected stationary hold without waiting for its duration"),
			Fixture.Data<FEnemyStationaryFireTask>()))
		{
			return false;
		}
		return bSawApproachQuery || WaitForRangeApproach(Fixture);
	}
	if (Parameters == TEXT("VerticalRangeReselect"))
	{
		if (!Fixture.WaitForTask<FEnemyFlyToLocationTask>()) return false;
		const auto* Guard = Fixture.Data<FEnemyCombatDestinationGuardTask>();
		if (!TestNotNull(TEXT("Vertical scenario uses the actual destination guard"), Guard)) return false;
		const FVector Captured = Guard->SelectedTargetLocation;
		FVector Muzzle;
		float Distance;
		if (!ReadCombatShotDistance(Fixture, Muzzle, Distance) || !SetCombatTestRange(Fixture, Distance + 1.0f)) return false;
		if (!TestFalse(TEXT("The original orbit target begins inside weapon range"),
			Fixture.Enemy->IsTargetOutsideWeaponRange(Fixture.Target))) return false;
		// 드론의 접근으로 사거리 복귀가 섞이지 않게 이동만 고정하고, 트리·EQS·거리 판정은 실제 실행한다.
		Fixture.Enemy->GetCharacterMovement()->StopMovementImmediately();
		Fixture.Enemy->GetCharacterMovement()->SetComponentTickEnabled(false);
		const float Sign = Fixture.Enemy->GetCombatAimPoint(Fixture.Target).Z >= Muzzle.Z ? 1.0f : -1.0f;
		const FVector Changed = Captured + FVector(0, 0, Sign * 150.0f);
		Fixture.Target->SetActorLocation(Changed);
		AddInfo(FString::Printf(TEXT("[RangeObservedVertical] Captured=%s Changed=%s Muzzle=%s Aim=%s BeforeDistance=%.3f"),
			*Captured.ToCompactString(), *Changed.ToCompactString(), *Muzzle.ToCompactString(),
			*Fixture.Enemy->GetCombatAimPoint(Fixture.Target).ToCompactString(), Distance));
		if (!TestTrue(TEXT("Vertical input preserves XY and remains below the old movement threshold"),
			FVector::Dist2D(Captured, Changed) == 0.0f && FVector::Dist(Captured, Changed) < Guard->TargetChangeDistance)
			|| !TestTrue(TEXT("Vertical movement crosses the actual three-dimensional weapon range boundary"),
				WaitForObservedRangeExit(Fixture)))
		{
			return false;
		}
		for (int32 Frame = 0; Frame < 90; ++Frame)
		{
			if (!Fixture.Advance()) return false;
			const auto* Query = Fixture.Data<FEnemyRunEnvQueryTask>();
			if (Query && Query->bValidateReachability && Query->SelectedTargetLocation.Equals(Changed, 1.0f)) return true;
		}
		AddError(TEXT("Pure vertical range loss did not replace orbit with a new Approach query"));
		return false;
	}
	AddError(TEXT("Unknown range response scenario"));
	return false;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyRotationStateTreeIntegrationTest,
	"Outlier.Enemy.Rotation.StateTree", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyRotationStateTreeIntegrationTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("AlertScanAndExpiry"), TEXT("AlertSightHandoff"), TEXT("LookAroundOwnershipAndResume")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyRotationStateTreeIntegrationTest::RunTest(const FString& Parameters)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	FEnemyCombatIntegrationWorld Fixture(*this, Parameters);
	if (!Fixture.Initialize(false, FVector(1200, 0, 0), false, false)) return false;
	FEnemyStat& Stat = const_cast<FEnemyStat&>(Fixture.Enemy->GetRuntimeStat());
	Stat.AwarenessFillDuration = 1.0f;
	Stat.AwarenessDecayDuration = 0.5f;
	Stat.MinAlertHoldAfterClue = 4.0f;
	Fixture.Start();
	if (!Fixture.Advance()) return false;

	if (Parameters == TEXT("LookAroundOwnershipAndResume"))
	{
		// EQS와 실제 비행이 수색 예산을 먼저 소비하므로, 이번 검증에서는 회전 인계 시간을 확보한다.
		Stat.HitSearchDuration = 30.0f;
		const FVector Origin = Fixture.Enemy->GetActorLocation() + FVector(-700, 0, 0);
		if (!ApplyInvestigationHit(Fixture, Origin) || !WaitForHitState(Fixture, true)
			|| !Fixture.WaitForTask<FEnemyLookAroundTask>()) return false;
		const auto* Before = Fixture.Data<FEnemyLookAroundTask>();
		const int32 StepIndex = Before->CurrentStepIndex;
		const float HoldTime = Before->ElapsedHoldTime;
		const float MaxFirstStep = Before->RotationSpeed * Fixture.Step + 0.1f;
		const FRotator OwnedRotation(30.0f, 179.0f, 0.0f);
		// 실제 공격 조준은 Controller와 본체 Yaw를 함께 갱신한다. 해제 후 기본 AI 갱신도 같은 방향에서 시작해야 한다.
		Fixture.Controller->SetControlRotation(OwnedRotation);
		Fixture.Enemy->SetActorRotation(FRotator(0.0f, OwnedRotation.Yaw, 0.0f));
		Fixture.Controller->SetAttackAimActive(true);
		// 실제 병렬 Task를 실행한다. 회전을 양보하는 동안 단계와 정지 시간이 소모되면 안 된다.
		for (int32 Frame = 0; Frame < 12; ++Frame)
		{
			if (!Fixture.Advance()) return false;
			const auto* Held = Fixture.Data<FEnemyLookAroundTask>();
			if (!TestNotNull(TEXT("LookAround remains active while aim owns rotation"), Held)
				|| !TestEqual(TEXT("Yielding does not consume a look step"), Held->CurrentStepIndex, StepIndex)
				|| !TestEqual(TEXT("Yielding does not consume hold time"), Held->ElapsedHoldTime, HoldTime)
				|| !TestTrue(TEXT("LookAround cannot overwrite the owned pitch or yaw"),
					Fixture.Controller->GetControlRotation().Equals(OwnedRotation, 0.01f))) return false;
		}
		Fixture.Controller->SetAttackAimActive(false);
		if (!Fixture.Advance()) return false;
		const FRotator Resumed = Fixture.Controller->GetControlRotation();
		if (!TestTrue(TEXT("Resuming starts from the actual pitch instead of snapping to horizontal"),
			FMath::Abs(FMath::FindDeltaAngleDegrees(OwnedRotation.Pitch, Resumed.Pitch)) <= MaxFirstStep)
			|| !TestTrue(TEXT("Released look-around begins returning pitch toward horizontal"),
				FMath::Abs(Resumed.Pitch) < FMath::Abs(OwnedRotation.Pitch))
			|| !TestTrue(TEXT("Resuming starts from the actual yaw across the wrap boundary"),
				FMath::Abs(FMath::FindDeltaAngleDegrees(OwnedRotation.Yaw, Resumed.Yaw)) <= MaxFirstStep)) return false;
		bool bSawProgress = false;
		for (int32 Frame = 0; Frame < 600; ++Frame)
		{
			if (!Fixture.Advance()) return false;
			if (const auto* Resuming = Fixture.Data<FEnemyLookAroundTask>())
				bSawProgress |= Resuming->CurrentStepIndex != StepIndex || Resuming->ElapsedHoldTime > HoldTime;
			else
				return TestTrue(TEXT("Released look-around resumes its steps before the bounded investigation ends"), bSawProgress)
					&& TestTrue(TEXT("Completed look-around keeps a finite rotation"),
						!Fixture.Controller->GetControlRotation().ContainsNaN());
		}
		AddError(TEXT("Released look-around never completed"));
		return false;
	}

	// 감지 성공/실패만 주입하고 Alert 진입과 해제는 저장된 StateTree 및 인지 시계가 수행한다.
	Fixture.SetSight(true);
	for (int32 Frame = 0; Frame < 6; ++Frame) if (!Fixture.Advance()) return false;
	Fixture.SetSight(false);
	const double LostAt = Fixture.World->GetTimeSeconds();
	if (!TestEqual(TEXT("Brief sight enters Alert without confirming combat"),
		Fixture.Enemy->GetCombatState(), EEnemyCombatState::Alert)
		|| !Fixture.WaitForTask<FEnemyAlertHoldTask>(30)) return false;
	float MinYaw = 180.0f;
	float MaxYaw = -180.0f;
	for (int32 Frame = 0; Frame < 180; ++Frame)
	{
		const auto* Alert = Fixture.Data<FEnemyAlertHoldTask>();
		if (!TestNotNull(TEXT("Hidden alert retains its idle task before the deadline"), Alert)) return false;
		const float Speed = Alert->RotationSpeed;
		const float HalfAngle = Alert->ScanHalfAngle;
		const FRotator Previous = Fixture.Controller->GetControlRotation();
		if (!Fixture.Advance()) return false;
		const FRotator Current = Fixture.Controller->GetControlRotation();
		const float Yaw = FMath::FindDeltaAngleDegrees(0.0f, Current.Yaw);
		MinYaw = FMath::Min(MinYaw, Yaw);
		MaxYaw = FMath::Max(MaxYaw, Yaw);
		if (!TestTrue(TEXT("Idle scan stays around the adopted eastward clue"), FMath::Abs(Yaw) <= HalfAngle + 1.0f)
			|| !TestTrue(TEXT("Idle scan preserves a horizontal view"), FMath::Abs(Current.Pitch) <= 1.0f)
			|| !TestTrue(TEXT("Idle scan respects the configured angular speed"),
				FMath::Abs(FMath::FindDeltaAngleDegrees(Previous.Yaw, Current.Yaw)) <= Speed * Fixture.Step + 0.1f)) return false;
	}
	if (!TestTrue(TEXT("Hidden alert scans instead of holding one direction"), MaxYaw - MinYaw > 15.0f)) return false;
	if (Parameters == TEXT("AlertSightHandoff"))
	{
		UEnemyRoomSubsystem* Rooms = Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
		if (!TestNotNull(TEXT("Sight handoff uses the actual room subsystem"), Rooms)) return false;
		Fixture.Target->SetActorLocation(Fixture.Enemy->GetActorLocation() + FVector(0, 1200, 0));
		// 임시 월드는 Arena 레벨을 로드하지 않는다. 방 전투 시작은 입력으로 주고 직접 재발견의 합류 전이는 실제 실행한다.
		Rooms->SetActiveRoomTargetForTesting(Fixture.SearchRoomTag, Fixture.Target->GetActorLocation(), false);
		Fixture.SetSight(true);
		for (int32 Frame = 0; Frame < 120 && Fixture.Enemy->GetCombatState() != EEnemyCombatState::Combat; ++Frame)
			if (!Fixture.Advance()) return false;
		if (!TestEqual(TEXT("Direct sight completes the normal combat transition"),
			Fixture.Enemy->GetCombatState(), EEnemyCombatState::Combat)) return false;
		// Combat 확정 이벤트는 다음 Tick으로 지연된다. 이벤트 전달과 트리 전이를 마친 뒤 잔존 여부를 확인한다.
		for (int32 Frame = 0; Frame < 2; ++Frame) if (!Fixture.Advance()) return false;
		for (int32 Frame = 0; Frame < 30; ++Frame)
		{
			if (!Fixture.Advance()) return false;
			if (!TestNull(TEXT("Combat cannot restore the previous idle alert scan"), Fixture.Data<FEnemyAlertHoldTask>())
				|| !TestNull(TEXT("Combat cannot inherit a stale look-around"), Fixture.Data<FEnemyLookAroundTask>())) return false;
		}
		return TestEqual(TEXT("Scan handoff preserves the real visible target"),
			Fixture.Enemy->GetTrackedTarget(), static_cast<AActor*>(Fixture.Target));
	}
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		if (!Fixture.Advance()) return false;
		if (Fixture.Enemy->GetCombatState() == EEnemyCombatState::NonCombat)
			return TestTrue(TEXT("Scan frames do not extend the last clue's four-second alert deadline"),
				Fixture.World->GetTimeSeconds() - LostAt <= 4.2);
	}
	AddError(TEXT("Idle scan extended Alert after the original clue expired"));
	return false;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyGunAimPresentationDiagnostic,
	"Outlier.Enemy.Combat.GunAimPresentationDiagnostic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyGunAimPresentationDiagnostic::RunTest(const FString& Parameters)
{
	struct FCase { const TCHAR* Name; FVector Offset; bool bMoving; };
	const FCase Cases[] = {
		{TEXT("CloseLevel50"), FVector(50, 0, 0), false},
		{TEXT("CloseLevel100"), FVector(100, 0, 0), false},
		{TEXT("CloseLevel200"), FVector(200, 0, 0), false},
		{TEXT("CloseLevel400"), FVector(400, 0, 0), false},
		{TEXT("CloseLevel1000"), FVector(1000, 0, 0), false},
		{TEXT("CloseAbove"), FVector(100, 0, 50), false},
		{TEXT("CloseBelow"), FVector(100, 0, -50), false},
		{TEXT("StationaryLevel"), FVector(1200, 0, 0), false},
		{TEXT("StationaryBelow"), FVector(1200, 0, -200), false},
		{TEXT("StationaryCloseBelow"), FVector(400, 0, -200), false},
		{TEXT("LiveApproach"), FVector(2400, 0, -200), true}
	};
	for (const FCase& Case : Cases)
	{
		FEnemyCombatIntegrationWorld Fixture(*this, Case.Name);
		if (!Fixture.Initialize(false, Case.Offset)) return false;
		AVECDrone* Drone = CastChecked<AVECDrone>(Fixture.Enemy);
		if (!TestTrue(TEXT("Production fixture observes the Shooter target"), Drone->IsAimingAtShooter())) return false;
		ARangedWeaponBase* Weapon = Drone->GetCurrentWeapon();
		if (!TestNotNull(TEXT("Production gun is equipped"), Weapon)) return false;
		if (!TestTrue(TEXT("Visual tick requires a non-dedicated world"), Drone->GetNetMode() != NM_DedicatedServer)) return false;
		AddInfo(FString::Printf(TEXT("[AimDefaults] Case=%s ControllerPitch=%d Actor=%s Aim=%s Tilt=%s Mesh=%s Anim=%s"),
			Case.Name, Drone->bUseControllerRotationPitch, *Drone->GetActorRotation().ToCompactString(),
			*Drone->GetAIFacingPitchRoot()->GetRelativeRotation().ToCompactString(),
			*Drone->GetThirdPersonTiltRoot()->GetRelativeRotation().ToCompactString(),
			*Drone->GetMesh()->GetRelativeRotation().ToCompactString(), *GetNameSafe(Drone->GetMesh()->GetAnimClass())));
		UVECDroneMovementComponent* Feel = Drone->FindComponentByClass<UVECDroneMovementComponent>();
		for (const FName Name : {FName(TEXT("CameraPitchOnMove")), FName(TEXT("MeshInertialTiltScale")), FName(TEXT("MeshPitchInertialTiltScale"))})
		{
			const FFloatProperty* Property = Feel ? FindFProperty<FFloatProperty>(Feel->GetClass(), Name) : nullptr;
			if (Property) AddInfo(FString::Printf(TEXT("[AimFeel] %s=%.3f"), *Name.ToString(), Property->GetPropertyValue_InContainer(Feel)));
		}
		for (TFieldIterator<FStructProperty> Property(Drone->GetMesh()->GetAnimClass()); Property; ++Property)
		{
			if (Property->Struct->GetName().StartsWith(TEXT("AnimNode_")))
				AddInfo(FString::Printf(TEXT("[AimAnimNode] %s"), *Property->Struct->GetName()));
		}
		int32 Shots = 0;
		double MaxBaseError = 0, MaxExtra = 0, MaxFiringExtra = 0;
		int32 FiringFrames = 0;
		const FDelegateHandle Handle = WallShotProbe::Observed().AddLambda([&](const FWallShotObservation& Shot)
		{
			if (Shot.Weapon != Weapon || Shot.bPresentation) return;
			const FVector Center = Drone->GetCombatAimPoint(Fixture.Target);
			TestTrue(TEXT("Actual shot starts at the shared body anchor"), Shot.Start.Equals(Drone->GetCombatShotOrigin(), 0.01));
			const FVector Expected = (Center - Shot.Start).GetSafeNormal();
			const double Error = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Expected, Shot.BaseDirection), -1.0, 1.0)));
			MaxBaseError = FMath::Max(MaxBaseError, Error);
			++Shots;
			if (Shots <= 3) AddInfo(FString::Printf(TEXT("[AimShot] Case=%s Start=%s Center=%s Expected=%.3f Base=%.3f Actual=%.3f Error=%.4f Actor=%.3f Aim=%.3f Tilt=%.3f World=%.3f Speed=%.1f"),
				Case.Name, *Shot.Start.ToCompactString(), *Center.ToCompactString(), Expected.Rotation().Pitch,
				Shot.BaseDirection.Rotation().Pitch, Shot.Direction.Rotation().Pitch, Error, Drone->GetActorRotation().Pitch,
				Drone->GetAIFacingPitchRoot()->GetRelativeRotation().Pitch, Drone->GetThirdPersonTiltRoot()->GetRelativeRotation().Pitch,
				Drone->GetThirdPersonTiltRoot()->GetComponentRotation().Pitch, Drone->GetVelocity().Size()));
		});
		ON_SCOPE_EXIT { WallShotProbe::Observed().Remove(Handle); Drone->StopCurrentAttack(); };
		if (Case.bMoving) Fixture.Start();
		else
		{
			// 이동만 고정하고 BP 무기 설정과 실제 발사 타이머는 유지한다.
			Drone->GetCharacterMovement()->SetComponentTickEnabled(false);
			// 이 진단은 정렬 후의 외형/탄도를 비교한다. 즉시 회전시키지 않고 실제 최초 전조와 제한 회전을 먼저 거친다.
			Drone->BeginTargetAttackPreparation(0.25f);
			for (int32 Frame = 0; Frame < 30; ++Frame)
			{
				Drone->AdvanceTargetAttackPreparation(FEnemyCombatIntegrationWorld::Step);
				Fixture.Tick(FEnemyCombatIntegrationWorld::Step);
			}
			if (!TestTrue(TEXT("Production attack starts"), Drone->StartAttackTarget(Fixture.Target))) return false;
		}
		for (int32 Frame = 0; Frame < 240; ++Frame)
		{
			// NullRHI에서도 최근 렌더 조건만 열어 실제 외형 Tick을 실행한다.
			Drone->GetMesh()->SetLastRenderTime(Fixture.World->GetTimeSeconds());
			if (!Case.bMoving) Drone->UpdateAttackLocation(Drone->GetCombatAimPoint(Fixture.Target));
			Fixture.Tick(FEnemyCombatIntegrationWorld::Step);
			if (UEnvQueryManager* Queries = UEnvQueryManager::GetCurrent(Fixture.World)) Queries->Tick(FEnemyCombatIntegrationWorld::Step);
			const double Extra = FMath::FindDeltaAngleDegrees(Drone->GetBaseAimRotation().Pitch,
				Drone->GetThirdPersonTiltRoot()->GetComponentRotation().Pitch);
			if (FMath::Abs(Extra) > FMath::Abs(MaxExtra)) MaxExtra = Extra;
			if (Drone->GetAttackPhase() == EEnemyAttackPhase::Firing)
			{
				++FiringFrames;
				double ExpectedBodyPitch = FMath::ClampAngle(Drone->GetBaseAimRotation().Pitch, -89.0f, 89.0f);
				const double BodyError = FMath::FindDeltaAngleDegrees(ExpectedBodyPitch,
					Drone->GetThirdPersonTiltRoot()->GetComponentRotation().Pitch);
				MaxFiringExtra = FMath::Max(MaxFiringExtra, FMath::Abs(BodyError));
			}
		}
		AddInfo(FString::Printf(TEXT("[AimSummary] Case=%s Shots=%d Error=%.4f MaxExtra=%.3f Control=%.3f Actor=%.3f Aim=%.3f Tilt=%.3f World=%.3f Rendered=%d"),
			Case.Name, Shots, MaxBaseError, MaxExtra, Drone->GetBaseAimRotation().Pitch, Drone->GetActorRotation().Pitch,
			Drone->GetAIFacingPitchRoot()->GetRelativeRotation().Pitch, Drone->GetThirdPersonTiltRoot()->GetRelativeRotation().Pitch,
			Drone->GetThirdPersonTiltRoot()->GetComponentRotation().Pitch, Drone->WasRecentlyRendered()));
		// 조준 축 일치와 실제 에셋의 총열 축은 별개다. 클라이언트 시각 검증에 사용할 실제 소켓 변환도 남긴다.
		TArray<FName> MuzzleSockets;
		Drone->GetWeaponMuzzleSocketNames(false, MuzzleSockets);
		for (const FName Socket : MuzzleSockets)
		{
			const bool bExists = Drone->GetMesh()->DoesSocketExist(Socket);
			TestTrue(TEXT("Production visual muzzle socket exists"), bExists);
			if (!bExists) continue;
			const FTransform Muzzle = Drone->GetMesh()->GetSocketTransform(Socket);
			const FVector BaseDirection = Drone->GetBaseAimRotation().Vector();
			const auto AxisError = [&](EAxis::Type Axis)
			{
				return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
					FVector::DotProduct(Muzzle.GetUnitAxis(Axis), BaseDirection), -1.0, 1.0)));
			};
			// 소켓 X축을 실제 총열 정면이라고 가정하지 않는다. 모델 축을 확인할 수 있게 세 축을 함께 기록한다.
			AddInfo(FString::Printf(TEXT("[AimModel] Case=%s Socket=%s Position=%s Forward=%s TargetFromSocket=%s MeshRelative=%s Base=%s AxisErrorXYZ=(%.3f,%.3f,%.3f) AimOwned=%d"),
				Case.Name, *Socket.ToString(), *Muzzle.GetLocation().ToCompactString(),
				*Muzzle.GetUnitAxis(EAxis::X).Rotation().ToCompactString(),
				*(Drone->GetCombatAimPoint(Fixture.Target) - Muzzle.GetLocation()).Rotation().ToCompactString(),
				*Drone->GetMesh()->GetRelativeTransform().ToString(), *BaseDirection.ToCompactString(),
				AxisError(EAxis::X), AxisError(EAxis::Y), AxisError(EAxis::Z), Drone->IsCombatAimPresentationOwned()));
		}
		const auto ObserveBodyPose = [&](const TCHAR* Stage)
		{
			USkeletalMeshComponent* Mesh = Drone->GetMesh();
			const FReferenceSkeleton& Skeleton = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
			const int32 Body = Skeleton.FindBoneIndex(TEXT("BodyBone"));
			if (!TestTrue(TEXT("Actual model body bone exists"), Body != INDEX_NONE)) return;
			FTransform Reference = Skeleton.GetRefBonePose()[Body];
			for (int32 Parent = Skeleton.GetParentIndex(Body); Parent != INDEX_NONE; Parent = Skeleton.GetParentIndex(Parent))
				Reference = Reference * Skeleton.GetRefBonePose()[Parent];
			// 모델의 기준 포즈 정면을 본 공간으로 옮긴 뒤 실제 포즈로 되돌려, 조준 축과 몸통 본을 분리한다.
			const FVector ModelForward = Mesh->GetRelativeRotation().UnrotateVector(FVector::ForwardVector);
			const FVector BodyForward = Mesh->GetBoneTransform(Body).GetRotation().RotateVector(
				Reference.GetRotation().UnrotateVector(ModelForward));
			const FRotator Axis = Drone->GetThirdPersonTiltRoot()->GetForwardVector().Rotation();
			AddInfo(FString::Printf(TEXT("[AimBodyPose] Case=%s Stage=%s Phase=%s Axis=%s BodyForward=%s PoseExtraPitch=%.3f BoneWorld=%s Reference=%s BodyPosition=%s RefPosition=%s Origin=%s MeshCenter=%s"),
				Case.Name, Stage, *UEnum::GetValueAsString(Drone->GetAttackPhase()), *Axis.ToCompactString(),
				*BodyForward.Rotation().ToCompactString(), FMath::FindDeltaAngleDegrees(Axis.Pitch, BodyForward.Rotation().Pitch),
				*Mesh->GetBoneTransform(Body).GetRotation().Rotator().ToCompactString(), *Reference.GetRotation().Rotator().ToCompactString(),
				*Mesh->GetBoneTransform(Body).GetLocation().ToCompactString(), *Reference.GetLocation().ToCompactString(),
				*Drone->GetCombatShotOrigin().ToCompactString(), *Mesh->Bounds.Origin.ToCompactString()));
		};
		ObserveBodyPose(TEXT("Firing"));
		TestTrue(TEXT("Actual production shots observed"), Shots > 0);
		// 정지 상태는 중심 정렬을 유지하고, 이동 중에는 현재 서버 조준의 승인된 허용각을 검사한다.
		const double AllowedBaseError = Case.bMoving ? Drone->GetRuntimeStat().AttackAimTolerance + 0.01 : 0.1;
		TestTrue(TEXT("Base shots respect capsule-center aiming tolerance"), MaxBaseError < AllowedBaseError);
		TestTrue(TEXT("Firing presentation frames were observed"), FiringFrames > 0);
		TestTrue(TEXT("Firing body follows full server aim without additional flight pitch"), MaxFiringExtra < 0.25);
		AddInfo(FString::Printf(TEXT("[AimAlignment] Case=%s FiringFrames=%d MaxError=%.4f"), Case.Name, FiringFrames, MaxFiringExtra));
		if (Case.bMoving)
		{
			// 사격 외형만 제한했는지 양성 대조한다. 같은 드론의 비사격 이동 기울기는 다시 나타나야 한다.
			Fixture.Tree->StopLogic(TEXT("Check presentation after firing"));
			Drone->StopCurrentAttack();
			// 공격 Phase 종료만으로 유효 목표의 조준권을 뺏지 않는다. 비전투 양성 대조는 추적도 종료한다.
			Fixture.Controller->SetCachedVisibleTargetForTesting(nullptr);
			Drone->SetPlayerCurrentlyVisible(false);
			Drone->ClearTargetTracking();
			Drone->SetAttackPhase(EEnemyAttackPhase::Idle);
			Drone->GetCharacterMovement()->SetComponentTickEnabled(false);
			Drone->GetCharacterMovement()->Velocity = Drone->GetActorForwardVector() * 300.0f;
			for (int32 Frame = 0; Frame < 120; ++Frame)
			{
				Drone->GetMesh()->SetLastRenderTime(Fixture.World->GetTimeSeconds());
				Fixture.Tick(FEnemyCombatIntegrationWorld::Step);
			}
			TestTrue(TEXT("Non-firing flight retains movement pitch"), FMath::Abs(Drone->GetThirdPersonTiltRoot()->GetRelativeRotation().Pitch) > 0.5);
		}
		else
		{
			Drone->StopCurrentAttack();
			Drone->SetAttackPhase(EEnemyAttackPhase::Idle);
			for (int32 Frame = 0; Frame < 90; ++Frame)
			{
				Drone->GetMesh()->SetLastRenderTime(Fixture.World->GetTimeSeconds());
				Fixture.Tick(FEnemyCombatIntegrationWorld::Step);
			}
			ObserveBodyPose(TEXT("Idle"));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyGunBodyOriginContractTest,
	"Outlier.Enemy.Combat.BodyOrigin.Contract", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyGunBodyOriginContractTest::RunTest(const FString& Parameters)
{
	FEnemyCombatIntegrationWorld Fixture(*this, TEXT("BodyOrigin"));
	if (!Fixture.Initialize(false, FVector(100, 0, 0))) return false;
	AVECDrone* Drone = CastChecked<AVECDrone>(Fixture.Enemy);
	Drone->GetCharacterMovement()->SetComponentTickEnabled(false);
	USceneComponent* Root = Drone->GetAIFacingPitchRoot();
	const FVector OriginalRelative = Root->GetRelativeLocation();
	Root->SetRelativeLocation(OriginalRelative + FVector(0, 0, 25));
	const FVector Origin = Drone->GetCombatShotOrigin();
	Root->SetRelativeRotation(FRotator(-50, 0, 0));
	Drone->GetThirdPersonTiltRoot()->SetRelativeRotation(FRotator(-20, 0, 15));
	TestTrue(TEXT("Aim and movement pitch cannot move the body origin"), Drone->GetCombatShotOrigin().Equals(Origin, 0.001));
	TestTrue(TEXT("Authored fixed root location is respected"), Origin.Equals(Root->GetComponentLocation(), 0.001));
	Root->SetRelativeRotation(FRotator::ZeroRotator);
	Drone->GetThirdPersonTiltRoot()->SetRelativeRotation(FRotator::ZeroRotator);
	Fixture.Target->SetActorLocation(Origin + FVector(100, 0, 0));
	TestTrue(TEXT("Level close target can be aimed"), Drone->UpdateAttackLocation(Drone->GetCombatAimPoint(Fixture.Target)));
	TestTrue(TEXT("Level close body aim is horizontal"), FMath::Abs(Fixture.Controller->GetControlRotation().Pitch) < 0.01);
	FTransform Shot;
	TestTrue(TEXT("Real weapon resolves its firing transform"), FWallShotProbeAccess::Resolve(Drone->GetCurrentWeapon(), Shot));
	TestTrue(TEXT("Weapon and AI use exactly the same authored body origin"), Shot.GetLocation().Equals(Origin, 0.001));
	const FRotator ValidRotation = Fixture.Controller->GetControlRotation();
	TestFalse(TEXT("Zero direction does not authorize a new aim"), Drone->UpdateAttackLocation(Origin));
	TestTrue(TEXT("Rejected aim preserves the last valid control rotation"), Fixture.Controller->GetControlRotation().Equals(ValidRotation));
	// 타깃이 정확히 기준점에 있을 때는 공격 요청도 실패해야 탄약/연사 타이머가 잘못 진행되지 않는다.
	Fixture.Target->SetActorLocation(Origin);
	const int32 Ammo = Drone->GetCurrentWeapon()->GetCurrentAmmo();
	TestFalse(TEXT("Degenerate target cannot start a burst"), Drone->StartAttackTarget(Fixture.Target));
	TestEqual(TEXT("Rejected degenerate shot consumes no ammunition"), Drone->GetCurrentWeapon()->GetCurrentAmmo(), Ammo);
	Root->SetRelativeLocation(OriginalRelative);
	Drone->SetEnemyPossessed(true);
	FVector View;
	FRotator Rotation;
	Fixture.Controller->GetPlayerViewPoint(View, Rotation);
	TestTrue(TEXT("Possessed drone retains its existing viewpoint origin"), Drone->GetCombatShotOrigin().Equals(View, 0.001));
	TestTrue(TEXT("Possessed weapon resolves"), FWallShotProbeAccess::Resolve(Drone->GetCurrentWeapon(), Shot));
	TestTrue(TEXT("Possessed weapon retains viewpoint origin"), Shot.GetLocation().Equals(View, 0.001));
	Drone->SetEnemyPossessed(false);
	TestTrue(TEXT("Returning to AI immediately restores body origin"), Drone->GetCombatShotOrigin().Equals(Root->GetComponentLocation(), 0.001));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyGunPausedAimPresentationTest,
	"Outlier.Enemy.Combat.GunAimPresentation.PausedBurst",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyGunPausedAimPresentationTest::RunTest(const FString& Parameters)
{
	FEnemyCombatIntegrationWorld Fixture(*this, TEXT("PausedAimPresentation"));
	if (!Fixture.Initialize(false, FVector(400, 0, -200))) return false;
	AVECDrone* Drone = CastChecked<AVECDrone>(Fixture.Enemy);
	Drone->GetCharacterMovement()->SetComponentTickEnabled(false);
	Drone->GetCharacterMovement()->Velocity = FVector(300, 0, 0);
	Drone->BeginTargetAttackPreparation(0.25f);
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		Drone->GetMesh()->SetLastRenderTime(Fixture.World->GetTimeSeconds());
		Drone->AdvanceTargetAttackPreparation(Fixture.Step);
		Fixture.Tick(Fixture.Step);
	}
	if (!TestTrue(TEXT("Real controller owns aligned combat aim"), Drone->IsCombatAimPresentationOwned())
		|| !TestTrue(TEXT("Aligned production burst starts"), Drone->StartAttackTarget(Fixture.Target))) return false;
	Drone->PauseTargetAttack();
	if (!TestEqual(TEXT("Paused firing uses the existing Idle phase"), Drone->GetAttackPhase(), EEnemyAttackPhase::Idle)) return false;
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		Drone->GetMesh()->SetLastRenderTime(Fixture.World->GetTimeSeconds());
		Fixture.Tick(Fixture.Step);
		if (!TestTrue(TEXT("Fire pause preserves valid combat aim ownership"), Drone->IsCombatAimPresentationOwned())
			|| !TestTrue(TEXT("Idle pause cannot reapply flight pitch"),
				FMath::Abs(Drone->GetThirdPersonTiltRoot()->GetRelativeRotation().Pitch) < 0.25)) return false;
		const double PitchError = FMath::Abs(FMath::FindDeltaAngleDegrees(Drone->GetBaseAimRotation().Pitch,
			Drone->GetThirdPersonTiltRoot()->GetComponentRotation().Pitch));
		if (!TestTrue(TEXT("Idle pause follows the full downward server pitch"), PitchError < 0.25)) return false;
	}
	Fixture.SetSight(false);
	Drone->ClearTargetTracking();
	TestFalse(TEXT("Tracking cleanup immediately releases visual aim ownership"), Drone->IsCombatAimPresentationOwned());
	Drone->SetEnemyPossessed(true);
	TestFalse(TEXT("Possession cannot inherit AI visual aim ownership"), Drone->IsCombatAimPresentationOwned());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyGunBodyOriginPathTest,
	"Outlier.Enemy.Combat.BodyOrigin.PathAndRange", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyGunBodyOriginPathTest::RunTest(const FString& Parameters)
{
	FEnemyCombatIntegrationWorld Fixture(*this, TEXT("BodyPath"));
	if (!Fixture.Initialize(false, FVector(400, 0, 0))) return false;
	Fixture.Enemy->GetCharacterMovement()->SetComponentTickEnabled(false);
	ARangedWeaponBase* Weapon = Fixture.Enemy->GetCurrentWeapon();
	const FVector Origin = Fixture.Enemy->GetCombatShotOrigin();
	const FVector Center = Fixture.Enemy->GetCombatAimPoint(Fixture.Target);
	const float Distance = FVector::Dist(Origin, Center);
	if (!SetCombatTestRange(Fixture, Distance)) return false;
	TestFalse(TEXT("Exact body range boundary is inside"), Weapon->IsOutsideEffectiveRange(Center));
	TestTrue(TEXT("Open exact range boundary permits a shot"), Weapon->HasClearTargetAttackPath(Fixture.Target, Center));
	TestTrue(TEXT("One cm beyond the shared range boundary is outside"), Weapon->IsOutsideEffectiveRange(Center + FVector(1, 0, 0)));
	if (!SetCombatTestRange(Fixture, Distance + 100)) return false;
	AActor* Wall = Fixture.SpawnBlockingBox(Origin + FVector(150, 0, 0), FVector(10, 100, 10));
	if (!TestNotNull(TEXT("Low body-path obstruction exists"), Wall)) return false;
	FVector View;
	FRotator Rotation;
	Fixture.Controller->GetPlayerViewPoint(View, Rotation);
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Fixture.Enemy);
	Params.AddIgnoredActor(Weapon);
	FHitResult Hit;
	TestFalse(TEXT("Old high viewpoint would clear the low obstruction"), Fixture.World->LineTraceSingleByChannel(Hit, View, Center, ECC_Visibility, Params));
	TestFalse(TEXT("New body path respects the low obstruction"), Weapon->HasClearTargetAttackPath(Fixture.Target, Center));
	TestFalse(TEXT("Obstructed body path cannot start a real target attack"), Fixture.Enemy->StartAttackTarget(Fixture.Target));
	Wall->Destroy();
	TestTrue(TEXT("Removing the obstruction restores the same body path"), Weapon->HasClearTargetAttackPath(Fixture.Target, Center));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyGunTaskFacingDiagnostic,
	"Outlier.Enemy.Combat.BodyOrigin.TaskFacingDiagnostic", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyGunTaskFacingDiagnostic::RunTest(const FString& Parameters)
{
	for (const float Distance : { 100.0f, 200.0f, 400.0f })
	{
		FEnemyCombatIntegrationWorld Fixture(*this, TEXT("NearTaskFacing"));
		if (!Fixture.Initialize(false, FVector(Distance, 0, 0))) return false;
		AVECDrone* Drone = CastChecked<AVECDrone>(Fixture.Enemy);
		// 움직일 공간이 막혀 제자리에서 후퇴를 시도하는 근거리 조건이다. Task와 조준은 실제 트리를 실행한다.
		Drone->GetCharacterMovement()->SetComponentTickEnabled(false);
		Fixture.Start();
		double LowestControlError = 0, LowestVisualError = 0;
		int32 Logged = 0;
		for (int32 Frame = 0; Frame < 360; ++Frame)
		{
			Drone->GetMesh()->SetLastRenderTime(Fixture.World->GetTimeSeconds());
			if (!Fixture.Advance()) return false;
			const double Expected = (Drone->GetCombatAimPoint(Fixture.Target) - Drone->GetCombatShotOrigin()).Rotation().Pitch;
			const double ControlError = FMath::FindDeltaAngleDegrees(Expected, Fixture.Controller->GetControlRotation().Pitch);
			const double VisualError = FMath::FindDeltaAngleDegrees(Expected, Drone->GetThirdPersonTiltRoot()->GetComponentRotation().Pitch);
			LowestControlError = FMath::Min(LowestControlError, ControlError);
			LowestVisualError = FMath::Min(LowestVisualError, VisualError);
			if ((ControlError < -5 || VisualError < -5) && Logged++ < 8)
			{
				const auto* Fly = Fixture.Data<FEnemyFlyToLocationTask>();
				const auto* Facing = Fixture.Data<FEnemyMaintainFacingTask>();
				AddInfo(FString::Printf(TEXT("[TaskFacing] Distance=%.0f Time=%.3f Expected=%.3f Control=%.3f Axis=%.3f Tilt=%.3f Visual=%.3f Phase=%s AttackOwns=%d FlyFacesMove=%d Destination=%s FacingActor=%s FacingLocation=%s Preferred=%d States=%s"),
					Distance, Fixture.World->GetTimeSeconds(), Expected, Fixture.Controller->GetControlRotation().Pitch,
					Drone->GetAIFacingPitchRoot()->GetRelativeRotation().Pitch, Drone->GetThirdPersonTiltRoot()->GetRelativeRotation().Pitch,
					Drone->GetThirdPersonTiltRoot()->GetComponentRotation().Pitch, *UEnum::GetValueAsString(Drone->GetAttackPhase()),
					Fixture.Controller->IsAttackAimActive(), Fly && Fly->bRotateTowardDestination,
					Fly ? *Fly->Destination.ToCompactString() : TEXT("None"), Facing ? *GetNameSafe(Facing->TargetActor) : TEXT("None"),
					Facing ? *Facing->TargetLocation.ToCompactString() : TEXT("None"), Facing && Facing->bUsePreferredVisibleTarget, *Fixture.Snapshot.Describe()));
			}
		}
		AddInfo(FString::Printf(TEXT("[TaskFacingSummary] Distance=%.0f LowestControlError=%.3f LowestVisualError=%.3f"), Distance, LowestControlError, LowestVisualError));
	}
	return !HasAnyErrors();
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyNoiseFacingIntegrationTest,
	"Outlier.Enemy.Perception.NoiseFacingIntegration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyNoiseFacingIntegrationTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("MovementControl"), TEXT("ReturnToInvestigation"), TEXT("RealSightHandoff"), TEXT("PitchPresentation")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyNoiseFacingIntegrationTest::RunTest(const FString& Parameters)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	const auto HearSecondary = [&](FEnemyCombatIntegrationWorld& Setup, const FVector& Point)
	{
		FAIStimulus Stimulus(*GetDefault<UAISense_Hearing>(), 1.0f, Point, Setup.Enemy->GetActorLocation());
		Setup.Controller->GetPerceptionComponent()->RegisterStimulus(Setup.Target, Stimulus);
		Setup.Controller->GetPerceptionComponent()->ProcessStimuli();
		FEnemyHeardNoise Candidate;
		// 더 가까운 기존 이동 후보와 별개로 짧은 방향 확인 후보가 저장됐는지 검사한다.
		return TestTrue(TEXT("Secondary hearing stores a direction candidate"), Setup.Enemy->TryGetDirectionConfirmationNoise(Candidate));
	};
	if (Parameters == TEXT("MovementControl") || Parameters == TEXT("ReturnToInvestigation"))
	{
		FVector Results[2];
		for (int32 Run = 0; Run < 2; ++Run)
		{
			FEnemyCombatIntegrationWorld Fixture(*this, Parameters);
			if (!Fixture.Initialize(false, FVector(5000, 0, 0), false, false)) return false;
			Fixture.Enemy->NoiseDestinationUpdateInterval = 10.0f;
			Fixture.Start();
			if (!HearInvestigationNoise(Fixture, Fixture.Enemy->GetActorLocation() + FVector(1800, 0, 0))
				|| !Fixture.WaitForTask<FEnemyFlyToLocationTask>()) return false;
			for (int32 Frame = 0; Frame < 6; ++Frame) if (!Fixture.Advance()) return false;
			const FVector Start = Fixture.Enemy->GetActorLocation();
			const FVector Goal = Fixture.Enemy->GetActiveNoiseInvestigationClue().Location;
			if (Run == 1 && !HearSecondary(Fixture, Start + FVector(0, 2400, 500))) return false;
			bool bSawFacing = false;
			for (int32 Frame = 0; Frame < 36; ++Frame)
			{
				if (!Fixture.Advance()) return false;
				bSawFacing |= Fixture.Enemy->IsNoiseDirectionFacing();
				if (!TestTrue(TEXT("Short look retains actual movement destination"), Fixture.Enemy->GetActiveNoiseInvestigationClue().Location.Equals(Goal))) return false;
			}
			Results[Run] = Fixture.Enemy->GetActorLocation() - Start;
			if (Run == 1)
			{
				TestTrue(TEXT("Actual saved tree/controller executed facing"), bSawFacing);
				TestTrue(TEXT("Body turns while flying forward"), Fixture.Enemy->GetActorRotation().Yaw > 10.0);
				if (Parameters == TEXT("ReturnToInvestigation"))
				{
					bool bRestoredDirection = false;
					double PreviousYaw = Fixture.Enemy->GetActorRotation().Yaw;
					// Wait for the existing one-second rotation cap/hold, then allow normal
					// 180-degree-per-second travel rotation to return from the 60-degree sector.
					for (int32 Frame = 0; Frame < 120; ++Frame)
					{
						if (!Fixture.Advance())
						{
							return false;
						}
						const double CurrentYaw = Fixture.Enemy->GetActorRotation().Yaw;
						TestTrue(TEXT("Direction handoff does not snap body rotation"),
							FMath::Abs(FMath::FindDeltaAngleDegrees(PreviousYaw, CurrentYaw)) <= 180.0 * FEnemyCombatIntegrationWorld::Step + 0.1);
						PreviousYaw = CurrentYaw;
						const double TravelYaw = (Goal - Fixture.Enemy->GetActorLocation()).Rotation().Yaw;
						if (!Fixture.Enemy->IsNoiseDirectionFacing()
							&& FMath::Abs(FMath::FindDeltaAngleDegrees(CurrentYaw, TravelYaw)) < 5.0)
						{
							bRestoredDirection = true;
							break;
						}
					}
					TestFalse(TEXT("Short sound ownership ends"), Fixture.Enemy->IsNoiseDirectionFacing());
					TestTrue(TEXT("Existing movement smoothly restores investigation direction"), bRestoredDirection);
					TestTrue(TEXT("Returning orientation preserves destination"), Fixture.Enemy->GetActiveNoiseInvestigationClue().Location.Equals(Goal));
				}
			}
		}
		AddInfo(FString::Printf(TEXT("[NoiseFacingMovement] Control=%s Facing=%s"), *Results[0].ToCompactString(), *Results[1].ToCompactString()));
		TestTrue(TEXT("Looking does not change forward progress"), FMath::Abs(Results[0].X - Results[1].X) < 5.0);
		return TestTrue(TEXT("Looking does not steer flight sideways"), FMath::Abs(Results[1].Y - Results[0].Y) < 5.0);
	}
	FEnemyCombatIntegrationWorld Fixture(*this, Parameters);
	if (!Fixture.Initialize(false, FVector(0, 1000, 0), false, true)) return false;
	AVECDrone* Drone = Cast<AVECDrone>(Fixture.Enemy);
	if (!TestNotNull(TEXT("Production gun drone"), Drone)) return false;
	Fixture.Enemy->NoiseDestinationUpdateInterval = 10.0f;
	Fixture.Start();
	if (!HearInvestigationNoise(Fixture, Drone->GetActorLocation() + FVector(1800, 0, 0))
		|| !Fixture.WaitForTask<FEnemyFlyToLocationTask>()) return false;
	for (int32 Frame = 0; Frame < 3; ++Frame) if (!Fixture.Advance()) return false;
	if (Parameters == TEXT("RealSightHandoff"))
	{
		// 초기에는 시야 밖, 60도 확인 후에는 시야 안이 되도록 감지 각도만 통제한다.
		const_cast<FEnemyStat&>(Drone->GetRuntimeStat()).BattlePeripheralVisionAngle = 45.0f;
		Fixture.Controller->RefreshPerceptionConfigFromPawn();
		if (!HearSecondary(Fixture, Fixture.Target->GetActorLocation())) return false;
		Fixture.Controller->SetEnemyPerceptionEnabled(true);
		bool bSawFacing = false;
		for (int32 Frame = 0; Frame < 90; ++Frame)
		{
			if (!Fixture.Advance()) return false;
			bSawFacing |= Drone->IsNoiseDirectionFacing();
			if (Fixture.Controller->GetPreferredVisibleTarget() == Fixture.Target)
			{
				TestTrue(TEXT("Sound-facing happened before actual sight"), bSawFacing);
				TestFalse(TEXT("Actual sight immediately releases sound rotation"), Drone->IsNoiseDirectionFacing());
				TestFalse(TEXT("Actual sight immediately releases old investigation"), Drone->IsNoiseInvestigationActive());
				for (int32 Check = 0; Check < 10; ++Check)
				{
					if (!Fixture.Advance()) return false;
					TestFalse(TEXT("Old search cannot overwrite reacquired combat"), Drone->IsNoiseDirectionConfirmationActive());
				}
				return TestEqual(TEXT("Actual detected target enters combat tracking"), Drone->GetTrackedTarget(), static_cast<AActor*>(Fixture.Target));
			}
		}
		AddError(TEXT("Sound-facing did not yield actual sight detection"));
		return false;
	}
	// 렌더 조건만 열어 실제 외형 갱신을 검사한다. 조준값이나 메시 회전은 테스트에서 덮지 않는다.
	const FBoolProperty* ShooterFlag = FindFProperty<FBoolProperty>(AEnemyBase::StaticClass(), TEXT("bAimTargetIsShooter"));
	if (!TestNotNull(TEXT("Replicated Shooter presentation flag"), ShooterFlag)) return false;
	ShooterFlag->SetPropertyValue_InContainer(Drone, true);
	if (!HearSecondary(Fixture, Drone->GetActorLocation() + FVector(0, 2400, -1000))) return false;
	bool bSawFacing = false;
	double BestError = 180.0;
	for (int32 Frame = 0; Frame < 48; ++Frame)
	{
		Drone->GetMesh()->SetLastRenderTime(Fixture.World->GetTimeSeconds());
		if (!Fixture.Advance()) return false;
		if (Drone->IsNoiseDirectionFacing())
		{
			bSawFacing = true;
			const double Desired = Drone->GetBaseAimRotation().GetNormalized().Pitch;
			const double Actual = Drone->GetThirdPersonTiltRoot()->GetComponentRotation().Pitch;
			if (Desired < -10.0) BestError = FMath::Min(BestError, FMath::Abs(FMath::FindDeltaAngleDegrees(Desired, Actual)));
		}
	}
	TestTrue(TEXT("Production presentation ran a sound confirmation"), bSawFacing);
	AddInfo(FString::Printf(TEXT("[NoiseFacingPitch] Best full-pitch error=%.3f"), BestError));
	return TestTrue(TEXT("Sound uses full pitch without Shooter half-angle or duplicate flight tilt"), BestError < 3.0);
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyNoiseBoundaryIntegrationTest,
	"Outlier.Enemy.Perception.NoiseBoundaryIntegration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyNoiseBoundaryIntegrationTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("FailedLoop"), TEXT("DoorRecovery"), TEXT("ExpiredActiveRetry"), TEXT("DoorEvents")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyNoiseBoundaryIntegrationTest::RunTest(const FString& Parameters)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	FEnemyCombatIntegrationWorld Fixture(*this, Parameters);
	if (!Fixture.Initialize(false, FVector(5000, 0, 0), false, false)) return false;
	const FVector Start = Fixture.Enemy->GetActorLocation();
	const FVector Goal = Start + FVector(1200, 0, 0);
	const auto SpawnDoor = [&](bool bInitiallyOpen, bool bAnimated)
	{
		const FTransform Transform(Start + FVector(600, 0, 0));
		AInteractableDoor* Door = Fixture.World->SpawnActorDeferred<AInteractableDoor>(AInteractableDoor::StaticClass(), Transform);
		if (!Door) return Door;
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		Door->DoorMeshLeft->SetStaticMesh(Cube);
		Door->DoorMeshRight->SetStaticMesh(Cube);
		Door->DoorMeshLeft->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Door->DoorMeshRight->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Door->bInitiallyOpen = bInitiallyOpen;
		Door->DoorId = MakeUniqueObjectName(Fixture.World, AInteractableDoor::StaticClass(), TEXT("NoiseBoundaryDoor"));
		if (bAnimated)
		{
			Door->DoorCurve = NewObject<UCurveFloat>(Door);
			Door->DoorCurve->FloatCurve.AddKey(0.0f, 0.0f);
			Door->DoorCurve->FloatCurve.AddKey(0.25f, 1.0f);
		}
		Door->FinishSpawning(Transform);
		return Door;
	};
	if (Parameters == TEXT("DoorEvents"))
	{
		URoomCombatSubsystem* Rooms = Fixture.World->GetSubsystem<URoomCombatSubsystem>();
		if (!TestNotNull(TEXT("Existing room subsystem"), Rooms)) return false;
		int32 OpenEvents = 0;
		const FDelegateHandle Handle = Rooms->OnMovementObstacleOpened.AddLambda([&](const FBox& Bounds)
		{
			TestTrue(TEXT("Door publishes finite local bounds"), Bounds.IsValid && !Bounds.Min.ContainsNaN() && !Bounds.Max.ContainsNaN());
			++OpenEvents;
		});
		ON_SCOPE_EXIT { Rooms->OnMovementObstacleOpened.Remove(Handle); };
		AInteractableDoor* Door = SpawnDoor(true, true);
		if (!TestNotNull(TEXT("Animated door"), Door)) return false;
		TestEqual(TEXT("Initial open placement is not a new cue"), OpenEvents, 0);
		Door->TrySetDoorOpen(true);
		TestEqual(TEXT("Repeated open request is not a new cue"), OpenEvents, 0);
		Door->TrySetDoorOpen(false);
		for (int32 Frame = 0; Frame < 20; ++Frame) Fixture.Tick(Fixture.Step);
		TestEqual(TEXT("Close completion is not an open cue"), OpenEvents, 0);
		Door->TrySetDoorOpen(true);
		for (int32 Frame = 0; Frame < 20; ++Frame) Fixture.Tick(Fixture.Step);
		TestEqual(TEXT("Actual animated opening signals once"), OpenEvents, 1);
		AInteractableDoor* Instant = SpawnDoor(false, false);
		if (!TestNotNull(TEXT("Instant door"), Instant)) return false;
		Instant->TrySetDoorOpen(true);
		TestEqual(TEXT("Actual instant opening also signals once"), OpenEvents, 2);
		Instant->TrySetDoorOpen(true);
		return TestEqual(TEXT("Repeated instant opening does not signal again"), OpenEvents, 2);
	}
	AActor* Wall = Fixture.SpawnBlockingBox(Start + FVector(600, 0, 0), FVector(80, 6000, 2000));
	if (!TestNotNull(TEXT("Sealed route obstacle"), Wall)) return false;
	AInteractableDoor* Door = SpawnDoor(false, true);
	if (!TestNotNull(TEXT("Route door"), Door)) return false;
	// 개방 완료와 실제 통로 변경을 함께 실행한다. 이동/조사 상태는 테스트에서 강제로 바꾸지 않는다.
	Door->OnDoorMotionFinished.AddLambda([Wall](AInteractableDoor*, bool bOpen) { if (bOpen && IsValid(Wall)) Wall->Destroy(); });
	if (Parameters == TEXT("ExpiredActiveRetry")) Fixture.Enemy->HeardNoiseLifetime = 0.15f;
	Fixture.Start();
	if (!HearInvestigationNoise(Fixture, Goal) || !WaitForNoiseState(Fixture, false)) return false;
	if (const auto* Flight = Fixture.Data<FEnemyFlyToLocationTask>())
	{
		// 도달 불가와 예산 부족을 분리한다. 작은 봉쇄 공간을 충분한 기존 탐색 예산으로 끝까지 검사한다.
		auto& FlightData = const_cast<FEnemyFlyToLocationTaskInstanceData&>(*Flight);
		FlightData.SearchPadding = 200.0f;
		FlightData.MaxSearchNodes = 4096;
		FlightData.MaxCollisionChecks = 32768;
		FlightData.MaxSearchMilliseconds = 50.0f;
	}
	const uint64 MoveSerial = Fixture.Enemy->GetNoiseInvestigationSerial();
	const double OriginalAt = Fixture.Enemy->GetActiveNoiseInvestigationClue().OccurredAt;
	if (Parameters == TEXT("ExpiredActiveRetry"))
	{
		for (int32 Frame = 0; Frame < 12; ++Frame) if (!Fixture.Advance()) return false;
		FEnemyHeardNoise Candidate;
		TestFalse(TEXT("Waiting candidate expires before reopening"), Fixture.Enemy->TryGetHeardNoise(Candidate));
		Door->TrySetDoorOpen(true);
		if (!WaitForNoiseState(Fixture, true)) return false;
		TestEqual(TEXT("Retry does not replace investigation episode"), Fixture.Enemy->GetNoiseInvestigationSerial(), MoveSerial);
		TestEqual(TEXT("Retry does not renew original cue time"), Fixture.Enemy->GetActiveNoiseInvestigationClue().OccurredAt, OriginalAt);
		return TestTrue(TEXT("Existing retry reaches original fixed point after expiry"), Fixture.Enemy->GetSearchEpisode().Center.Equals(Goal, 120.0));
	}
	if (!WaitForNoiseState(Fixture, true)) return false;
	const uint64 SearchSerial = Fixture.Enemy->GetSearchEpisode().Serial;
	TestFalse(TEXT("Failed goal was not reported as reached"), Fixture.Enemy->GetSearchEpisode().Center.Equals(Goal, 120.0));
	if (Parameters == TEXT("FailedLoop"))
	{
		for (int32 Event = 0; Event < 12; ++Event)
		{
			for (int32 Frame = 0; Frame < 6; ++Frame) if (!Fixture.Advance()) return false;
			if (!HearInvestigationNoise(Fixture, Goal + FVector(20, 0, 0))) return false;
			if (!TestEqual(TEXT("Repeated sound cannot restart failed investigation"), Fixture.Enemy->GetSearchEpisode().Serial, SearchSerial)) return false;
			TestTrue(TEXT("Repeated sound keeps safe reached-side search"), Fixture.Enemy->IsNoiseSearchActive());
		}
		return true;
	}
	Door->TrySetDoorOpen(true);
	for (int32 Frame = 0; Frame < 20; ++Frame) if (!Fixture.Advance()) return false;
	TestTrue(TEXT("Door-only event does not restart old investigation"), Fixture.Enemy->IsNoiseSearchActive());
	TestEqual(TEXT("Door-only event keeps search episode"), Fixture.Enemy->GetSearchEpisode().Serial, SearchSerial);
	if (!HearInvestigationNoise(Fixture, Goal + FVector(20, 0, 0)) || !WaitForNoiseState(Fixture, false)) return false;
	TestTrue(TEXT("Door plus fresh cue creates a new investigation"), Fixture.Enemy->GetNoiseInvestigationSerial() != MoveSerial);
	if (!WaitForNoiseState(Fixture, true)) return false;
	return TestTrue(TEXT("Reopened route reaches fresh sound point"), Fixture.Enemy->GetSearchEpisode().Center.Equals(Goal + FVector(20, 0, 0), 120.0));
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyCombatTacticalSelectionIntegrationTest,
	"Outlier.Enemy.Combat.Tactical.Selection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyCombatTacticalSelectionIntegrationTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : {TEXT("CandidateProgress"), TEXT("DeferredBudget"), TEXT("EndedEpisodeCallback"),
		TEXT("BudgetRecovery"), TEXT("VerticalGoalChange")})
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyCombatTacticalSelectionIntegrationTest::RunTest(const FString& Parameters)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	FEnemyCombatIntegrationWorld Fixture(*this, Parameters);
	if (!Fixture.Initialize(false, FVector(800, 0, 0))) return false;
	AEnemyBase* Enemy = Fixture.Enemy;
	const FVector Start = Enemy->GetActorLocation();
	auto Prepare = [&]()
	{
		FEnemyRunEnvQueryTaskInstanceData Data;
		Data.Enemy = Enemy;
		Data.bValidateReachability = true;
		Data.RunMode = EEnvQueryRunMode::AllMatching;
		Data.MaxPathCandidates = 1;
		Data.MaxSearchMilliseconds = 100.0f;
		EnemySearchTesting::CaptureQuery(Data);
		TestTrue(TEXT("Gun captures one persistent combat episode"), Data.CapturedCombatSerial != 0);
		TestTrue(TEXT("Actual request consumes the shared episode budget"), Enemy->TryBeginCombatTacticalRequest());
		return Data;
	};
	FEnemyRunEnvQueryTaskInstanceData Data = Prepare();
	const FVector Blocked = Start + FVector(0, 400, 0);
	const FVector Clear = Start + FVector(300, 0, 0);
	if (Parameters == TEXT("VerticalGoalChange"))
	{
		Enemy->EnterCombatObservationWait();
		EnemySearchTesting::ResumeCombatSelection(Data);
		TestTrue(TEXT("Old goal is held in observation"), Data.bCombatObservationWaiting);
		const uint64 OldSerial = Enemy->GetCombatTacticalEpisodeSerial();
		Fixture.Target->AddActorWorldOffset(FVector(0, 0, 250));
		TestEqual(TEXT("A meaningful vertical target change invalidates the old observation snapshot"),
			EnemySearchTesting::ResumeCombatSelection(Data), EStateTreeRunStatus::Failed);
		Data = Prepare();
		TestTrue(TEXT("The newly adopted vertical goal starts a different episode"), Data.CapturedCombatSerial != OldSerial);
		return TestFalse(TEXT("The new vertical goal has its own finite budget"), Enemy->IsCombatObservationWaiting());
	}
	if (Parameters == TEXT("EndedEpisodeCallback"))
	{
		Data.RequestId = 71;
		Data.QueryResult = EEnemyEnvQueryResult::Running;
		FEnvQueryInstance Result = MakePointResult(Enemy, {Clear});
		Result.QueryID = 71;
		Enemy->EnterCombatObservationWait();
		TestFalse(TEXT("Same lease callback from the ended episode is rejected before output mutation"),
			EnemySearchTesting::CompletePendingQuery(Data, Result));
		return TestFalse(TEXT("Late completion cannot publish a destination"), Data.bHasDestination);
	}
	IConsoleVariable* FrameChecks = IConsoleManager::Get().FindConsoleVariable(TEXT("outlier.Flight.SearchFrameChecks"));
	if (!TestNotNull(TEXT("Production world shared budget exists"), FrameChecks)) return false;
	const int32 OriginalChecks = FrameChecks->GetInt();
	ON_SCOPE_EXIT { FrameChecks->Set(OriginalChecks, ECVF_SetByCode); };
	if (Parameters == TEXT("DeferredBudget") || Parameters == TEXT("BudgetRecovery"))
	{
		FrameChecks->Set(0, ECVF_SetByCode);
		EnemySearchTesting::CompleteQuery(Data, MakePointResult(Enemy, {Clear}));
		TestTrue(TEXT("EQS completion defers combat path work"), Data.bCombatSelectionPending);
		TestEqual(TEXT("Starved candidate keeps the selection pending"),
			EnemySearchTesting::ResumeCombatSelection(Data), EStateTreeRunStatus::Running);
		TestFalse(TEXT("No destination is fabricated without a scheduler grant"), Data.bHasDestination);
		if (!TestTrue(TEXT("The suspended candidate retains its resumable search"), Data.CombatPathSearch.IsValid())) return false;
		TestEqual(TEXT("An ungranted candidate performs no path or sight collision checks"),
			Data.CombatPathSearch->GetResult().CollisionChecks, 0);
		TestFalse(TEXT("Budget wait is not cached as a physical obstruction"),
			Enemy->IsCombatTacticalCandidateFailure(TEXT("Approach"), Clear));
		if (Parameters == TEXT("DeferredBudget"))
		{
			FrameChecks->Set(OriginalChecks, ECVF_SetByCode);
			for (int32 Frame = 0; Frame < 90 && Data.bCombatSelectionPending; ++Frame)
			{
				++GFrameCounter;
				EnemySearchTesting::ResumeCombatSelection(Data);
			}
			return TestTrue(TEXT("Same pending candidate completes after an actual grant"), Data.bHasDestination);
		}
		// 대기 회차 종료와 복구 배정은 실제 Task의 공통 Tick 경로를 통과한다.
		Enemy->EnterCombatObservationWait(true);
		EnemySearchTesting::ResumeCombatSelection(Data);
		TestTrue(TEXT("Spent selection enters observation"), Data.bCombatObservationWaiting);
		const uint64 WaitingSerial = Enemy->GetCombatTacticalEpisodeSerial();
		for (int32 Frame = 0; Frame < 4; ++Frame)
		{
			++GFrameCounter;
			EnemySearchTesting::ResumeCombatSelection(Data);
		}
		TestEqual(TEXT("Repeated budget polls do not create a recovery interval"), Enemy->GetCombatTacticalEpisodeSerial(), WaitingSerial);
		FrameChecks->Set(OriginalChecks, ECVF_SetByCode);
		++GFrameCounter;
		EnemySearchTesting::ResumeCombatSelection(Data);
		TestFalse(TEXT("Real allocation opens the single bounded recovery interval"), Enemy->IsCombatObservationWaiting());
		TestFalse(TEXT("Recovery probe never supplies a movement destination"), Data.bHasDestination);
		return TestFalse(TEXT("The granted interval cannot be granted again"), Enemy->CanResumeCombatTacticalBudget());
	}
	Fixture.SpawnBlockingBox(Blocked, FVector(100));
	EnemySearchTesting::CompleteQuery(Data, MakePointResult(Enemy, {Blocked, Clear}));
	for (int32 Frame = 0; Frame < 90 && Data.bCombatSelectionPending; ++Frame)
	{
		++GFrameCounter;
		EnemySearchTesting::ResumeCombatSelection(Data);
	}
	TestEqual(TEXT("One candidate cap does not prove every destination unreachable"), Data.QueryResult, EEnemyEnvQueryResult::CandidateLimitReached);
	TestTrue(TEXT("Physical failed candidate is retained across Task instances"),
		Enemy->IsCombatTacticalCandidateFailure(TEXT("Approach"), Blocked));
	Data = Prepare();
	EnemySearchTesting::CompleteQuery(Data, MakePointResult(Enemy, {Blocked, Clear}));
	for (int32 Frame = 0; Frame < 90 && Data.bCombatSelectionPending; ++Frame)
	{
		++GFrameCounter;
		EnemySearchTesting::ResumeCombatSelection(Data);
	}
	TestTrue(TEXT("The next request skips the proven failed prefix and reaches the later candidate"), Data.bHasDestination);
	TestEqual(TEXT("Only the newly inspected candidate spends this request's candidate cap"), Data.CheckedPathCandidates, 1);
	return TestTrue(TEXT("The later valid position is selected"), Data.Destination.Equals(Clear));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyCombatTacticalSavedTreeIntegrationTest,
	"Outlier.Enemy.Combat.Tactical.SavedTreeObservationAndReopenedFire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyCombatTacticalSavedTreeIntegrationTest::RunTest(const FString& Parameters)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	FEnemyCombatIntegrationWorld Fixture(*this, TEXT("TacticalObservation"));
	if (!Fixture.Initialize(false, FVector(1800, 0, 0))) return false;
	IConsoleVariable* FrameChecks = IConsoleManager::Get().FindConsoleVariable(TEXT("outlier.Flight.SearchFrameChecks"));
	if (!TestNotNull(TEXT("Shared path scheduler can be controlled in this test"), FrameChecks)) return false;
	const int32 OriginalChecks = FrameChecks->GetInt();
	ON_SCOPE_EXIT { FrameChecks->Set(OriginalChecks, ECVF_SetByCode); };
	AActor* Cover = Fixture.SpawnBlockingBox(
		(Fixture.Enemy->GetActorLocation() + Fixture.Target->GetActorLocation()) * 0.5, FVector(25, 6000, 2500));
	if (!TestNotNull(TEXT("Real full-height cover exists"), Cover)) return false;
	FrameChecks->Set(0, ECVF_SetByCode);
	Fixture.Start();
	for (int32 Frame = 0; Frame < 360 && !Fixture.Enemy->IsCombatObservationWaiting(); ++Frame)
		if (!Fixture.Advance()) return TestTrue(TEXT("Actual Gun tree remains running until observation"), false);
	if (!TestTrue(TEXT("The actual saved Gun tree reaches bounded observation"), Fixture.Enemy->IsCombatObservationWaiting())) return false;
	const double LastRequestAt = Fixture.Enemy->GetNextEnvQueryRequestTime();
	const uint64 WaitingSerial = Fixture.Enemy->GetCombatTacticalEpisodeSerial();
	for (int32 Frame = 0; Frame < 240; ++Frame)
		if (!Fixture.Advance()) return TestTrue(TEXT("Observation keeps the actual combat tree running"), false);
	TestEqual(TEXT("Observation does not repeatedly issue EQS while the scheduler is starved"),
		Fixture.Enemy->GetNextEnvQueryRequestTime(), LastRequestAt);
	TestEqual(TEXT("Polling does not mint recovery episodes"), Fixture.Enemy->GetCombatTacticalEpisodeSerial(), WaitingSerial);
	TestTrue(TEXT("No old destination keeps moving the observed enemy"),
		Fixture.Enemy->GetCharacterMovement()->Velocity.IsNearlyZero(1.0f));
	TestEqual(TEXT("Observation preserves the directly observed target"), Fixture.Enemy->GetTrackedTarget(), static_cast<AActor*>(Fixture.Target));
	Cover->SetActorEnableCollision(false);
	Fixture.bSawFiring = false;
	for (int32 Frame = 0; Frame < 180 && !Fixture.bSawFiring; ++Frame)
		if (!Fixture.Advance()) return TestTrue(TEXT("Reopening keeps the actual combat tree running"), false);
	TestTrue(TEXT("Parallel real FireCycle can fire after reopening without a new path budget"), Fixture.bSawFiring);
	return !HasAnyErrors();
}
#endif

#endif
