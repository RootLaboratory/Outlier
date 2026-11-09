#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/VECDrone.h"
#include "Enemy/EnemyStateTreeComponent.h"
#include "Enemy/EnemyStateTreeMovementTasks.h"
#include "Enemy/EnemyCombatTestHelpers.h"
#include "Enemy/EnemyPoolSubsystem.h"
#include "Enemy/EnemyPoolDefinition.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomTagComponent.h"
#include "Room/RoomVolume.h"
#include "Shooter/ShooterCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformTime.h"
#include "UObject/UnrealType.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISense_Hearing.h"

namespace EnemyHearingNetworkProbe
{
// 명시적 실행 인자가 있는 개발 검증 프로세스만 구독한다. 게임의 상시 Tick/복제 경로는 추가하지 않는다.
struct FProbe
{
	FString Role, Output;
	bool bEnabled = false, bHeader = false, bWarmed = false;
	double WallStarted = 0.0, ConnectedAt = -1.0, CaseStarted = -1.0, LastSample = -1.0;
	int32 Case = -1, Stage = 0;
	TWeakObjectPtr<AVECDrone> Drone;
	TWeakObjectPtr<AShooterCharacter> Source;
	TWeakObjectPtr<AEnemyAIController> AI;
	TWeakObjectPtr<ARoomVolume> Room;
	TWeakObjectPtr<AActor> Wall;
	FVector InitialLocation, FirstGoal;
	double CueAt = -1.0;
	uint64 Investigation = 0;
	bool bSawFacing = false, bSawSearch = false, bSawPreempt = false;
	int32 Starts = 0;
	uint64 LastConfirmation = 0;
	double LastConfirmationStartedAt = -1.0;
	bool bIntervalValid = true;
	FGameplayTag RoomTag;

	bool Enabled(UWorld* World) const
	{
		return bEnabled && World && World->IsGameWorld() && World->HasBegunPlay()
			&& (Role == TEXT("Server") ? World->GetNetMode() == NM_DedicatedServer : World->GetNetMode() == NM_Client);
	}

	AActor* Block(UWorld* World, FVector Location, FVector Extent)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
		Actor->SetRootComponent(Box);
		Box->SetBoxExtent(Extent);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->RegisterComponent();
		Actor->SetActorLocation(Location);
		return Actor;
	}

	void Emit(UWorld* World, const FVector& Location, bool bSight = false)
	{
		if (!AI.IsValid() || !Source.IsValid()) return;
		const UAISense& Sense = bSight ? static_cast<const UAISense&>(*GetDefault<UAISense_Sight>())
			: static_cast<const UAISense&>(*GetDefault<UAISense_Hearing>());
		FAIStimulus Stimulus(Sense, 1.0f, Location, Drone->GetActorLocation());
		AI->GetPerceptionComponent()->RegisterStimulus(Source.Get(), Stimulus);
		AI->GetPerceptionComponent()->ProcessStimuli();
	}

	void Setup(UWorld* World)
	{
		RoomTag = FGameplayTag::RequestGameplayTag(TEXT("Room.Level01.1"));
		URoomCombatSubsystem* Rooms = World->GetSubsystem<URoomCombatSubsystem>();
		URoomCombatDefinition* Definition = NewObject<URoomCombatDefinition>(Rooms);
		auto& Entry = Definition->RoomDefinitions.AddDefaulted_GetRef();
		Entry.RoomTag = RoomTag;
		Entry.CombatPhases.AddDefaulted();
		Rooms->SetCombatDefinitionForTesting(Definition);
		ARoomVolume* Volume = World->SpawnActor<ARoomVolume>();
		CastChecked<UBoxComponent>(Volume->GetRootComponent())->SetBoxExtent(FVector(6000, 120000, 2500));
		Rooms->RegisterRoom(Volume, RoomTag);
		Room = Volume;
		Block(World, FVector(0, 0, -50), FVector(6000, 120000, 50));
	}

	void BeginCase(UWorld* World)
	{
		if (Drone.IsValid()) Drone->Destroy();
		if (Source.IsValid()) Source->Destroy();
		if (Wall.IsValid()) Wall->Destroy();
		++Case;
		Stage = Starts = 0;
		bSawFacing = bSawSearch = bSawPreempt = false;
		LastConfirmation = 0;
		LastConfirmationStartedAt = -1.0;
		bIntervalValid = true;
		CaseStarted = World->GetTimeSeconds();
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		UClass* Class = LoadClass<AVECDrone>(nullptr, TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"));
		Drone = World->SpawnActor<AVECDrone>(Class, FVector(0, Case * 10000, 300), FRotator::ZeroRotator, Spawn);
		if (Case == 10)
		{
			Drone->Destroy();
			UEnemyPoolSubsystem* Pool = World->GetSubsystem<UEnemyPoolSubsystem>();
			UEnemyPoolDefinition* Definition = NewObject<UEnemyPoolDefinition>(Pool);
			auto& Entry = Definition->Entries.AddDefaulted_GetRef();
			Entry.EnemyClass = Class; Entry.PrewarmCount = Entry.MaxCount = 1;
			Pool->PrewarmPool(Definition);
			FEnemyPoolLeaseContext Context; Context.RoomTag = RoomTag; Context.GameplayGeneration = 1;
			Drone = Cast<AVECDrone>(Pool->LeaseEnemy(Class, FTransform(FVector(0, Case * 10000, 300)), Context));
			if (Drone.IsValid() && Drone->GetEnemyPoolState() == EEnemyPoolState::SpawnPresentation)
				Drone->CompletePoolSpawnPresentation(Drone->GetPoolGameplayGeneration(), Drone->GetPoolLeaseSerial());
		}
		if (!Drone.IsValid()) { UE_LOG(LogTemp, Error, TEXT("[HearingNetwork] Failed to create case %d"), Case); return; }
		Drone->bAlwaysRelevant = true;
		AI = Cast<AEnemyAIController>(Drone->GetController());
		if (!AI.IsValid()) { UE_LOG(LogTemp, Error, TEXT("[HearingNetwork] Missing active controller case %d"), Case); FPlatformMisc::RequestExit(true); return; }
		Drone->GetStateTreeComponent()->StopLogic(TEXT("Hearing network scenario setup"));
		AI->SetEnemyPerceptionEnabled(false);
		Drone->GetRoomTagComp()->AssignDefaultRoomTag(RoomTag);
		InitialLocation = Drone->GetActorLocation();
		FirstGoal = InitialLocation + FVector(Case == 1 || Case == 8 ? 100 : 1800, 0, 0);
		Drone->EnterAlertFromPerception(FirstGoal);
		Drone->NoiseDestinationUpdateInterval = 10.0f;
		if (Case == 9) Drone->NoiseDestinationUpdateInterval = 1.0f;
		Spawn.Name = NAME_None;
		UClass* PlayerClass = LoadClass<AShooterCharacter>(nullptr, TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
		Source = World->SpawnActor<AShooterCharacter>(PlayerClass, InitialLocation + FVector(0, 4000, 0), FRotator::ZeroRotator, Spawn);
		Source->SetActorEnableCollision(false);
		Source->SetCanBeDamaged(false);
		Source->GetCharacterMovement()->DisableMovement();
		Source->SetActorTickEnabled(false);
		// 자동 시야만 막고 감지 콜백·인지·StateTree는 실제 구현을 사용한다.
		AI->GetPerceptionComponent()->SetSenseEnabled(UAISense_Sight::StaticClass(), false);
		Drone->GetStateTreeComponent()->StartLogic();
		if (Case == 9) Wall = Block(World, InitialLocation + FVector(600, 0, 0), FVector(80, 6000, 2000));
		Emit(World, FirstGoal);
	}

	void Result(UWorld* World)
	{
		const bool bBehaviorPassed = Case == 8 ? bSawSearch && bSawPreempt
			: Case == 9 ? bSawSearch && bSawPreempt : bSawFacing && (Case < 2 || Case == 7 || bSawPreempt);
		const bool bPassed = bBehaviorPassed && bIntervalValid && (Case != 7 || Starts >= 2);
		const FString Line = FString::Printf(TEXT("%d,%d,%d,%d,%d,%d,%.3f,%d\n"), Case, bPassed, bSawFacing, bSawSearch, bSawPreempt, Starts,
			FVector::Distance(InitialLocation, Drone->GetActorLocation()), bIntervalValid);
		FFileHelper::SaveStringToFile(Line, *(FPaths::GetPath(Output) / TEXT("Cases.csv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
			&IFileManager::Get(), FILEWRITE_Append);
		if (!bPassed) UE_LOG(LogTemp, Error, TEXT("[HearingNetwork] Failed case %d Stage=%d"), Case, Stage);
	}

	void Before(UWorld* World, ELevelTick, float)
	{
		if (!Enabled(World)) return;
		if (FPlatformTime::Seconds() - WallStarted > 160) { FPlatformMisc::RequestExit(false); return; }
		if (Role != TEXT("Server"))
		{
			for (TActorIterator<AVECDrone> It(World); It; ++It) It->GetMesh()->SetLastRenderTime(World->GetTimeSeconds());
			return;
		}
		if (!World->GetNetDriver() || World->GetNetDriver()->ClientConnections.Num() < 2) return;
		const double Now = World->GetTimeSeconds();
		if (ConnectedAt < 0.0) { ConnectedAt = Now; Setup(World); }
		if (Now - ConnectedAt < 2.0) return;
		if (Case < 0) BeginCase(World);
		if (!bWarmed)
		{
			// 첫 BP/메시 비동기 로딩이 짧은 확인 구간을 가리지 않게 예열 Actor를 먼저 복제한다.
			if (Now - CaseStarted < 8.0) return;
			Case = -1;
			BeginCase(World);
			bWarmed = true;
		}
		if (!Drone.IsValid() || !AI.IsValid()) return;
		const bool bFacing = Drone->IsNoiseDirectionConfirmationActive();
		bSawFacing |= bFacing;
		bSawSearch |= Drone->IsNoiseSearchActive();
		if (bFacing && Drone->GetNoiseDirectionConfirmation().Serial != LastConfirmation)
		{
			LastConfirmation = Drone->GetNoiseDirectionConfirmation().Serial;
			const double StartedAt = Drone->GetNoiseDirectionConfirmation().StartedAt;
			if (LastConfirmationStartedAt >= 0.0 && StartedAt - LastConfirmationStartedAt < .999) bIntervalValid = false;
			LastConfirmationStartedAt = StartedAt;
			++Starts;
		}
		if (Case == 9)
		{
			const auto Snapshot = EnemyCombatTesting::ReadComponent(*CastChecked<UEnemyStateTreeComponent>(Drone->GetStateTreeComponent()));
			if (const auto* Task = Snapshot.FindTask<FEnemyFlyToLocationTask>())
			{
				auto* Data = const_cast<FEnemyFlyToLocationTaskInstanceData*>(Task->Data.GetPtr<const FEnemyFlyToLocationTaskInstanceData>());
				Data->SearchPadding = 200;
				Data->MaxSearchNodes = 4096; Data->MaxCollisionChecks = 32768; Data->MaxSearchMilliseconds = 50;
			}
			if (Stage == 0 && Drone->IsNoiseSearchActive()) { Emit(World, FirstGoal + FVector(20, 0, 0)); Stage = 1; CueAt = Now; }
			if (Stage == 1 && Now - CueAt > .4)
			{
				if (Wall.IsValid()) Wall->Destroy();
				World->GetSubsystem<URoomCombatSubsystem>()->NotifyMovementObstacleOpened(FBox(InitialLocation, FirstGoal).ExpandBy(100));
				Emit(World, FirstGoal + FVector(25, 0, 0)); Stage = 2;
			}
			if (Stage == 2 && Drone->IsNoiseSearchActive() && FVector::Distance(Drone->GetActorLocation(), FirstGoal) < 150) bSawPreempt = true;
		}
		else if (Stage == 0 && (Case == 1 || Case == 8 ? Drone->IsNoiseSearchActive() : Drone->IsNoiseInvestigationActive()))
		{
			Investigation = Drone->GetNoiseInvestigationSerial();
			FVector Offset(0, 2400, -1000);
			if (Case == 1) Offset = FVector(-2400, 0, 600);
			if (Case == 2) Offset = FVector(0, 1000, 0);
			if (Case == 4) Offset = FVector(150, 40, 0);
			Emit(World, Drone->GetActorLocation() + Offset); Stage = 1; CueAt = Now;
		}
		else if (Stage == 1 && bFacing && Now - CueAt > .15)
		{
			if (Case == 2) Emit(World, Source->GetActorLocation(), true);
			if (Case == 3)
			{
				FEnemyHitClue Hit;
				if (Drone->CreateHitClue(Drone->GetActorLocation() + FVector(0, -700, 0), Now, 6.0, Hit)) Drone->RecordHitClue(Hit);
			}
			if (Case == 5) Drone->EnterStun();
			if (Case == 10)
			{
				const uint64 OldSerial = Drone->GetNoiseDirectionConfirmation().Serial;
				AVECDrone* Previous = Drone.Get();
				UEnemyPoolSubsystem* Pool = World->GetSubsystem<UEnemyPoolSubsystem>();
				if (!Pool->ReturnEnemy(Previous, Previous->GetPoolGameplayGeneration(), Previous->GetPoolLeaseSerial()))
				{
					UE_LOG(LogTemp, Error, TEXT("[HearingNetwork] Pool return failed")); FPlatformMisc::RequestExit(true); return;
				}
				FEnemyPoolLeaseContext Context; Context.RoomTag = RoomTag; Context.GameplayGeneration = 2;
				Drone = Cast<AVECDrone>(Pool->LeaseEnemy(Previous->GetClass(), FTransform(InitialLocation), Context));
				if (Drone.IsValid() && Drone->GetEnemyPoolState() == EEnemyPoolState::SpawnPresentation)
					Drone->CompletePoolSpawnPresentation(Drone->GetPoolGameplayGeneration(), Drone->GetPoolLeaseSerial());
				if (!Drone.IsValid()) { UE_LOG(LogTemp, Error, TEXT("[HearingNetwork] Second lease failed")); FPlatformMisc::RequestExit(true); return; }
				AI = Cast<AEnemyAIController>(Drone->GetController());
				if (!AI.IsValid()) { UE_LOG(LogTemp, Error, TEXT("[HearingNetwork] Second lease has no controller")); FPlatformMisc::RequestExit(true); return; }
				AI->GetPerceptionComponent()->SetSenseEnabled(UAISense_Sight::StaticClass(), false);
				bSawPreempt = Drone.Get() == Previous && !Drone->IsNoiseDirectionFacing() && !Drone->NotifyNoiseDirectionReached(OldSerial);
			}
			if (Case == 6)
			{
				AI->UnPossess();
				AEnemyAIController* Replacement = World->SpawnActor<AEnemyAIController>();
				Replacement->Possess(Drone.Get()); AI = Replacement;
				AI->GetPerceptionComponent()->SetSenseEnabled(UAISense_Sight::StaticClass(), false);
			}
			if (Case == 2 || Case == 3 || Case == 5 || Case == 6 || Case == 10) Stage = 2;
		}
		if (Case == 4 && Stage == 1 && bSawFacing && !bFacing && Now - CueAt < .9) { bSawPreempt = true; Stage = 2; }
		if ((Case == 2 || Case == 3 || Case == 5 || Case == 6) && Stage == 2 && !bFacing) bSawPreempt = true;
		if (Case == 7 && Stage >= 1 && Now - CueAt > .25 && Now - CaseStarted < 4.0)
		{
			const double Side = int32((Now - CaseStarted) * 4) % 2 ? -1.0 : 1.0;
			Emit(World, Drone->GetActorLocation() + FVector(0, Side * 2400, 0)); CueAt = Now;
		}
		if (Case == 8 && bSawSearch && !Drone->IsNoiseInvestigationActive()) bSawPreempt = true;
		const double Duration = Case == 9 ? 14.0 : Case == 8 ? 9.0 : Case == 7 ? 7.0 : 5.0;
		if (Now - CaseStarted > Duration)
		{
			Result(World);
			if (Case == 10) { FPlatformMisc::RequestExit(false); return; }
			BeginCase(World);
		}
	}

	void After(UWorld* World, ELevelTick, float)
	{
		if (!Enabled(World)) return;
		// 동일 PC의 세 프로세스는 공통 QPC 시계를 사용한다. GameState가 없는 최소 테스트 맵의 로컬 게임 시각을 비교하지 않는다.
		const double Now = FPlatformTime::Seconds();
		if (Now - LastSample < .05) return;
		LastSample = Now;
		FString Rows;
		if (!bHeader) { Rows = TEXT("time,mode,case,facing,x,y,z,yaw,pitch,bodyPitch,stage,serial,state,generation,lease\n"); bHeader = true; }
		for (TActorIterator<AVECDrone> It(World); It; ++It)
		{
			AVECDrone* Actor = *It;
			// 동적 Actor 이름은 클라이언트에서 달라질 수 있다. 충분히 분리한 배치 레인으로 사례를 식별한다.
			const int32 Id = FMath::RoundToInt(Actor->GetActorLocation().Y / 10000.0);
			if (Id < 0 || Id > 10) continue;
			const FVector Location = Actor->GetActorLocation();
			Rows += FString::Printf(TEXT("%.4f,%d,%d,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%llu,%d,%d,%d\n"), Now, int32(World->GetNetMode()), Id,
				Actor->IsNoiseDirectionFacing(), Location.X, Location.Y, Location.Z, Actor->GetActorRotation().Yaw,
				Actor->GetBaseAimRotation().GetNormalized().Pitch, Actor->GetThirdPersonTiltRoot()->GetComponentRotation().Pitch,
				Role == TEXT("Server") ? Stage : -1, Actor->GetNoiseDirectionConfirmation().Serial,
				int32(Actor->GetCombatState()), Actor->GetPoolGameplayGeneration(), Actor->GetPoolLeaseSerial());
		}
		FFileHelper::SaveStringToFile(Rows, *Output, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
	}
};

FProbe Probe;
struct FRegister
{
	FRegister()
	{
		Probe.bEnabled = FParse::Value(FCommandLine::Get(), TEXT("OutlierHearingProbe="), Probe.Role)
			&& FParse::Value(FCommandLine::Get(), TEXT("OutlierHearingOutput="), Probe.Output);
		if (!Probe.bEnabled) return;
		Probe.WallStarted = FPlatformTime::Seconds();
		FWorldDelegates::OnWorldTickStart.AddRaw(&Probe, &FProbe::Before);
		FWorldDelegates::OnWorldPostActorTick.AddRaw(&Probe, &FProbe::After);
	}
} Register;
}
#endif
