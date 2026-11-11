#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Conditions/StateTreeCommonConditions.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StateTreeComponent.h"
#include "Drone/FlightPathfinding.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyCombatTestHelpers.h"
#include "Enemy/EnemyStateTreeMovementTasks.h"
#include "Enemy/EnemyStateTreeReturnTasks.h"
#include "Enemy/EnemyStateTreeTacticalTasks.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomTagComponent.h"
#include "Room/RoomVolume.h"
#include "StateTree.h"
#include "StateTreeCompiler.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditorData.h"
#include "StateTreePropertyBindings.h"
#include "StateTreeState.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
void CollectF5States(UStateTreeState* State, TArray<UStateTreeState*>& States)
{
	if (!State)
	{
		return;
	}
	States.Add(State);
	for (UStateTreeState* Child : State->Children)
	{
		CollectF5States(Child, States);
	}
}

UStateTreeState* FindF5State(UStateTreeEditorData& Data, const FName Name)
{
	TArray<UStateTreeState*> States;
	for (UStateTreeState* Root : Data.SubTrees)
	{
		CollectF5States(Root, States);
	}
	UStateTreeState** Found = States.FindByPredicate([&](const UStateTreeState* State)
	{
		return State->Name == Name;
	});
	return Found ? *Found : nullptr;
}

void BindF5(UStateTreeEditorData& Data, const FGuid Source, const TCHAR* SourcePath,
	const FGuid Target, const TCHAR* TargetPath)
{
	FPropertyBindingPath From, To;
	From.SetStructID(Source);
	From.FromString(SourcePath);
	To.SetStructID(Target);
	To.FromString(TargetPath);
	Data.AddPropertyBinding(From, To);
}

bool HasF5Binding(const UStateTreeEditorData& Data, const FGuid Source, const TCHAR* SourcePath,
	const FGuid Target, const TCHAR* TargetPath)
{
	for (const auto& Binding : Data.EditorBindings.GetBindings())
	{
		if (Binding.GetSourcePath().GetStructID() == Source && Binding.GetSourcePath().ToString() == SourcePath
			&& Binding.GetTargetPath().GetStructID() == Target && Binding.GetTargetPath().ToString() == TargetPath)
		{
			return true;
		}
	}
	return false;
}

bool HasF5Transition(const UStateTreeState& From, const UStateTreeState& To,
	const EStateTreeTransitionTrigger Trigger)
{
	return From.Transitions.ContainsByPredicate([&](const FStateTreeTransition& Transition)
	{
		return Transition.bTransitionEnabled && Transition.Trigger == Trigger && Transition.State.ID == To.ID;
	});
}

bool HasCompiledF5Transition(const UStateTree& Tree, const FName From, const FName To,
	const EStateTreeTransitionTrigger Trigger, const FName Output = NAME_None)
{
	for (const FCompactStateTreeState& State : Tree.GetStates())
	{
		if (State.Name != From)
		{
			continue;
		}
		for (int32 Index = 0; Index < State.TransitionsNum; ++Index)
		{
			const auto* Transition = Tree.GetTransitionFromIndex(FStateTreeIndex16(State.TransitionsBegin + Index));
			const auto* Target = Transition ? Tree.GetStateFromHandle(Transition->State) : nullptr;
			if (!Transition || !Transition->bTransitionEnabled || Transition->Trigger != Trigger
				|| !Target || Target->Name != To)
			{
				continue;
			}
			if (Output.IsNone())
			{
				return true;
			}
			if (Transition->ConditionsNum != 1)
			{
				continue;
			}
			const auto* Condition = Tree.GetNode(Transition->ConditionsBegin).GetPtr<const FStateTreeCompareBoolCondition>();
			const auto* Value = EnemyCombatTesting::GetDefaultData(Tree, Condition);
			if (!Condition || !Value || !Value->bRight || !Condition->BindingsBatch.IsValid())
			{
				continue;
			}
			const FPropertyBindingBindingCollection& Bindings = Tree.GetPropertyBindings();
			if (!Bindings.GetCopyBatches().IsValidIndex(Condition->BindingsBatch.Get()))
			{
				continue;
			}
			for (const FPropertyBindingCopyInfo& Copy : Bindings.GetBatchCopies(Bindings.GetCopyBatches()[Condition->BindingsBatch.Get()]))
			{
				if (Copy.SourceLeafProperty && Copy.SourceLeafProperty->GetFName() == Output
					&& Copy.TargetLeafProperty && Copy.TargetLeafProperty->GetFName() == TEXT("bLeft"))
				{
					return true;
				}
			}
		}
	}
	return false;
}

bool FinishF5Migration(FAutomationTestBase& Test, UStateTree& Tree, const bool bChanged)
{
	if (bChanged)
	{
		FStateTreeCompilerLog Log;
		FStateTreeCompiler Compiler(Log);
		if (!Compiler.Compile(Tree))
		{
			Log.DumpToLog(LogTemp);
			Test.AddError(TEXT("F5 StateTree migration did not compile; no package saved"));
			return false;
		}
		Tree.MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		const FString Filename = FPackageName::LongPackageNameToFilename(
			Tree.GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		if (!Test.TestTrue(TEXT("Save only canonical F5 StateTree"),
			UPackage::SavePackage(Tree.GetOutermost(), &Tree, *Filename, Args)))
		{
			return false;
		}
	}
	return Test.TestTrue(TEXT("Saved F5 tree is compiled and ready"), Tree.IsReadyToRun());
}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyF5BattleStateBindingsTest,
	"Outlier.Enemy.F5.StateBindings.Battle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyF5BattleStateBindingsTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Type : { TEXT("Gun"), TEXT("Melee") })
	{
		Names.Add(Type);
		Commands.Add(Type);
	}
}

bool FEnemyF5BattleStateBindingsTest::RunTest(const FString& Parameters)
{
	const bool bInstall = FParse::Param(FCommandLine::Get(), TEXT("OutlierInstallF5State"));
	const FString Name = TEXT("ST_VECDroneBattle_") + Parameters;
	UStateTree* Tree = LoadObject<UStateTree>(nullptr,
		*(TEXT("/Game/Blueprints/AI/VECDrone/StateTree/") + Name + TEXT(".") + Name));
	if (!TestNotNull(TEXT("Canonical battle tree loads"), Tree))
	{
		return false;
	}
	auto* Data = Cast<UStateTreeEditorData>(Tree->EditorData);
	if (!TestNotNull(TEXT("Battle authoring data exists"), Data)
		|| (bInstall && !TestFalse(TEXT("Preserve preexisting dirty battle package"), Tree->GetOutermost()->IsDirty())))
	{
		return false;
	}
	UStateTreeState* Search = FindF5State(*Data, TEXT("Search"));
	UStateTreeState* Recover = FindF5State(*Data, TEXT("NoiseRecover"));
	if (!TestNotNull(TEXT("General Search exists"), Search) || !TestNotNull(TEXT("Existing delayed decision recovery exists"), Recover))
	{
		return false;
	}
	const FStateTreeEditorNode* Session = Search->Tasks.FindByPredicate([](const FStateTreeEditorNode& Node)
	{
		return Node.Node.GetScriptStruct() == FEnemySearchSessionTask::StaticStruct();
	});
	if (!TestNotNull(TEXT("Search owns session output"), Session))
	{
		return false;
	}
	UStateTreeState* Finish = FindF5State(*Data, TEXT("FinishGeneralSearch"));
	bool bChanged = false;
	if (!Finish && bInstall)
	{
		Tree->Modify();
		Data->Modify();
		Search->Modify();
		Finish = &Search->AddChildState(TEXT("FinishGeneralSearch"));
		Finish->Description = TEXT("같은 일반 수색의 3초 경계 종료만 귀환으로 확정. 새 단서 전이가 먼저 평가된다.");
		auto& Task = Finish->AddTask<FEnemySearchProgressTask>();
		Task.GetInstanceData().Action = EEnemySearchProgress::FinishGeneralSearch;
		const FGuid EnemyContext = Data->FindContextData(AEnemyBase::StaticClass(), TEXT("Enemy")).ID;
		if (!TestTrue(TEXT("Battle Enemy context exists"), EnemyContext.IsValid()))
		{
			return false;
		}
		BindF5(*Data, EnemyContext, TEXT(""), Task.ID, TEXT("Enemy"));
		auto& Transition = Search->AddTransition(EStateTreeTransitionTrigger::OnTick,
			EStateTreeTransitionType::GotoState, Finish);
		Transition.Priority = EStateTreeTransitionPriority::Low;
		auto& Condition = Transition.AddCondition<FStateTreeCompareBoolCondition>();
		Condition.GetInstanceData().bRight = true;
		BindF5(*Data, Session->ID, TEXT("bShouldReturnHome"), Condition.ID, TEXT("bLeft"));
		// 확정 시 Pawn이 기존 AlertCleared 이벤트를 예약한다. 짧은 기존 복구 상태로
		// 빠져나와 다음 Tick의 공용 상태 선택을 기다리며 종료 Task를 반복하지 않는다.
		Finish->AddTransition(EStateTreeTransitionTrigger::OnStateSucceeded, EStateTreeTransitionType::GotoState, Recover);
		Finish->AddTransition(EStateTreeTransitionTrigger::OnStateFailed, EStateTreeTransitionType::GotoState, Recover);
		bChanged = true;
	}
	if (!TestNotNull(TEXT("F5 finish state installed (-OutlierInstallF5State)"), Finish))
	{
		return false;
	}
	TestTrue(TEXT("Finish remains inside current search until commit"), Finish->Parent == Search);
	TestTrue(TEXT("Successful commit releases search through existing recovery"),
		HasF5Transition(*Finish, *Recover, EStateTreeTransitionTrigger::OnStateSucceeded));
	TestTrue(TEXT("Rejected stale commit re-evaluates current intent"),
		HasF5Transition(*Finish, *Recover, EStateTreeTransitionTrigger::OnStateFailed));
	const auto* Task = Finish->Tasks.Num() == 1 ? Finish->Tasks[0].Instance.GetPtr<FEnemySearchProgressTaskInstanceData>() : nullptr;
	TestTrue(TEXT("Finish uses general-search completion action"), Task && Task->Action == EEnemySearchProgress::FinishGeneralSearch);
	int32 CompletionTransitions = 0;
	for (const FStateTreeTransition& Transition : Search->Transitions)
	{
		if (Transition.State.ID != Finish->ID)
		{
			continue;
		}
		++CompletionTransitions;
		TestEqual(TEXT("Guard completion waits for current Tick"), Transition.Trigger, EStateTreeTransitionTrigger::OnTick);
		TestEqual(TEXT("Fresh sight and investigation retain priority"), Transition.Priority, EStateTreeTransitionPriority::Low);
		if (TestEqual(TEXT("Completion has one owned bool condition"), Transition.Conditions.Num(), 1))
		{
			const auto& Condition = Transition.Conditions[0];
			TestTrue(TEXT("Completion reads current search output"),
				HasF5Binding(*Data, Session->ID, TEXT("bShouldReturnHome"), Condition.ID, TEXT("bLeft")));
		}
	}
	TestEqual(TEXT("Only one finite guard completion transition"), CompletionTransitions, 1);
	if (HasAnyErrors() || !FinishF5Migration(*this, *Tree, bChanged))
	{
		return false;
	}
	TestTrue(TEXT("Compiled Search owns finite return transition"), HasCompiledF5Transition(*Tree,
		TEXT("Search"), TEXT("FinishGeneralSearch"), EStateTreeTransitionTrigger::OnTick, TEXT("bShouldReturnHome")));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyF5ReturnStateBindingsTest,
	"Outlier.Enemy.F5.StateBindings.Return",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyF5ReturnStateBindingsTest::RunTest(const FString& Parameters)
{
	const bool bInstall = FParse::Param(FCommandLine::Get(), TEXT("OutlierInstallF5State"));
	UStateTree* Tree = LoadObject<UStateTree>(nullptr,
		TEXT("/Game/Blueprints/AI/VECDrone/StateTree/ST_VECDrone_Common.ST_VECDrone_Common"));
	if (!TestNotNull(TEXT("Canonical common tree loads"), Tree))
	{
		return false;
	}
	auto* Data = Cast<UStateTreeEditorData>(Tree->EditorData);
	if (!TestNotNull(TEXT("Common authoring data exists"), Data)
		|| (bInstall && !TestFalse(TEXT("Preserve preexisting dirty common package"), Tree->GetOutermost()->IsDirty())))
	{
		return false;
	}
	UStateTreeState* Returning = FindF5State(*Data, TEXT("Returning"));
	UStateTreeState* Select = FindF5State(*Data, TEXT("SelectHome"));
	UStateTreeState* Guard = FindF5State(*Data, TEXT("ReturnGuard"));
	if (!TestNotNull(TEXT("Returning exists"), Returning) || !TestNotNull(TEXT("SelectHome exists"), Select))
	{
		return false;
	}
	const FStateTreeEditorNode* SelectTask = Select->Tasks.FindByPredicate([](const FStateTreeEditorNode& Node)
	{
		return Node.Node.GetScriptStruct() == FEnemySelectReturnHomeTask::StaticStruct();
	});
	if (!TestNotNull(TEXT("Select owns terminal guard output"), SelectTask))
	{
		return false;
	}
	bool bChanged = false;
	if (!Guard && bInstall)
	{
		Tree->Modify();
		Data->Modify();
		Returning->Modify();
		Select->Modify();
		Guard = &Returning->AddChildState(TEXT("ReturnGuard"));
		Guard->Description = TEXT("대체 지점 도착 또는 귀환 예산 소진 후 경계. 새 단서와 의미 있는 복구만 선점한다.");
		auto& Task = Guard->AddTask<FEnemyReturnHomeGuardTask>();
		const FGuid EnemyContext = Data->FindContextData(AEnemyBase::StaticClass(), TEXT("Enemy")).ID;
		if (!TestTrue(TEXT("Common Enemy context exists"), EnemyContext.IsValid()))
		{
			return false;
		}
		BindF5(*Data, EnemyContext, TEXT(""), Task.ID, TEXT("Enemy"));
		auto& Failed = Select->AddTransition(EStateTreeTransitionTrigger::OnStateFailed,
			EStateTreeTransitionType::GotoState, Guard);
		auto& Condition = Failed.AddCondition<FStateTreeCompareBoolCondition>();
		Condition.GetInstanceData().bRight = true;
		BindF5(*Data, SelectTask->ID, TEXT("bGuarding"), Condition.ID, TEXT("bLeft"));
		Guard->AddTransition(EStateTreeTransitionTrigger::OnStateSucceeded, EStateTreeTransitionType::GotoState, Select);
		bChanged = true;
	}
	if (!TestNotNull(TEXT("F5 return guard installed (-OutlierInstallF5State)"), Guard))
	{
		return false;
	}
	TestTrue(TEXT("Return guard preserves common interrupts"), Guard->Parent == Returning);
	TestTrue(TEXT("Select exhausts into explicit guard"), HasF5Transition(*Select, *Guard, EStateTreeTransitionTrigger::OnStateFailed));
	TestTrue(TEXT("Meaningful recovery reuses original selector"), HasF5Transition(*Guard, *Select, EStateTreeTransitionTrigger::OnStateSucceeded));
	TestTrue(TEXT("Guard uses existing Pawn-owned return state"), Guard->Tasks.Num() == 1
		&& Guard->Tasks[0].Node.GetScriptStruct() == FEnemyReturnHomeGuardTask::StaticStruct());
	if (HasAnyErrors() || !FinishF5Migration(*this, *Tree, bChanged))
	{
		return false;
	}
	TestTrue(TEXT("Compiled exhaustion reads owned guard result"), HasCompiledF5Transition(*Tree,
		TEXT("SelectHome"), TEXT("ReturnGuard"), EStateTreeTransitionTrigger::OnStateFailed, TEXT("bGuarding")));
	TestTrue(TEXT("Compiled guard resumes only on recovery"), HasCompiledF5Transition(*Tree,
		TEXT("ReturnGuard"), TEXT("SelectHome"), EStateTreeTransitionTrigger::OnStateSucceeded));
	return !HasAnyErrors();
}

namespace
{
struct FF5ExecutionWorld
{
	UWorld* World = nullptr;
	UGameInstance* GameInstance = nullptr;
	const FGameplayTag RoomTag = FGameplayTag::RequestGameplayTag(TEXT("Room.Level01.1"));

	FF5ExecutionWorld()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("F5StateExecution")));
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->AddToRoot();
		GameInstance = NewObject<UGameInstance>(GEngine);
		World->SetGameInstance(GameInstance);
		GameInstance->Init();
		World->CreateAISystem();
		FURL URL;
		URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
		World->SetGameMode(URL);
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
		URoomCombatDefinition* Definition = NewObject<URoomCombatDefinition>(World);
		auto& RoomDefinition = Definition->RoomDefinitions.AddDefaulted_GetRef();
		RoomDefinition.RoomTag = RoomTag;
		RoomDefinition.CombatPhases.AddDefaulted();
		World->GetSubsystem<URoomCombatSubsystem>()->SetCombatDefinitionForTesting(Definition);
	}

	~FF5ExecutionWorld()
	{
		GEngine->ShutdownWorldNetDriver(World);
		World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(true);
		GameInstance->Shutdown();
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}

	void Tick() const
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 1.0f / 30.0f);
	}

	void AddBlockingBox(const FVector& Location, const FVector& Extent) const
	{
		AActor* Actor = World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
		Actor->SetRootComponent(Box);
		Box->SetBoxExtent(Extent);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->RegisterComponent();
		Box->SetWorldLocation(Location);
		World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
	}
};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyF5StateExecutionTest,
	"Outlier.Enemy.F5.StateExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyF5StateExecutionTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Type : { TEXT("Gun"), TEXT("Melee") })
	{
		for (const TCHAR* Scenario : { TEXT("Return"), TEXT("FreshHint") })
		{
			const FString Name = FString(Type) + TEXT(".") + Scenario;
			Names.Add(Name);
			Commands.Add(Name);
		}
	}
}

bool FEnemyF5StateExecutionTest::RunTest(const FString& Parameters)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	FF5ExecutionWorld Fixture;
	ARoomVolume* Room = Fixture.World->SpawnActor<ARoomVolume>();
	if (!TestNotNull(TEXT("Execution room exists"), Room))
	{
		return false;
	}
	CastChecked<UBoxComponent>(Room->GetRootComponent())->SetBoxExtent(FVector(1600, 1600, 700));
	if (!TestTrue(TEXT("Execution room registers"),
		Fixture.World->GetSubsystem<URoomCombatSubsystem>()->RegisterRoom(Room, Fixture.RoomTag)))
	{
		return false;
	}
	const bool bMelee = Parameters.StartsWith(TEXT("Melee"));
	const bool bFreshHint = Parameters.EndsWith(TEXT("FreshHint"));
	UClass* EnemyClass = LoadClass<AEnemyBase>(nullptr, bMelee
		? TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Explosion.BP_VECDrone_Explosion_C")
		: TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"));
	if (!TestNotNull(TEXT("Real drone BP loads"), EnemyClass))
	{
		return false;
	}
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AEnemyBase* Enemy = Fixture.World->SpawnActor<AEnemyBase>(EnemyClass, FTransform(FVector(0, 0, 200)), Spawn);
	AEnemyAIController* Controller = Enemy ? Cast<AEnemyAIController>(Enemy->GetController()) : nullptr;
	if (!TestNotNull(TEXT("Real drone exists"), Enemy) || !TestNotNull(TEXT("Real AI exists"), Controller))
	{
		return false;
	}
	Controller->SetEnemyPerceptionEnabled(false);
	Enemy->GetRoomTagComp()->AssignDefaultRoomTag(Fixture.RoomTag);
	// 멀리 있는 마지막 단서로 즉시 후보 소진을 유도한다. 수색/경계/귀환 Task나
	// 완료 출력을 직접 호출하지 않고 저장된 공용·Linked Battle Tree가 전부 진행한다.
	Enemy->EnterCombatFromRoom(FVector(5000, 0, 200), false);
	Controller->SetEnemyPerceptionEnabled(false);
	Enemy->SetPlayerCurrentlyVisible(false);
	Enemy->UpdateLastKnownPlayerLocation(FVector(5000, 0, 200));
	Enemy->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	UStateTreeComponent* Component = Enemy->GetStateTreeComponent();
	Component->StopLogic(TEXT("F5 saved tree execution setup"));
	*FindFProperty<FStructProperty>(AEnemyBase::StaticClass(), TEXT("PatrolPointA"))
		->ContainerPtrToValuePtr<FVector>(Enemy) = FVector(700, 0, 200);
	Component->StartLogic();
	double GuardStartedAt = -1.0;
	uint64 GuardSerial = 0;
	for (int32 Frame = 0; Frame < 300; ++Frame)
	{
		Fixture.Tick();
		if (Enemy->GetSearchEpisode().bLocalGuard)
		{
			GuardStartedAt = Fixture.World->GetTimeSeconds();
			GuardSerial = Enemy->GetSearchEpisode().Serial;
			break;
		}
	}
	if (!TestTrue(TEXT("Saved battle reaches local guard"), GuardStartedAt >= 0.0))
	{
		return false;
	}
	while (Fixture.World->GetTimeSeconds() - GuardStartedAt < 2.5)
	{
		Fixture.Tick();
	}
	TestTrue(TEXT("Three-second guard does not return early"), Enemy->GetCombatState() == EEnemyCombatState::Combat);
	if (bFreshHint)
	{
		const FVector Hint(500, 500, 200);
		Enemy->ApplySharedTargetContact(Hint, true, Enemy->GetSharedTargetRevision() + 1);
		for (int32 Frame = 0; Frame < 30 && Enemy->GetSearchEpisode().Serial == GuardSerial; ++Frame)
		{
			Fixture.Tick();
		}
		TestTrue(TEXT("Fresh hint preempts actual guard deadline"), Enemy->GetSearchEpisode().Serial > GuardSerial);
		TestTrue(TEXT("Fresh hint becomes actual search center"), Enemy->GetSearchEpisode().Center.Equals(Hint, 1));
		TestTrue(TEXT("Old guard cannot commit return after hint"), Enemy->GetCombatState() == EEnemyCombatState::Combat);
	}
	else
	{
		bool bReturning = false;
		for (int32 Frame = 0; Frame < 90; ++Frame)
		{
			Fixture.Tick();
			if (Enemy->GetCombatState() == EEnemyCombatState::Returning)
			{
				bReturning = true;
				break;
			}
		}
		TestTrue(TEXT("Saved battle completion enters common Returning"), bReturning);
		TestTrue(TEXT("Return starts only after three-second guard"),
			Fixture.World->GetTimeSeconds() - GuardStartedAt >= 2.95);
		bool bSawFlyHome = false;
		for (int32 Frame = 0; Frame < 150; ++Frame)
		{
			Fixture.Tick();
			bSawFlyHome |= Component->GetActiveStateNames().Contains(TEXT("FlyHome"));
			if (bSawFlyHome)
			{
				break;
			}
		}
		TestTrue(TEXT("Common tree actually consumes return destination"), bSawFlyHome);
		TestFalse(TEXT("Old general search is released after return handoff"), Enemy->GetSearchEpisode().bActive);
	}
	Component->StopLogic(TEXT("F5 saved tree execution finished"));
	return !HasAnyErrors();
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyF5ReturnGuardExecutionTest,
	"Outlier.Enemy.F5.ReturnGuardExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyF5ReturnGuardExecutionTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : { TEXT("MissingHome"), TEXT("OccupiedHome"), TEXT("SealedHome") })
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FEnemyF5ReturnGuardExecutionTest::RunTest(const FString& Parameters)
{
	IConsoleVariable* FlightDebug = IConsoleManager::Get().FindConsoleVariable(TEXT("outlier.Enemy.FlightDebug"));
	const int32 PreviousDebug = FlightDebug ? FlightDebug->GetInt() : 0;
	const EConsoleVariableFlags DebugPriority = FlightDebug
		? static_cast<EConsoleVariableFlags>(FlightDebug->GetFlags() & ECVF_SetByMask) : ECVF_SetByCode;
	if (FlightDebug)
	{
		FlightDebug->Set(2, DebugPriority);
	}
	ON_SCOPE_EXIT
	{
		if (FlightDebug)
		{
			FlightDebug->Set(PreviousDebug, DebugPriority);
		}
	};
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	FF5ExecutionWorld Fixture;
	const bool bMissingHome = Parameters == TEXT("MissingHome");
	const FVector HomeAnchor(700, 0, 200);
	if (!bMissingHome)
	{
		ARoomVolume* Room = Fixture.World->SpawnActor<ARoomVolume>();
		if (!TestNotNull(TEXT("Return room exists"), Room))
		{
			return false;
		}
		CastChecked<UBoxComponent>(Room->GetRootComponent())->SetBoxExtent(FVector(1600, 1600, 700));
		if (!TestTrue(TEXT("Return room registers"),
			Fixture.World->GetSubsystem<URoomCombatSubsystem>()->RegisterRoom(Room, Fixture.RoomTag)))
		{
			return false;
		}
		if (Parameters == TEXT("OccupiedHome"))
		{
			Fixture.AddBlockingBox(HomeAnchor, FVector(70, 70, 70));
		}
		else
		{
			// 정확한 목적지는 비어 있지만 여섯 면이 닫혀 도달할 수 없다.
			// 단순 endpoint 보정만으로는 빠져나오지 못하고 다음 귀환 요청에서 대체 경로가 필요하다.
			for (const FVector& Axis : { FVector::ForwardVector, FVector::RightVector, FVector::UpVector })
			{
				const FVector Extent = FVector(120) - Axis.GetAbs() * 110.0f;
				Fixture.AddBlockingBox(HomeAnchor + Axis * 110.0f, Extent);
				Fixture.AddBlockingBox(HomeAnchor - Axis * 110.0f, Extent);
			}
		}
	}
	UClass* EnemyClass = LoadClass<AEnemyBase>(nullptr,
		TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"));
	if (!TestNotNull(TEXT("Return drone BP loads"), EnemyClass))
	{
		return false;
	}
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AEnemyBase* Enemy = Fixture.World->SpawnActor<AEnemyBase>(EnemyClass, FTransform(FVector(0, 0, 200)), Spawn);
	AEnemyAIController* Controller = Enemy ? Cast<AEnemyAIController>(Enemy->GetController()) : nullptr;
	if (!TestNotNull(TEXT("Return drone exists"), Enemy) || !TestNotNull(TEXT("Return AI exists"), Controller))
	{
		return false;
	}
	Controller->SetEnemyPerceptionEnabled(false);
	Enemy->GetRoomTagComp()->AssignDefaultRoomTag(Fixture.RoomTag);
	Enemy->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	UEnemyStateTreeComponent* Component = CastChecked<UEnemyStateTreeComponent>(Enemy->GetStateTreeComponent());
	Component->StopLogic(TEXT("F5 return guard execution setup"));
	*FindFProperty<FStructProperty>(AEnemyBase::StaticClass(), TEXT("PatrolPointA"))
		->ContainerPtrToValuePtr<FVector>(Enemy) = HomeAnchor;
	*FindFProperty<FEnumProperty>(AEnemyBase::StaticClass(), TEXT("CombatState"))
		->ContainerPtrToValuePtr<EEnemyCombatState>(Enemy) = EEnemyCombatState::Returning;
	if (Parameters == TEXT("SealedHome"))
	{
		UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent();
		FFlightPathRequest Request;
		Request.Start = Capsule->GetComponentLocation();
		Request.Goal = HomeAnchor + Capsule->GetComponentLocation() - Enemy->GetActorLocation();
		Request.SearchBounds = FBox(Request.Start.ComponentMin(Request.Goal), Request.Start.ComponentMax(Request.Goal)).ExpandBy(1.0f);
		TestEqual(TEXT("Sealed fixture proves exact endpoint itself is clear"),
			FlightPathfinding::CheckDestination(*Fixture.World, *Capsule, Request), EFlightPathStatus::Success);
	}
	const FVector Start = Enemy->GetActorLocation();
	Component->StartLogic();
	bool bSawFallbackFly = false;
	bool bReachedGuard = false;
	FVector FallbackDestination = FVector::ZeroVector;
	for (int32 Frame = 0; Frame < 900; ++Frame)
	{
		Fixture.Tick();
		const EnemyCombatTesting::FSnapshot Snapshot = EnemyCombatTesting::ReadComponent(*Component);
		if (const auto* ActiveFly = Snapshot.FindTask<FEnemyFlyToLocationTask>())
		{
			const auto* Fly = ActiveFly->Data.GetPtr<const FEnemyFlyToLocationTaskInstanceData>();
			if (Fly && !Fly->Destination.Equals(HomeAnchor, 1.0f))
			{
				bSawFallbackFly = true;
				FallbackDestination = Fly->Destination;
			}
		}
		if (Snapshot.HasState(TEXT("ReturnGuard")))
		{
			bReachedGuard = true;
			break;
		}
	}
	AddInfo(FString::Printf(TEXT("F5 %s: Guard=%d Requests=%d Candidates=%d Location=%s Fallback=%s"),
		*Parameters, bReachedGuard, Enemy->GetReturnHomeRequestCount(), Enemy->GetReturnHomeCandidateChecks(),
		*Enemy->GetActorLocation().ToCompactString(), *FallbackDestination.ToCompactString()));
	if (!TestTrue(TEXT("Actual common tree enters explicit terminal guard"), bReachedGuard))
	{
		Component->StopLogic(TEXT("F5 failed guard execution"));
		return false;
	}
	TestTrue(TEXT("Guard does not claim ordinary return completion"), Enemy->GetCombatState() == EEnemyCombatState::Returning);
	TestTrue(TEXT("Guard stays inside original request cap"), Enemy->GetReturnHomeRequestCount() <= 3);
	TestTrue(TEXT("Guard stays inside total fallback candidate cap"), Enemy->GetReturnHomeCandidateChecks() <= 8);
	if (bMissingHome)
	{
		TestFalse(TEXT("Missing home cannot start fallback movement"), bSawFallbackFly);
		TestTrue(TEXT("Missing home keeps actual current position"), Enemy->GetActorLocation().Equals(Start, 1));
	}
	else
	{
		TestTrue(TEXT("Actual Fly consumes selected fallback rather than exact home"), bSawFallbackFly);
		TestTrue(TEXT("Fallback guard follows actual destination arrival"), Enemy->GetActorLocation().Equals(FallbackDestination, 5));
		TestFalse(TEXT("Fallback does not report arrival inside obstacle"), Enemy->GetActorLocation().Equals(HomeAnchor, 20));
	}
	const int32 RequestsAtGuard = Enemy->GetReturnHomeRequestCount();
	const int32 CandidatesAtGuard = Enemy->GetReturnHomeCandidateChecks();
	const FVector GuardLocation = Enemy->GetActorLocation();
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		Fixture.Tick();
	}
	TestTrue(TEXT("Unchanged conditions retain actual ReturnGuard"), Component->GetActiveStateNames().Contains(TEXT("ReturnGuard")));
	TestEqual(TEXT("Guard does not restart selection by time passage"), Enemy->GetReturnHomeRequestCount(), RequestsAtGuard);
	TestEqual(TEXT("Guard does not refill fallback candidates"), Enemy->GetReturnHomeCandidateChecks(), CandidatesAtGuard);
	TestTrue(TEXT("Guard does not drift or patrol"), Enemy->GetActorLocation().Equals(GuardLocation, 1));
	Enemy->EnterAlertFromPerception(FVector(300, 300, 200));
	Fixture.Tick();
	TestFalse(TEXT("New valid alert interrupts the actual terminal guard"), Component->GetActiveStateNames().Contains(TEXT("ReturnGuard")));
	Component->StopLogic(TEXT("F5 return guard execution finished"));
	return !HasAnyErrors();
}

#endif
