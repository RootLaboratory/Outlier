#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "OutlierArenaSettings.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "Room/RoomTagComponent.h"
#include "Shooter/ShooterCharacter.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyPresentationProtectionTest,
	"Outlier.Presentation.Protection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyPresentationProtectionTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Name : {TEXT("AIPerceptionAndEvidence"), TEXT("SightAlert"), TEXT("SightCombat"),
		TEXT("AlertRefresh"), TEXT("AlertCommit"), TEXT("CombatRefresh"), TEXT("StunAlert"),
		TEXT("StunCombat"), TEXT("OtherSource"), TEXT("IndependentLocation"),
		TEXT("RoomHearing"), TEXT("RoomOtherSource"), TEXT("RoomIndependentLocation")})
	{
		Names.Add(Name);
		Commands.Add(Name);
	}
}

bool FEnemyPresentationProtectionTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("PresentationAI")));
	if (!TestNotNull(TEXT("World"), World)) return false;
	World->AddToRoot();
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	UGameInstance* Instance = NewObject<UGameInstance>(GEngine);
	World->SetGameInstance(Instance);
	Instance->Init();
	World->CreateAISystem();
	FURL URL;
	URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
	World->SetGameMode(URL);
	World->InitializeActorsForPlay(URL);
	World->BeginPlay();
	// 공유 보고의 실제 Arena 전파 경로를 사용한다. 설정은 이 테스트 동안만 바꾸고 저장하지 않는다.
	TGuardValue<TSoftObjectPtr<UWorld>> ArenaMap(GetMutableDefault<UOutlierArenaSettings>()->ArenaLevel,
		TSoftObjectPtr<UWorld>(World));
	ON_SCOPE_EXIT
	{
		World->BeginTearingDown();
		World->DestroyWorld(false);
		Instance->Shutdown();
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};

	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	UClass* EnemyClass = LoadClass<AEnemyBase>(nullptr,
		TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"));
	if (!TestNotNull(TEXT("Actual Shooter class"), ShooterClass)
		|| !TestNotNull(TEXT("Actual Enemy class"), EnemyClass)) return false;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AShooterCharacter* Target = World->SpawnActor<AShooterCharacter>(ShooterClass, FTransform(FVector(600, 0, 200)), Spawn);
	AShooterCharacter* OtherTarget = World->SpawnActor<AShooterCharacter>(ShooterClass, FTransform(FVector(900, 300, 200)), Spawn);
	AEnemyBase* Enemy = World->SpawnActor<AEnemyBase>(EnemyClass, FTransform(FVector(0, 0, 200)), Spawn);
	AEnemyAIController* AI = Enemy ? Cast<AEnemyAIController>(Enemy->GetController()) : nullptr;
	if (!AI) AI = World->SpawnActor<AEnemyAIController>();
	AActor* Owner = World->SpawnActor<AActor>();
	if (!Target || !OtherTarget || !Enemy || !AI || !Owner) return false;
	Enemy->GetOutlierAbilitySystemComponent()->InitializeVitalityToSelf(100.0f);
	const FGameplayTag Room = FGameplayTag::RequestGameplayTag(TEXT("Room.Level01.1"));
	Enemy->GetRoomTagComp()->AssignDefaultRoomTag(Room);
	AI->Possess(Enemy);
	UEnemyRoomSubsystem* Rooms = World->GetSubsystem<UEnemyRoomSubsystem>();
	Rooms->RegisterEnemy(Enemy);
	Rooms->SetActiveRoomTargetForTesting(Room, FVector::ZeroVector, false);
	if (Parameters != TEXT("AIPerceptionAndEvidence"))
	{
		const FVector Observed = Target->GetActorLocation();
		AActor* ExpectedSource = Target;
		EEnemyCombatState ExpectedState = EEnemyCombatState::Combat;
		const bool bRoomCase = Parameters.StartsWith(TEXT("Room"));
		FVector ExpectedLocation = Observed;
		if (bRoomCase)
		{
			if (Parameters == TEXT("RoomHearing"))
			{
				// 직접 시야 Contact가 없는 방에서 들린 위치만 전파한 경우도 정리해야 한다.
				Enemy->GetRoomTagComp()->AssignDefaultRoomTag(FGameplayTag::RequestGameplayTag(TEXT("Room.Level01.2")));
				Rooms->RegisterEnemy(Enemy);
			}
			else
			{
				ExpectedSource = Parameters == TEXT("RoomOtherSource") ? OtherTarget : nullptr;
				ExpectedLocation = FVector(1200, -300, 200);
			}
			TestTrue(TEXT("Room propagates the observation through its existing approval path"),
				Rooms->NotifyRoomCombat(Enemy->GetDefaultRoomTag(), ExpectedLocation, nullptr, ExpectedSource));
		}
		else if (Parameters == TEXT("SightAlert") || Parameters == TEXT("SightCombat"))
		{
			if (Parameters == TEXT("SightCombat")) Enemy->EnterCombatFromRoom(Observed, false);
			else
			{
				Rooms->NotifyRoomCombatEnded(Room);
				ExpectedState = EEnemyCombatState::Alert;
			}
			const FAIStimulus Seen(*GetDefault<UAISense_Sight>(), 1.0f, Observed, Enemy->GetActorLocation());
			AI->EnemyPerceptionComponent->RegisterStimulus(Target, Seen);
			AI->EnemyPerceptionComponent->ProcessStimuli();
			TestTrue(TEXT("Actual perception accepts direct sight"), AI->IsTargetDirectlyVisible(Target));
		}
		else if (Parameters == TEXT("StunAlert") || Parameters == TEXT("StunCombat"))
		{
			Enemy->EnterStun();
			if (Parameters == TEXT("StunAlert"))
			{
				ExpectedState = EEnemyCombatState::Alert;
				Enemy->EnterAlertFromPerception(Observed, Target);
			}
			else Enemy->EnterCombatFromRoom(Observed, false, true, Target);
			TestEqual(TEXT("Observation does not end stun early"), Enemy->GetCombatState(), EEnemyCombatState::Stun);
			Enemy->RestoreStateAfterStun();
		}
		else
		{
			Enemy->EnterAlertFromPerception(Observed, Target);
			if (Parameters == TEXT("AlertRefresh"))
			{
				ExpectedState = EEnemyCombatState::Alert;
				Enemy->EnterAlertFromPerception(Observed, Target);
			}
			else if (Parameters == TEXT("AlertCommit"))
			{
				// 기존 Room 승인 경로를 거쳐도 같은 관측의 출처를 잃지 않아야 한다.
				TestTrue(TEXT("Alert commits through the existing transition"), Enemy->CommitAlertToCombat());
			}
			else if (Parameters == TEXT("OtherSource"))
			{
				ExpectedSource = OtherTarget;
				Enemy->EnterCombatFromRoom(OtherTarget->GetActorLocation(), false, false, OtherTarget);
			}
			else if (Parameters == TEXT("IndependentLocation"))
			{
				ExpectedSource = nullptr;
				// 대상 없는 새 좌표를 이전 관측 대상의 단서로 잘못 보존하지 않는다.
				Enemy->EnterCombatFromRoom(FVector(1200, -300, 200), false);
			}
			else
			{
				Enemy->EnterCombatFromRoom(Observed, false, true, Target);
				Enemy->EnterCombatFromRoom(Observed, false, true, Target);
			}
		}
		TestEqual(TEXT("Transition keeps the original combat state"), Enemy->GetCombatState(), ExpectedState);
		TestEqual(TEXT("Last observation retains its exact source"), Enemy->LastKnownPlayerSource.Get(), ExpectedSource);
		TestTrue(TEXT("Fixture has a last observed location"), Enemy->bHasLastKnownPlayerLocation);
		const FGuid Execution = FGuid::NewGuid();
		TestTrue(TEXT("Protection starts through the actual server path"), Target->AcquirePresentationProtection(Owner, Execution));
		const bool bShouldRetainLocation = ExpectedSource != Target;
		TestEqual(TEXT("Only this target's old location is discarded"), Enemy->bHasLastKnownPlayerLocation, bShouldRetainLocation);
		if (bShouldRetainLocation)
		{
			TestEqual(TEXT("Unrelated source remains unchanged"), Enemy->LastKnownPlayerSource.Get(), ExpectedSource);
			TestEqual(TEXT("Unrelated combat state remains unchanged"), Enemy->GetCombatState(), ExpectedState);
		}
		else if (ExpectedState == EEnemyCombatState::Combat)
		{
			TestEqual(TEXT("Protection does not finish the encounter"), Enemy->GetCombatState(), EEnemyCombatState::Combat);
			TestFalse(TEXT("Discarded coordinates cannot start a search"), Enemy->BeginSearchEpisode());
		}
		Target->ReleasePresentationProtection(Owner, Execution);
		TestEqual(TEXT("Release does not restore stale coordinates"), Enemy->bHasLastKnownPlayerLocation, bShouldRetainLocation);
		if (bRoomCase)
		{
			AEnemyBase* Reinforcement = World->SpawnActor<AEnemyBase>();
			if (!TestNotNull(TEXT("Reinforcement"), Reinforcement)) return false;
			Reinforcement->GetRoomTagComp()->AssignDefaultRoomTag(Enemy->GetDefaultRoomTag());
			Rooms->RegisterEnemy(Reinforcement);
			TestEqual(TEXT("Room cache cannot restore protected target's stale evidence"),
				Reinforcement->bHasLastKnownPlayerLocation, bShouldRetainLocation);
			if (bShouldRetainLocation)
			{
				TestEqual(TEXT("Reinforcement inherits the latest source"), Reinforcement->LastKnownPlayerSource.Get(), ExpectedSource);
				TestTrue(TEXT("Stale Contact cannot overwrite the latest independent location"),
					Reinforcement->GetLastKnownPlayerLocation().Equals(ExpectedLocation));
			}
		}
		return true;
	}
	Enemy->EnterCombatFromRoom(Target->GetActorLocation(), false);
	const FAIStimulus Seen(*GetDefault<UAISense_Sight>(), 1.0f, Target->GetActorLocation(), Enemy->GetActorLocation());
	AI->EnemyPerceptionComponent->RegisterStimulus(Target, Seen);
	AI->EnemyPerceptionComponent->ProcessStimuli();
	TestTrue(TEXT("Fixture begins with real direct visibility"), AI->IsTargetDirectlyVisible(Target));
	Rooms->ReportRoomTargetContact(Enemy, Target, Target->GetActorLocation());
	TestTrue(TEXT("Fixture has shared target evidence"), Enemy->HasSharedTargetContact());

	const FGuid Id = FGuid::NewGuid();
	TestTrue(TEXT("Server accepts protection"), Target->AcquirePresentationProtection(Owner, Id));
	TestFalse(TEXT("Existing direct visibility is removed immediately"), AI->IsTargetDirectlyVisible(Target));
	TestFalse(TEXT("Existing tracked target is discarded"), Enemy->HasTrackableTarget());
	TestFalse(TEXT("Existing shared coordinate is discarded without a search event"), Enemy->HasSharedTargetContact());
	TestFalse(TEXT("Old coordinates cannot start a search"), Enemy->BeginSearchEpisode());
	AI->EnemyPerceptionComponent->RegisterStimulus(Target, Seen);
	AI->EnemyPerceptionComponent->ProcessStimuli();
	TestFalse(TEXT("Fresh sight during protection cannot reacquire"), AI->IsTargetDirectlyVisible(Target));
	Rooms->ReportRoomTargetContact(Enemy, Target, Target->GetActorLocation());
	TestFalse(TEXT("Shared reports cannot reacquire protected targets"), Enemy->HasSharedTargetContact());
	TestFalse(TEXT("Fresh protected sound is rejected"), Enemy->RecordHeardNoise(Target, Target->GetActorLocation(), World->GetTimeSeconds()));

	// 보호와 무관한 다른 대상의 실제 공유 보고는 유지한다.
	Rooms->ReportRoomTargetContact(Enemy, OtherTarget, OtherTarget->GetActorLocation());
	TestTrue(TEXT("Other target remains shareable"), Enemy->HasSharedTargetContact());
	Enemy->ForgetPresentationTarget(Target);
	TestTrue(TEXT("Repeated cleanup preserves another target's evidence"), Enemy->HasSharedTargetContact());
	Target->ReleasePresentationProtection(Owner, Id);
	TestTrue(TEXT("Final release restores tracking eligibility"), AI->IsValidTrackingTarget(Target));
	AI->EnemyPerceptionComponent->RegisterStimulus(Target, Seen);
	AI->EnemyPerceptionComponent->ProcessStimuli();
	TestTrue(TEXT("New sight after release is accepted"), AI->IsTargetDirectlyVisible(Target));
	return true;
}

#endif
