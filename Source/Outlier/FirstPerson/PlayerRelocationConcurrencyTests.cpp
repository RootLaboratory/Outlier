#if WITH_DEV_AUTOMATION_TESTS

#include "FirstPerson/PlayerRelocationEQS.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyEnvQuery.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "GameFramework/Character.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomTagComponent.h"
#include "Room/RoomVolume.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace
{
struct FConcurrentRelocationWorld
{
	UWorld* World = nullptr;
	UGameInstance* Instance = nullptr;
	FGameplayTag RoomTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")));
	FConcurrentRelocationWorld()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("RelocationConcurrency")), GetTransientPackage());
		World->AddToRoot();
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		Instance = NewObject<UGameInstance>(GEngine);
		World->SetGameInstance(Instance);
		Instance->Init();
		World->CreateAISystem();
		World->InitializeActorsForPlay(FURL());
		AActor* Floor = World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Floor);
		Floor->SetRootComponent(Box);
		Box->SetBoxExtent(FVector(100000, 1000, 10));
		Box->SetCollisionProfileName(TEXT("BlockAll"));
		Box->RegisterComponent();
		Floor->SetActorLocation(FVector(0, 0, -10));
		World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
		World->GetPhysicsScene()->Flush();
		// Enemy 에셋의 실제 방/캡슐 목적지 필터가 동작하도록 등록된 방을 준비한다.
		URoomCombatDefinition* Definition = NewObject<URoomCombatDefinition>(World);
		FRoomCombatRoomDefinition& Entry = Definition->RoomDefinitions.AddDefaulted_GetRef();
		Entry.RoomTag = RoomTag;
		Entry.CombatPhases.AddDefaulted();
		URoomCombatSubsystem* Rooms = World->GetSubsystem<URoomCombatSubsystem>();
		Rooms->SetCombatDefinitionForTesting(Definition);
		ARoomVolume* Room = World->SpawnActor<ARoomVolume>();
		*FindFProperty<FStructProperty>(Room->GetClass(), TEXT("RoomTag"))
			->ContainerPtrToValuePtr<FGameplayTag>(Room) = RoomTag;
		CastChecked<UBoxComponent>(Room->GetRootComponent())->SetBoxExtent(FVector(100000, 10000, 1500));
		Rooms->RegisterRoom(Room, RoomTag);
	}
	~FConcurrentRelocationWorld()
	{
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		Instance->Shutdown();
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}
	ACharacter* Character(const FVector& Location)
	{
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACharacter* Result = World->SpawnActor<ACharacter>(ACharacter::StaticClass(), Location, FRotator::ZeroRotator, Spawn);
		Result->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		return Result;
	}
};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FRelocationConcurrencyTest, "Outlier.Player.RelocationEQS.Concurrent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FRelocationConcurrencyTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Profile : {TEXT("Player"), TEXT("Enemy"), TEXT("Mixed")})
		for (int32 Count : {1, 8, 32})
		{
			const FString Name = FString::Printf(TEXT("%s.%d"), Profile, Count);
			Names.Add(Name);
			Commands.Add(Name);
		}
	Names.Add(TEXT("TerminationIsolation"));
	Commands.Add(TEXT("TerminationIsolation"));
}

bool FRelocationConcurrencyTest::RunTest(const FString& Parameters)
{
	FConcurrentRelocationWorld Fixture;
	UEnvQueryManager* Manager = UEnvQueryManager::GetCurrent(Fixture.World);
	if (!TestNotNull(TEXT("Both request types share one real EQS manager"), Manager)) return false;
	const bool bTermination = Parameters == TEXT("TerminationIsolation");
	FString Profile, CountText;
	Parameters.Split(TEXT("."), &Profile, &CountText);
	const int32 Count = bTermination ? 8 : FCString::Atoi(*CountText);
	const bool bPlayers = bTermination || Profile != TEXT("Enemy");
	const bool bEnemies = bTermination || Profile != TEXT("Player");
	TArray<TStrongObjectPtr<UPlayerRelocationQuery>> Players;
	TArray<int32> PlayerCallbacks, EnemyCallbacks, EnemyIds;
	TArray<FPlayerRelocationResult> PlayerResults;
	TArray<TSharedPtr<FEnvQueryResult>> EnemyResults;
	TArray<FVector> References;
	TArray<double> CompletionMilliseconds, TickMilliseconds;
	PlayerCallbacks.Init(0, Count);
	EnemyCallbacks.Init(0, Count);
	PlayerResults.SetNum(Count);
	EnemyResults.SetNum(Count);
	References.SetNum(Count);
	Players.Reserve(Count);
	bool bContextCurrent = true;
	TStrongObjectPtr<UEnvQuery> EnemyTemplate(bEnemies ? LoadObject<UEnvQuery>(nullptr,
		TEXT("/Game/Blueprints/AI/VECDrone/EQS/EQS_Drone_SearchAnchor.EQS_Drone_SearchAnchor")) : nullptr);
	if (bEnemies)
	{
		if (!TestNotNull(TEXT("Existing production Enemy EQS asset loads"), EnemyTemplate.Get())) return false;
		if (!TestTrue(TEXT("Enemy asset retains real EQS tests"),
			!EnemyTemplate->GetOptions().IsEmpty() && !EnemyTemplate->GetOptions()[0]->Tests.IsEmpty())) return false;
	}
	const double Started = FPlatformTime::Seconds();
	// 모든 요청을 먼저 제출한 뒤 같은 매니저를 돌린다. 요청마다 좌표를 분리해 결과 혼입도 검사한다.
	for (int32 Index = 0; Index < Count; ++Index)
	{
		References[Index] = FVector(Index * 2000, 0, 110);
		if (bPlayers)
		{
			FPlayerRelocationRequest Request;
			Request.Anchor = Fixture.Character(References[Index]);
			Request.MovingCharacter = Fixture.Character(References[Index] + FVector(0, -800, 0));
			Request.Reference = FTransform(References[Index]);
			Request.TimeoutSeconds = bTermination && Index == 3 ? .01 : 5;
			if (bTermination && Index == 2) Request.MaxCollisionChecks = 1;
			if (bTermination && Index == 1) Request.IsContextCurrent = [&bContextCurrent] { return bContextCurrent; };
			Players.Emplace(NewObject<UPlayerRelocationQuery>(Fixture.World));
			TestTrue(TEXT("Player request starts asynchronously"), Players.Last()->Start(Request,
				[&, Index](const FPlayerRelocationResult& Result)
				{
					++PlayerCallbacks[Index];
					PlayerResults[Index] = Result;
					CompletionMilliseconds.Add((FPlatformTime::Seconds() - Started) * 1000);
				}));
		}
		if (bEnemies)
		{
			AEnemyBase* Enemy = Fixture.World->SpawnActor<AEnemyBase>();
			const FVector Center(References[Index].X, 5000, 400);
			Enemy->SetActorLocation(Center);
			Enemy->GetRoomTagComp()->AssignDefaultRoomTag(Fixture.RoomTag);
			Enemy->EnterCombatFromRoom(Center, false);
			Enemy->UpdateLastKnownPlayerLocation(Center);
			TestTrue(TEXT("Enemy starts a real search episode"), Enemy->BeginSearchEpisode());
			FEnvQueryRequest Request(EnemyTemplate.Get(), Enemy);
			EnemySearch::SetQuerySnapshot(Request, Center, 400);
			const int32 Id = Request.Execute(EEnvQueryRunMode::AllMatching,
				FQueryFinishedSignature::CreateLambda([&, Index](TSharedPtr<FEnvQueryResult> Result)
				{
					++EnemyCallbacks[Index];
					EnemyResults[Index] = Result;
				}));
			EnemyIds.Add(Id);
			TestTrue(TEXT("Actual Enemy EQS starts"), Id != INDEX_NONE);
		}
	}
	// 실패해도 스택을 참조하는 완료 통보를 다음 테스트까지 남기지 않는다.
	ON_SCOPE_EXIT
	{
		for (auto& Player : Players) Player->Cancel();
		for (int32 Id : EnemyIds) if (Id != INDEX_NONE) Manager->AbortQuery(Id);
	};
	if (bTermination)
	{
		Players[0]->Cancel();
		Players[0]->Cancel();
		bContextCurrent = false;
		FPlatformProcess::Sleep(.02f);
	}
	const auto IsComplete = [&]
	{
		for (int32 Index = 0; Index < Count; ++Index)
			if ((bPlayers && PlayerCallbacks[Index] == 0) || (bEnemies && EnemyCallbacks[Index] == 0)) return false;
		return true;
	};
	for (int32 Frame = 0; Frame < 1000 && !IsComplete() && FPlatformTime::Seconds() - Started < 10; ++Frame)
	{
		const double Before = FPlatformTime::Seconds();
		Manager->Tick(.016f);
		TickMilliseconds.Add((FPlatformTime::Seconds() - Before) * 1000);
	}
	TestTrue(TEXT("All requests terminate within bounded pumping"), IsComplete());
	// 추가 Tick/취소가 완료 통보를 다시 발행하지 않는지도 확인한다.
	for (int32 Frame = 0; Frame < 3; ++Frame) Manager->Tick(.016f);
	int32 TotalChecks = 0, TotalCandidates = 0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (bPlayers)
		{
			TestEqual(TEXT("Player callback exactly once"), PlayerCallbacks[Index], 1);
			const auto Expected = bTermination && Index == 0 ? EPlayerRelocationStatus::Cancelled
				: bTermination && Index == 1 ? EPlayerRelocationStatus::InvalidContext
				: bTermination && Index == 2 ? EPlayerRelocationStatus::BudgetExceeded
				: bTermination && Index == 3 ? EPlayerRelocationStatus::TimedOut : EPlayerRelocationStatus::Success;
			TestEqual(TEXT("One request ending does not alter its neighbors"), PlayerResults[Index].Status, Expected);
			const int32 CheckBudget = bTermination && Index == 2 ? 1 : 2048;
			TestTrue(TEXT("Collision checks stay in this request's budget"), PlayerResults[Index].CollisionChecks <= CheckBudget);
			TestTrue(TEXT("Generated candidates stay in four-stage budget"), PlayerResults[Index].GeneratedCandidates <= 4 * 256);
			if (Expected == EPlayerRelocationStatus::Success)
			{
				TestTrue(TEXT("Player result retains its own reference"), FVector::Dist2D(PlayerResults[Index].Location, References[Index]) <= 200.01);
				TestTrue(TEXT("Player result remains on actual floor"), PlayerResults[Index].Location.Z > 80 && PlayerResults[Index].Location.Z < 120);
			}
			TotalChecks += PlayerResults[Index].CollisionChecks;
			TotalCandidates += PlayerResults[Index].GeneratedCandidates;
		}
		if (bEnemies)
		{
			TestEqual(TEXT("Enemy callback exactly once"), EnemyCallbacks[Index], 1);
			const TSharedPtr<FEnvQueryResult>& Result = EnemyResults[Index];
			if (TestTrue(TEXT("Real Enemy asset filters supply valid results"), Result.IsValid() && Result->IsSuccessful()))
			{
				TestEqual(TEXT("Enemy result retains request identity"), Result->QueryID, EnemyIds[Index]);
				TestTrue(TEXT("Enemy safety filters retain bounded nonempty results"), Result->Items.Num() > 0 && Result->Items.Num() <= 73);
				for (int32 Item = 0; Item < Result->Items.Num(); ++Item)
					TestTrue(TEXT("Enemy result retains its own snapshot"), EnemySearch::IsCandidateInRange(Result->GetItemAsLocation(Item),
						FVector(References[Index].X, 5000, 400), 400, EEnemySearchQueryMode::Anchor));
			}
		}
	}
	TickMilliseconds.Sort();
	CompletionMilliseconds.Sort();
	const auto Percentile = [](const TArray<double>& Values, double Percent)
	{
		return Values.IsEmpty() ? 0.0 : Values[FMath::Clamp(FMath::CeilToInt(Values.Num() * Percent) - 1, 0, Values.Num() - 1)];
	};
	AddInfo(FString::Printf(TEXT("RelocationConcurrency Profile=%s Requests=%d WallMs=%.3f ManagerTicks=%d TickP50Ms=%.3f TickP95Ms=%.3f TickMaxMs=%.3f PlayerCompletionP95Ms=%.3f Checks=%d Candidates=%d; actual Enemy EQS asset with tests, manager pumping only, no Actor Tick/StateTree/full path search/network cost"),
		*Parameters, Count, (FPlatformTime::Seconds() - Started) * 1000, TickMilliseconds.Num(), Percentile(TickMilliseconds, .5),
		Percentile(TickMilliseconds, .95), Percentile(TickMilliseconds, 1), Percentile(CompletionMilliseconds, .95), TotalChecks, TotalCandidates));
	return true;
}

#endif
