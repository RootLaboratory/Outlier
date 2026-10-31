#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Conditions/StateTreeCommonConditions.h"
#include "AIController.h"
#include "Components/BoxComponent.h"
#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyStateTreeComponent.h"
#include "Enemy/EnemyStateTreeMovementTasks.h"
#include "Enemy/EnemyStateTreeReturnTasks.h"
#include "Enemy/EnemyStateTreeSyncTask.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "NativeGameplayTags.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomTagComponent.h"
#include "Room/RoomVolume.h"
#include "StateTree.h"
#include "StateTreeCompiler.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditorData.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeInstanceData.h"
#include "StateTreeState.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace
{
constexpr TCHAR CommonTreePath[] = TEXT("/Game/Blueprints/AI/VECDrone/StateTree/ST_VECDrone_Common.ST_VECDrone_Common");
UE_DEFINE_GAMEPLAY_TAG_STATIC(ReturnTreeHome, "Room.ReturnTreeTest.1.1");
UE_DEFINE_GAMEPLAY_TAG_STATIC(ReturnTreeSibling, "Room.ReturnTreeTest.1.2");

void CollectStates(UStateTreeState* State, TArray<UStateTreeState*>& States)
{
	if (!State) return;
	States.Add(State);
	for (UStateTreeState* Child : State->Children) CollectStates(Child, States);
}

void Bind(UStateTreeEditorData& Data, const FGuid SourceID, const TCHAR* Source,
	const FGuid TargetID, const TCHAR* Target)
{
	FPropertyBindingPath SourcePath;
	SourcePath.SetStructID(SourceID);
	SourcePath.FromString(Source);
	FPropertyBindingPath TargetPath;
	TargetPath.SetStructID(TargetID);
	TargetPath.FromString(Target);
	Data.AddPropertyBinding(SourcePath, TargetPath);
}

void AddCombatStateGuard(UStateTreeEditorData& Data, UStateTreeState& State,
	const FGuid SyncID, const EEnemyCombatState Value)
{
	auto& Condition = State.AddEnterCondition<FStateTreeCompareEnumCondition>();
	auto& Instance = Condition.GetInstanceData();
	Instance.Left.Initialize(StaticEnum<EEnemyCombatState>());
	Instance.Right.Initialize(StaticEnum<EEnemyCombatState>());
	Instance.Right.Value = static_cast<uint32>(Value);
	Bind(Data, SyncID, TEXT("CombatState"), Condition.ID, TEXT("Left"));
}

bool HasStateGuard(const UStateTreeState& State, const EEnemyCombatState Value)
{
	for (const FStateTreeEditorNode& Condition : State.EnterConditions)
	{
		const auto* Enum = Condition.Instance.GetPtr<FStateTreeCompareEnumConditionInstanceData>();
		const auto* Node = Condition.Node.GetPtr<FStateTreeCompareEnumCondition>();
		if (Enum && Node && !Node->bInvert && Enum->Right.Enum == StaticEnum<EEnemyCombatState>()
			&& Enum->Right.Value == static_cast<uint32>(Value)) return true;
	}
	return false;
}

bool HasTransition(const UStateTreeState& From, const UStateTreeState& To,
	const EStateTreeTransitionTrigger Trigger)
{
	return From.Transitions.ContainsByPredicate([&](const FStateTreeTransition& Transition)
	{
		return Transition.bTransitionEnabled && Transition.Trigger == Trigger && Transition.State.ID == To.ID;
	});
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyRoomReturnCommonTreeTest,
	"Outlier.Enemy.RoomReturn.CommonTree", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyRoomReturnCommonTreeTest::RunTest(const FString& Parameters)
{
	UStateTree* Tree = LoadObject<UStateTree>(nullptr, CommonTreePath);
	if (!TestNotNull(TEXT("Configured common StateTree exists"), Tree)) return false;
	for (const TCHAR* ClassPath : {
		TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"),
		TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun_Patrol.BP_VECDrone_Gun_Patrol_C"),
		TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Explosion.BP_VECDrone_Explosion_C"),
		TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Explosion_Patrol.BP_VECDrone_Explosion_Patrol_C") })
	{
		UClass* EnemyClass = LoadClass<AEnemyBase>(nullptr, ClassPath);
		if (!TestNotNull(TEXT("Configured drone class exists"), EnemyClass)) return false;
		const AEnemyBase* DefaultEnemy = EnemyClass->GetDefaultObject<AEnemyBase>();
		const auto* Component = Cast<UEnemyStateTreeComponent>(DefaultEnemy->GetStateTreeComponent());
		if (!TestNotNull(TEXT("Configured drone StateTree component"), Component)) return false;
		TestTrue(FString::Printf(TEXT("%s resolves to migrated common tree"), ClassPath),
			Component->GetConfiguredStateTreeReference().GetStateTree() == Tree);
	}
	UStateTreeEditorData* Data = Cast<UStateTreeEditorData>(Tree->EditorData);
	if (!TestNotNull(TEXT("Common StateTree editor graph exists"), Data)) return false;
	TArray<UStateTreeState*> States;
	for (UStateTreeState* Root : Data->SubTrees) CollectStates(Root, States);
	for (const UStateTreeState* State : States)
	{
		AddInfo(FString::Printf(TEXT("Common tree: %s"), *State->GetPath()));
		for (const FStateTreeTransition& Transition : State->Transitions)
		{
			AddInfo(FString::Printf(TEXT("  %s -> %s"), *Transition.RequiredEvent.Tag.ToString(), *Transition.State.Name.ToString()));
		}
	}
	auto FindState = [&](const FName Name) -> UStateTreeState*
	{
		UStateTreeState** Found = States.FindByPredicate([&](const UStateTreeState* State) { return State->Name == Name; });
		return Found ? *Found : nullptr;
	};
	UStateTreeState* NonBattle = FindState(TEXT("NonBattle"));
	if (!TestNotNull(TEXT("Existing NonBattle branch"), NonBattle)
		|| !TestNotNull(TEXT("NonBattle selection parent"), NonBattle->Parent.Get())) return false;
	UStateTreeState* Selection = NonBattle->Parent;
	FStateTreeEditorNode* Sync = Data->GlobalTasks.FindByPredicate([](const FStateTreeEditorNode& Node)
	{
		return Node.Node.GetScriptStruct() == FEnemyStateTreeSyncTask::StaticStruct();
	});
	if (!TestNotNull(TEXT("Existing global Enemy State Sync"), Sync)) return false;
	UStateTreeState* Returning = FindState(TEXT("Returning"));
	const bool bInstall = FParse::Param(FCommandLine::Get(), TEXT("OutlierInstallRoomReturnState"));
	if (!Returning && bInstall)
	{
		// 일반 검증은 자산을 저장하지 않는다. 승인된 일회성 이전 실행에서만 공용 트리를 갱신한다.
		Tree->Modify();
		Data->Modify();
		Selection->Modify();
		Returning = &Selection->AddChildState(TEXT("Returning"));
		Selection->Children.Remove(Returning);
		Selection->Children.Insert(Returning, Selection->Children.IndexOfByKey(NonBattle));
		AddCombatStateGuard(*Data, *Returning, Sync->ID, EEnemyCombatState::Returning);
		// 귀환도 비전투 분기의 감지/피격/빙의 인터럽트를 그대로 받는다.
		// 새 노드 ID와 바인딩을 부여해 원래 분기의 데이터와 공유하지 않는다.
		for (const FStateTreeTransition& Existing : NonBattle->Transitions)
		{
			if (!EnumHasAnyFlags(Existing.Trigger, EStateTreeTransitionTrigger::OnEvent)) continue;
			FStateTreeTransition& Copied = Returning->Transitions.Add_GetRef(Existing);
			Copied.ID = FGuid::NewGuid();
			for (FStateTreeEditorNode& Condition : Copied.Conditions)
			{
				const FGuid PreviousID = Condition.ID;
				Condition.ID = FGuid::NewGuid();
				Data->EditorBindings.CopyBindings(PreviousID, Condition.ID);
			}
		}
		if (!HasStateGuard(*NonBattle, EEnemyCombatState::NonCombat))
		{
			AddCombatStateGuard(*Data, *NonBattle, Sync->ID, EEnemyCombatState::NonCombat);
		}
		UStateTreeState& Select = Returning->AddChildState(TEXT("SelectHome"));
		UStateTreeState& Move = Returning->AddChildState(TEXT("FlyHome"));
		UStateTreeState& Complete = Returning->AddChildState(TEXT("CompleteHome"));
		auto& SelectTask = Select.AddTask<FEnemySelectReturnHomeTask>();
		auto& MoveTask = Move.AddTask<FEnemyFlyToLocationTask>();
		auto& CompleteTask = Complete.AddTask<FEnemyCompleteReturnHomeTask>();
		MoveTask.GetInstanceData().bUsePathfinding = true;
		MoveTask.GetInstanceData().bReturnHome = true;
		MoveTask.GetInstanceData().GoalSearchRadius = 0.0f;
		MoveTask.GetInstanceData().AcceptanceRadius = 1.0f;
		MoveTask.GetInstanceData().MoveTimeout = 20.0f;
		MoveTask.GetInstanceData().NoProgressTimeout = 3.0f;
		const FGuid EnemyContextID = Data->FindContextData(AEnemyBase::StaticClass(), TEXT("Enemy")).ID;
		if (!TestTrue(TEXT("Enemy context descriptor"), EnemyContextID.IsValid())) return false;
		Bind(*Data, EnemyContextID, TEXT(""), SelectTask.ID, TEXT("Enemy"));
		Bind(*Data, EnemyContextID, TEXT(""), MoveTask.ID, TEXT("Enemy"));
		Bind(*Data, EnemyContextID, TEXT(""), CompleteTask.ID, TEXT("Enemy"));
		// 원래 순찰 시작점을 입력으로 전달하고, Fly 진입 시 현재 등록된 소속 방 내부로 다시 보정한다.
		Bind(*Data, Sync->ID, TEXT("PatrolPointA"), MoveTask.ID, TEXT("Destination"));
		// 형제 상태의 종료된 Task 출력에는 바인딩하지 않는다. Fly 진입 시 현재 소속 방 목적지를 다시 구한다.
		Select.AddTransition(EStateTreeTransitionTrigger::OnStateSucceeded, EStateTreeTransitionType::GotoState, &Move);
		Move.AddTransition(EStateTreeTransitionTrigger::OnStateSucceeded, EStateTreeTransitionType::GotoState, &Complete);
		Move.AddTransition(EStateTreeTransitionTrigger::OnStateFailed, EStateTreeTransitionType::GotoState, &Select);
		Complete.AddTransition(EStateTreeTransitionTrigger::OnStateSucceeded, EStateTreeTransitionType::GotoState, Selection);
		Complete.AddTransition(EStateTreeTransitionTrigger::OnStateFailed, EStateTreeTransitionType::GotoState, &Select);
		// Alert 해제/기절 복구 등이 NonBattle을 직접 선택하면 귀환 상태를 건너뛴다.
		// 기존 이벤트와 우선순위는 보존하고 부모에서 현재 상태를 다시 선택하도록 연결한다.
		for (UStateTreeState* State : States)
		{
			for (FStateTreeTransition& Transition : State->Transitions)
			{
				if (Transition.State.ID == NonBattle->ID) Transition.State = Selection->GetLinkToState();
			}
		}
		FStateTreeCompilerLog Log;
		FStateTreeCompiler Compiler(Log);
		if (!Compiler.Compile(*Tree))
		{
			Log.DumpToLog(LogTemp);
			AddError(TEXT("Common StateTree migration did not compile; asset was not saved"));
			return false;
		}
		Tree->MarkPackageDirty();
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		const FString Filename = FPackageName::LongPackageNameToFilename(Tree->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		if (!TestTrue(TEXT("Save only configured common StateTree"), UPackage::SavePackage(Tree->GetOutermost(), Tree, *Filename, SaveArgs))) return false;
		AddInfo(TEXT("Installed and saved common Returning branch"));
	}
	if (!TestNotNull(TEXT("Common tree contains Returning branch"), Returning)) return false;
	TestTrue(TEXT("Returning is gated by explicit combat state"), HasStateGuard(*Returning, EEnemyCombatState::Returning));
	TestTrue(TEXT("NonBattle remains home-only NonCombat branch"), HasStateGuard(*NonBattle, EEnemyCombatState::NonCombat));
	for (const FStateTreeTransition& Existing : NonBattle->Transitions)
	{
		if (!EnumHasAnyFlags(Existing.Trigger, EStateTreeTransitionTrigger::OnEvent)) continue;
		TestTrue(FString::Printf(TEXT("Return preserves %s interruption"), *Existing.RequiredEvent.Tag.ToString()),
			Returning->Transitions.ContainsByPredicate([&](const FStateTreeTransition& Transition)
			{
				return Transition.RequiredEvent.Tag == Existing.RequiredEvent.Tag
					&& Transition.State.ID == Existing.State.ID && Transition.Priority == Existing.Priority;
			}));
	}
	if (!TestEqual(TEXT("Returning has select/move/complete sequence"), Returning->Children.Num(), 3)) return false;
	const UStateTreeState& Select = *Returning->Children[0];
	const UStateTreeState& Move = *Returning->Children[1];
	const UStateTreeState& Complete = *Returning->Children[2];
	TestTrue(TEXT("Select advances to actual movement"), HasTransition(Select, Move, EStateTreeTransitionTrigger::OnStateSucceeded));
	TestTrue(TEXT("Movement success checks actual home containment"), HasTransition(Move, Complete, EStateTreeTransitionTrigger::OnStateSucceeded));
	TestTrue(TEXT("Blocked movement retries after select delay"), HasTransition(Move, Select, EStateTreeTransitionTrigger::OnStateFailed));
	TestTrue(TEXT("Incomplete arrival retries"), HasTransition(Complete, Select, EStateTreeTransitionTrigger::OnStateFailed));
	TestTrue(TEXT("Complete return selects normal state"), HasTransition(Complete, *Selection, EStateTreeTransitionTrigger::OnStateSucceeded));
	TestEqual(TEXT("Existing Fly task is reused"), Move.Tasks.Num(), 1);
	if (Move.Tasks.Num() == 1)
	{
		const auto* Fly = Move.Tasks[0].Instance.GetPtr<FEnemyFlyToLocationTaskInstanceData>();
		TestTrue(TEXT("Return uses pathfinding with exact home goal"), Fly && Fly->bUsePathfinding && Fly->bReturnHome && Fly->GoalSearchRadius == 0.0f);
	}
	for (UStateTreeState* State : States)
	{
		for (const FStateTreeTransition& Transition : State->Transitions)
		{
			TestTrue(FString::Printf(TEXT("%s does not bypass return via direct NonBattle transition"), *State->Name.ToString()), Transition.State.ID != NonBattle->ID);
		}
	}
	FStateTreeCompilerLog Log;
	FStateTreeCompiler Compiler(Log);
	const bool bCompiled = Compiler.Compile(*Tree);
	if (!bCompiled) Log.DumpToLog(LogTemp);
	TestTrue(TEXT("Actual configured common tree compiles"), bCompiled && Tree->IsReadyToRun());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyRoomReturnExecutionTest,
	"Outlier.Enemy.RoomReturn.CommonTreeExecution", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyRoomReturnExecutionTest::RunTest(const FString& Parameters)
{
	UStateTree* Tree = LoadObject<UStateTree>(nullptr, CommonTreePath);
	if (!TestNotNull(TEXT("Configured common tree"), Tree)) return false;
	struct FWorldFixture
	{
		UWorld* World = nullptr;
		UGameInstance* GameInstance = nullptr;
		FWorldFixture()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false,
				MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("ReturnTreeExecution")));
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			GameInstance = NewObject<UGameInstance>(GEngine);
			World->SetGameInstance(GameInstance);
			GameInstance->Init();
			World->SetGameMode(FURL());
			World->InitializeActorsForPlay(FURL());
		}
		~FWorldFixture()
		{
			GEngine->ShutdownWorldNetDriver(World);
			World->DestroyWorld(false);
			GameInstance->Shutdown();
			World->SetPhysicsScene(nullptr);
			GEngine->DestroyWorldContext(World);
			World->RemoveFromRoot();
		}
	} Fixture;
	URoomCombatSubsystem* Rooms = Fixture.World->GetSubsystem<URoomCombatSubsystem>();
	URoomCombatDefinition* Definition = NewObject<URoomCombatDefinition>(Fixture.World);
	Rooms->SetCombatDefinitionForTesting(Definition);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FGameplayTag Tag = Index == 0 ? ReturnTreeHome.GetTag() : ReturnTreeSibling.GetTag();
		auto& Entry = Definition->RoomDefinitions.AddDefaulted_GetRef();
		Entry.RoomTag = Tag;
		Entry.CombatPhases.AddDefaulted_GetRef().Waves.AddDefaulted();
		ARoomVolume* Room = Fixture.World->SpawnActor<ARoomVolume>();
		*FindFProperty<FStructProperty>(ARoomVolume::StaticClass(), TEXT("RoomTag"))
			->ContainerPtrToValuePtr<FGameplayTag>(Room) = Tag;
		Room->SetActorLocation(FVector(Index == 0 ? -1000.0f : 1000.0f, 0, 0));
		CastChecked<UBoxComponent>(Room->GetRootComponent())->SetBoxExtent(FVector(1000, 1000, 1000));
		Rooms->RegisterRoom(Room, Tag);
	}
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AEnemyBase* Enemy = Fixture.World->SpawnActor<AEnemyBase>(AEnemyBase::StaticClass(), FTransform(FVector(1000, 0, 0)), Spawn);
	AAIController* Controller = Fixture.World->SpawnActor<AAIController>();
	if (!TestNotNull(TEXT("Return actor"), Enemy) || !TestNotNull(TEXT("Return controller"), Controller)) return false;
	Controller->Possess(Enemy);
	Enemy->GetRoomTagComp()->AssignDefaultRoomTag(ReturnTreeHome);
	// 전이 자체를 분리 검증한다. 실제 경로/충돌 이동은 RoomMovement 및 Fly 통합 테스트가 검증한다.
	*FindFProperty<FEnumProperty>(AEnemyBase::StaticClass(), TEXT("CombatState"))
		->ContainerPtrToValuePtr<EEnemyCombatState>(Enemy) = EEnemyCombatState::Returning;
	FStateTreeInstanceData Instance;
	FStateTreeExecutionContext Context(*Enemy, *Tree, Instance);
	for (const FStateTreeExternalDataDesc& Desc : Tree->GetContextDataDescs())
	{
		if (Desc.Struct && Controller->IsA(Cast<UClass>(Desc.Struct)))
		{
			Context.SetContextDataByName(Desc.Name, FStateTreeDataView(Controller));
		}
		else if (Desc.Struct && Enemy->IsA(Cast<UClass>(Desc.Struct)))
		{
			Context.SetContextDataByName(Desc.Name, FStateTreeDataView(Enemy));
		}
	}
	TestTrue(TEXT("Actual common tree starts Returning"), Context.Start() == EStateTreeRunStatus::Running);
	TestTrue(TEXT("Actual common tree selects home retry gate"), Context.GetActiveStateNames().Contains(TEXT("SelectHome")));
	Context.Tick(1.1f);
	TestTrue(TEXT("Retry gate enters existing Fly task"), Context.GetActiveStateNames().Contains(TEXT("FlyHome")));
	TestTrue(TEXT("Movement cannot finish outside home"), Enemy->GetCombatState() == EEnemyCombatState::Returning);
	Enemy->EnterAlertFromPerception(FVector(800, 0, 0));
	Context.SendEvent(FGameplayTag::RequestGameplayTag(TEXT("Enemy.Event.Combat.Alerted")));
	Context.Tick(0.1f);
	TestTrue(TEXT("Rediscovery interrupts actual common Returning branch"), Context.GetActiveStateNames().Contains(TEXT("Alert")));
	TestFalse(TEXT("Rediscovery exits FlyHome"), Context.GetActiveStateNames().Contains(TEXT("FlyHome")));
	Context.Stop();
	return true;
}

#endif
