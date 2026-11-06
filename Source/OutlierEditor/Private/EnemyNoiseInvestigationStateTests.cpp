#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AIController.h"
#include "Conditions/StateTreeCommonConditions.h"
#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyStateTreeMovementTasks.h"
#include "Enemy/EnemyStateTreeSyncTask.h"
#include "Enemy/EnemyStateTreeTacticalTasks.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "StateTree.h"
#include "StateTreeCompiler.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "StructUtils/PropertyBag.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace
{
constexpr TCHAR NoiseTreeFolder[] = TEXT("/Game/Blueprints/AI/VECDrone/StateTree/");

void CollectNoiseStates(UStateTreeState* State, TArray<UStateTreeState*>& States)
{
	if (!State) return;
	States.Add(State);
	for (UStateTreeState* Child : State->Children) CollectNoiseStates(Child, States);
}

UStateTreeState* FindNoiseState(UStateTreeEditorData& Data, const FName Name)
{
	TArray<UStateTreeState*> States;
	for (UStateTreeState* Root : Data.SubTrees) CollectNoiseStates(Root, States);
	UStateTreeState** Found = States.FindByPredicate([&](const UStateTreeState* State) { return State->Name == Name; });
	return Found ? *Found : nullptr;
}

FStateTreeEditorNode* FindNoiseSync(UStateTreeEditorData& Data)
{
	return Data.GlobalTasks.FindByPredicate([](const FStateTreeEditorNode& Node)
	{
		return Node.Node.GetScriptStruct() == FEnemyStateTreeSyncTask::StaticStruct();
	});
}

template<typename T>
const typename T::FInstanceDataType* FindAuthoredNoiseData(const UStateTreeState* State)
{
	if (!State) return nullptr;
	for (const FStateTreeEditorNode& Node : State->Tasks)
		if (Node.Node.GetScriptStruct() == T::StaticStruct()) return Node.Instance.GetPtr<typename T::FInstanceDataType>();
	return nullptr;
}

void NoiseBind(UStateTreeEditorData& Data, const FGuid SourceID, const TCHAR* Source, const FGuid TargetID, const TCHAR* Target,
	const bool bOutput = false)
{
	FPropertyBindingPath From, To;
	From.SetStructID(SourceID);
	From.FromString(Source);
	To.SetStructID(TargetID);
	To.FromString(Target);
	if (bOutput) Data.EditorBindings.AddOutputBinding(From, To);
	else Data.AddPropertyBinding(From, To);
}

// 기존 OR 식 전체를 괄호로 묶고 새 조건을 AND한다. 마지막 항에만 조건을 붙이지 않는다.
void GroupNoiseConditions(TArray<FStateTreeEditorNode>& Conditions)
{
	for (FStateTreeEditorNode& Condition : Conditions) ++Condition.ExpressionIndent;
}

void ConfigureNoiseGoal(UStateTreeEditorData& Data, FStateTreeEditorNode& Condition, const FGuid Sync,
	const EEnemyPerceptionGoalType Goal)
{
	auto& Instance = Condition.Instance.GetMutable<FStateTreeCompareEnumConditionInstanceData>();
	Instance.Left.Initialize(StaticEnum<EEnemyPerceptionGoalType>());
	Instance.Right.Initialize(StaticEnum<EEnemyPerceptionGoalType>());
	Instance.Right.Value = static_cast<uint32>(Goal);
	NoiseBind(Data, Sync, TEXT("PerceptionGoalType"), Condition.ID, TEXT("Left"));
}

void AddNoiseGoal(UStateTreeEditorData& Data, FStateTreeTransition& Transition, const FGuid Sync,
	const EEnemyPerceptionGoalType Goal)
{
	GroupNoiseConditions(Transition.Conditions);
	auto& Condition = Transition.AddCondition<FStateTreeCompareEnumCondition>();
	ConfigureNoiseGoal(Data, Condition, Sync, Goal);
}

void AddNoiseBool(UStateTreeEditorData& Data, FStateTreeTransition& Transition, const FGuid Source,
	const TCHAR* Output, const bool Value = true)
{
	GroupNoiseConditions(Transition.Conditions);
	auto& Condition = Transition.AddCondition<FStateTreeCompareBoolCondition>();
	Condition.GetInstanceData().bRight = Value;
	NoiseBind(Data, Source, Output, Condition.ID, TEXT("bLeft"));
}

void AddNoiseEnterBool(UStateTreeEditorData& Data, UStateTreeState& State, const FGuid Source,
	const TCHAR* Output, const bool Value = true)
{
	GroupNoiseConditions(State.EnterConditions);
	auto& Condition = State.AddEnterCondition<FStateTreeCompareBoolCondition>();
	Condition.GetInstanceData().bRight = Value;
	NoiseBind(Data, Source, Output, Condition.ID, TEXT("bLeft"));
}

FStateTreeTransition& NoiseGoto(UStateTreeState& From, UStateTreeState& To,
	const EStateTreeTransitionTrigger Trigger = EStateTreeTransitionTrigger::OnTick)
{
	return From.AddTransition(Trigger, EStateTreeTransitionType::GotoState, &To);
}

void AddNoiseGoalTransition(UStateTreeEditorData& Data, UStateTreeState& From, UStateTreeState& To,
	const FGuid Sync, const EEnemyPerceptionGoalType Goal)
{
	auto& Transition = NoiseGoto(From, To);
	Transition.Priority = EStateTreeTransitionPriority::High;
	AddNoiseGoal(Data, Transition, Sync, Goal);
}

struct FNoiseSearchDefaults
{
	FEnemyRunEnvQueryTaskInstanceData Anchor;
	FEnemyRunEnvQueryTaskInstanceData Points;
	FEnemyFlyToLocationTaskInstanceData Fly;
	FInstancedStruct Look;
	FStateTreeEditorNode Wait;
};

bool ReadNoiseDefaults(FAutomationTestBase& Test, UStateTreeEditorData& Gun, FNoiseSearchDefaults& Defaults)
{
	const auto* Anchor = FindAuthoredNoiseData<FEnemyRunEnvQueryTask>(FindNoiseState(Gun, TEXT("SelectAnchor")));
	const auto* Points = FindAuthoredNoiseData<FEnemyRunEnvQueryTask>(FindNoiseState(Gun, TEXT("SelectPoints")));
	const auto* Fly = FindAuthoredNoiseData<FEnemyFlyToLocationTask>(FindNoiseState(Gun, TEXT("MovePoint")));
	const auto* Look = FindAuthoredNoiseData<FEnemyLookAroundTask>(FindNoiseState(Gun, TEXT("PointLookAround")));
	UStateTreeState* GuardWait = FindNoiseState(Gun, TEXT("GuardWait"));
	if (!Test.TestTrue(TEXT("Authored search settings exist before migration"), Anchor && Points && Fly && Look && GuardWait)) return false;
	Defaults.Anchor = *Anchor;
	Defaults.Points = *Points;
	Defaults.Fly = *Fly;
	// 런타임 모듈의 비공개 생성자를 직접 호출하지 않고, 리플렉션으로 기존 설정을 복사한다.
	Defaults.Look.InitializeAs(FEnemyLookAroundTaskInstanceData::StaticStruct(), reinterpret_cast<const uint8*>(Look));
	for (const FStateTreeEditorNode& Node : GuardWait->Tasks)
	{
		if (Node.Node.GetScriptStruct() && Node.Node.GetScriptStruct()->GetFName() == TEXT("StateTreeDelayTask")) Defaults.Wait = Node;
	}
	return Test.TestTrue(TEXT("Existing local guard delay can be reused without Engine private headers"), Defaults.Wait.Instance.IsValid());
}

void AddNoiseWait(UStateTreeState& State, const FNoiseSearchDefaults& Defaults, const bool bForever = false)
{
	FStateTreeEditorNode& Wait = State.Tasks.Add_GetRef(Defaults.Wait);
	Wait.ID = FGuid::NewGuid();
	if (FBoolProperty* Forever = FindFProperty<FBoolProperty>(Wait.Instance.GetScriptStruct(), TEXT("bRunForever")))
		Forever->SetPropertyValue_InContainer(Wait.Instance.GetMutableMemory(), bForever);
}

void BindNoiseEnemy(UStateTreeEditorData& Data, const FGuid Context, const FGuid Node)
{
	NoiseBind(Data, Context, TEXT(""), Node, TEXT("Enemy"));
}

UStateTreeState& AddNoiseSearch(UStateTreeEditorData& Data, UStateTreeState& Parent, UStateTreeState& Move,
	UStateTreeState& Combat, UStateTreeState& Reselect, const FGuid Sync, const FGuid EnemyContext,
	const FNoiseSearchDefaults& Defaults)
{
	UStateTreeState& Search = Parent.AddChildState(TEXT("NoiseSearch"));
	Search.Description = TEXT("실제 도달 위치를 고정 중심으로 수색. 새 소리/재발견은 수색 완료보다 먼저 전환한다.");
	Search.Parameters.Parameters.AddProperty(TEXT("Destination"), EPropertyBagPropertyType::Struct, TBaseStructure<FVector>::Get());
	auto& Session = Search.AddTask<FEnemySearchSessionTask>();
	Session.GetInstanceData().bNoiseInvestigationSearch = true;
	Session.GetInstanceData().NoiseSearchDuration = 5.0f;
	BindNoiseEnemy(Data, EnemyContext, Session.ID);

	UStateTreeState& Anchor = Search.AddChildState(TEXT("NoiseSelectAnchor"));
	UStateTreeState& MoveAnchor = Search.AddChildState(TEXT("NoiseMoveAnchor"));
	UStateTreeState& Arrived = Search.AddChildState(TEXT("NoiseAnchorArrived"));
	UStateTreeState& Points = Search.AddChildState(TEXT("NoiseSelectPoints"));
	UStateTreeState& Point = Search.AddChildState(TEXT("NoiseSelectPoint"));
	UStateTreeState& MovePoint = Search.AddChildState(TEXT("NoiseMovePoint"));
	UStateTreeState& Look = Search.AddChildState(TEXT("NoisePointLookAround"));
	UStateTreeState& Checked = Search.AddChildState(TEXT("NoisePointChecked"));
	UStateTreeState& Guard = Search.AddChildState(TEXT("NoiseEnterLocalGuard"));
	UStateTreeState& GuardLook = Search.AddChildState(TEXT("NoiseGuardLookAround"));
	UStateTreeState& GuardWait = Search.AddChildState(TEXT("NoiseGuardWait"));
	UStateTreeState& Finish = Search.AddChildState(TEXT("NoiseFinish"));

	// 형제 상태의 종료된 Task를 직접 읽지 않는다. 수색 부모의 위치 저장소만 공유한다.
	auto AddQuery = [&](UStateTreeState& State, const FEnemyRunEnvQueryTaskInstanceData& Authored)
	{
		auto& Task = State.AddTask<FEnemyRunEnvQueryTask>();
		Task.GetInstanceData() = Authored;
		BindNoiseEnemy(Data, EnemyContext, Task.ID);
		NoiseBind(Data, Search.Parameters.ID, TEXT("Destination"), Task.ID, TEXT("Destination"), true);
	};
	auto AddFly = [&](UStateTreeState& State)
	{
		auto& Task = State.AddTask<FEnemyFlyToLocationTask>();
		Task.GetInstanceData() = Defaults.Fly;
		Task.GetInstanceData().bInvestigateNoise = false;
		BindNoiseEnemy(Data, EnemyContext, Task.ID);
		NoiseBind(Data, Search.Parameters.ID, TEXT("Destination"), Task.ID, TEXT("Destination"));
	};
	auto AddProgress = [&](UStateTreeState& State, const EEnemySearchProgress Action, const bool bDestination = false)
	{
		auto& Task = State.AddTask<FEnemySearchProgressTask>();
		Task.GetInstanceData().Action = Action;
		BindNoiseEnemy(Data, EnemyContext, Task.ID);
		if (bDestination) NoiseBind(Data, Search.Parameters.ID, TEXT("Destination"), Task.ID, TEXT("Destination"), true);
	};
	auto AddLook = [&](UStateTreeState& State)
	{
		auto& Task = State.AddTask<FEnemyLookAroundTask>();
		Task.GetInstanceData() = Defaults.Look.Get<FEnemyLookAroundTaskInstanceData>();
		BindNoiseEnemy(Data, EnemyContext, Task.ID);
	};
	AddQuery(Anchor, Defaults.Anchor);
	AddFly(MoveAnchor);
	AddProgress(Arrived, EEnemySearchProgress::AnchorArrived);
	AddQuery(Points, Defaults.Points);
	AddProgress(Point, EEnemySearchProgress::SelectCurrentPoint, true);
	AddFly(MovePoint);
	AddLook(Look);
	AddProgress(Checked, EEnemySearchProgress::PointChecked);
	AddProgress(Guard, EEnemySearchProgress::LocalGuard);
	AddLook(GuardLook);
	AddNoiseWait(GuardWait, Defaults);
	AddProgress(Finish, EEnemySearchProgress::FinishNoiseInvestigation);

	struct FStep { UStateTreeState* From; UStateTreeState* To; UStateTreeState* Failure; };
	for (const FStep& Step : {FStep{&Anchor, &MoveAnchor, &Guard}, {&MoveAnchor, &Arrived, &Guard},
		{&Arrived, &Points, &Guard}, {&Points, &Point, &Guard}, {&Point, &MovePoint, &Guard},
		{&MovePoint, &Look, &Guard}, {&Look, &Checked, &Guard}, {&Checked, &Point, &Guard},
		{&Guard, &GuardLook, &GuardLook}, {&GuardLook, &GuardWait, &GuardWait}, {&GuardWait, &GuardLook, &GuardLook},
		{&Finish, &Reselect, &Reselect}})
	{
		NoiseGoto(*Step.From, *Step.To, EStateTreeTransitionTrigger::OnStateSucceeded);
		NoiseGoto(*Step.From, *Step.Failure, EStateTreeTransitionTrigger::OnStateFailed);
	}
	AddNoiseGoalTransition(Data, Search, Combat, Sync, EEnemyPerceptionGoalType::Combat);
	auto& Retarget = NoiseGoto(Search, Move);
	Retarget.Priority = EStateTreeTransitionPriority::High;
	AddNoiseBool(Data, Retarget, Session.ID, TEXT("bShouldInvestigateNoise"));
	auto& Complete = NoiseGoto(Search, Finish);
	AddNoiseBool(Data, Complete, Session.ID, TEXT("bNoiseSearchComplete"));
	return Search;
}

bool InstallNoiseBranch(FAutomationTestBase& Test, UStateTree& Tree, UStateTreeEditorData& Data,
	const FNoiseSearchDefaults& Defaults, const bool bCommon)
{
	UStateTreeState* Parent = FindNoiseState(Data, bCommon ? TEXT("Alert") : TEXT("CombatDecision"));
	UStateTreeState* Direct = bCommon ? Parent : FindNoiseState(Data, TEXT("DirectCombat"));
	FStateTreeEditorNode* SyncNode = FindNoiseSync(Data);
	const FGuid EnemyContext = Data.FindContextData(AEnemyBase::StaticClass(), TEXT("Enemy")).ID;
	if (!Test.TestTrue(TEXT("Canonical graph has selector, sync and Enemy context"), Parent && Direct && SyncNode && EnemyContext.IsValid())) return false;
	if (FindNoiseState(Data, TEXT("InvestigateNoise"))) return true;
	Tree.Modify();
	Data.Modify();
	Parent->Modify();
	const FGuid Sync = SyncNode->ID;
	TArray<UStateTreeState*> Previous;
	CollectNoiseStates(Parent, Previous);

	if (bCommon)
	{
		// Alert 시간 판정은 부모에 한 번만 둔다. 조사 자식이 선택돼도 최초 발견 확정 규칙은 유지한다.
		for (UStateTreeState* State : Previous)
		{
			if (State == Parent) continue;
			for (int32 Index = State->Tasks.Num() - 1; Index >= 0; --Index)
			{
				if (State->Tasks[Index].Node.GetScriptStruct() != FEnemyAlertHoldTask::StaticStruct()) continue;
				Parent->Tasks.Add(State->Tasks[Index]);
				State->Tasks.RemoveAt(Index);
				if (State->Tasks.IsEmpty()) AddNoiseWait(*State, Defaults, true);
			}
		}
		if (Parent->Children.IsEmpty())
		{
			UStateTreeState& Hold = Parent->AddChildState(TEXT("AlertWait"));
			AddNoiseWait(Hold, Defaults, true);
		}
	}

	UStateTreeState& Flow = Parent->AddChildState(TEXT("NoiseInvestigation"));
	auto& FlowGoal = Flow.AddEnterCondition<FStateTreeCompareEnumCondition>();
	ConfigureNoiseGoal(Data, FlowGoal, Sync, EEnemyPerceptionGoalType::InvestigateNoise);
	auto& FlowActive = Flow.AddEnterCondition<FStateTreeCompareBoolCondition>();
	FlowActive.ExpressionOperand = EStateTreeExpressionOperand::Or;
	FlowActive.GetInstanceData().bRight = true;
	NoiseBind(Data, Sync, TEXT("bNoiseInvestigationActive"), FlowActive.ID, TEXT("bLeft"));
	// Fly/Session이 같은 Tick에서 Pawn 의도를 바꾼다. 명시적 자식 전환은 이전 Sync 값으로 재검사하지 않는다.
	UStateTreeState& Move = Flow.AddChildState(TEXT("InvestigateNoise"));
	Move.Description = TEXT("플레이어의 실제 위치가 아닌 소리 발생 좌표로 이동. 도착/탐색 소진만 수색에 인계한다.");
	auto& Fly = Move.AddTask<FEnemyFlyToLocationTask>();
	Fly.GetInstanceData() = Defaults.Fly;
	Fly.GetInstanceData().bInvestigateNoise = true;
	Fly.GetInstanceData().bUsePathfinding = true;
	Fly.GetInstanceData().bUseInitialPath = false;
	Fly.GetInstanceData().bUsePlannedPath = false;
	Fly.GetInstanceData().bReturnHome = false;
	Fly.GetInstanceData().GoalSearchRadius = 200.0f;
	Fly.GetInstanceData().MoveTimeout = 30.0f;
	Fly.GetInstanceData().NoProgressTimeout = 3.0f;
	BindNoiseEnemy(Data, EnemyContext, Fly.ID);
	NoiseBind(Data, Sync, TEXT("PerceptionGoalLocation"), Fly.ID, TEXT("Destination"));
	UStateTreeState& Recover = Flow.AddChildState(TEXT("NoiseRecover"));
	AddNoiseWait(Recover, Defaults);
	for (const TCHAR* Property : {TEXT("Duration"), TEXT("RandomDeviation")})
		if (FFloatProperty* Value = FindFProperty<FFloatProperty>(Recover.Tasks[0].Instance.GetScriptStruct(), Property))
			Value->SetPropertyValue_InContainer(Recover.Tasks[0].Instance.GetMutableMemory(), FCString::Strcmp(Property, TEXT("Duration")) == 0 ? 0.01f : 0.0f);
	// 인터럽트 직후 같은 프레임의 낡은 Goal로 부모를 재선택하면 실패/재진입 루프가 생긴다.
	// 한 번 Tick을 넘겨 Sync 갱신 후 기존 경계/전투 선택기로 복귀한다.
	NoiseGoto(Recover, *Parent, EStateTreeTransitionTrigger::OnStateSucceeded);
	UStateTreeState& Search = AddNoiseSearch(Data, Flow, Move, *Direct, Recover, Sync, EnemyContext, Defaults);
	NoiseGoto(Search, Recover, EStateTreeTransitionTrigger::OnStateFailed);
	Flow.Children.Remove(&Recover);
	Flow.Children.Add(&Recover);
	// 기존 전투/경계 조건보다 앞에 두되 커밋된 돌진은 Goal이 CommittedAction이므로 진입하지 않는다.
	Parent->Children.Remove(&Flow);
	Parent->Children.Insert(&Flow, 0);
	AddNoiseGoalTransition(Data, Move, *Direct, Sync, EEnemyPerceptionGoalType::Combat);
	for (const EStateTreeTransitionTrigger Trigger : {EStateTreeTransitionTrigger::OnStateSucceeded, EStateTreeTransitionTrigger::OnStateFailed})
	{
		auto& Ready = NoiseGoto(Move, Search, Trigger);
		AddNoiseBool(Data, Ready, Fly.ID, TEXT("bNoiseSearchReady"));
		auto& Interrupted = NoiseGoto(Move, Recover, Trigger);
		AddNoiseBool(Data, Interrupted, Fly.ID, TEXT("bNoiseSearchReady"), false);
	}

	for (UStateTreeState* State : Previous)
	{
		// 중립 TrackingChanged 이벤트로 상태를 재진입시키지 않고 현재 의도만 비교한다.
		// 자폭 커밋 중에는 이 조건이 성립하지 않아 기존 Charge/Detonate 흐름을 유지한다.
		if (!bCommon && (State == Direct || State->Name == TEXT("Search")))
		{
			AddNoiseGoalTransition(Data, *State, Flow, Sync, EEnemyPerceptionGoalType::InvestigateNoise);
			AddNoiseBool(Data, State->Transitions.Last(), Sync, TEXT("bNoiseInvestigationActive"), false);
		}
		if (!bCommon)
		{
			for (FStateTreeTransition& Transition : State->Transitions)
				if (Transition.State.ID == Direct->ID) AddNoiseGoal(Data, Transition, Sync, EEnemyPerceptionGoalType::Combat);
		}
	}
	if (bCommon)
	{
		for (UStateTreeState* Child : Parent->Children)
			if (Child != &Flow) AddNoiseGoalTransition(Data, *Child, Flow, Sync, EEnemyPerceptionGoalType::InvestigateNoise);
	}
	if (!bCommon)
	{
		// 공유 추적 정보는 조사 동안 보존된다. 그 존재만으로 조사 중 직접 전투를 다시 선택하지 않는다.
		AddNoiseEnterBool(Data, *Direct, Sync, TEXT("bNoiseInvestigationActive"), false);
		if (UStateTreeState* ExistingSearch = FindNoiseState(Data, TEXT("Search")))
			AddNoiseEnterBool(Data, *ExistingSearch, Sync, TEXT("bNoiseInvestigationActive"), false);
	}
	return true;
}

bool HasNoiseBinding(const UStateTreeEditorData& Data, const FGuid Target, const TCHAR* Input,
	const FGuid Source, const TCHAR* Output)
{
	for (const FStateTreePropertyPathBinding& Binding : Data.EditorBindings.GetBindings())
	{
		if (Binding.GetTargetPath().GetStructID() == Target && Binding.GetTargetPath().ToString() == Input
			&& Binding.GetSourcePath().GetStructID() == Source && Binding.GetSourcePath().ToString() == Output) return true;
	}
	return false;
}

bool ValidateNoiseBranch(FAutomationTestBase& Test, UStateTree& Tree, UStateTreeEditorData& Data)
{
	UStateTreeState* Move = FindNoiseState(Data, TEXT("InvestigateNoise"));
	UStateTreeState* Search = FindNoiseState(Data, TEXT("NoiseSearch"));
	if (!Test.TestTrue(Tree.GetName() + TEXT(" contains installed noise states (-OutlierInstallNoiseInvestigationState)"), Move && Search)) return false;
	const auto* Fly = FindAuthoredNoiseData<FEnemyFlyToLocationTask>(Move);
	const auto* Session = FindAuthoredNoiseData<FEnemySearchSessionTask>(Search);
	Test.TestTrue(TEXT("Noise move uses bounded AStar investigation"), Fly && Fly->bInvestigateNoise && Fly->bUsePathfinding
		&& !Fly->bReturnHome && !Fly->bUseInitialPath && !Fly->bUsePlannedPath && Fly->MoveTimeout > 0.0f && Fly->NoProgressTimeout > 0.0f);
	Test.TestTrue(TEXT("Noise search owns a finite opt-in session"), Session && Session->bNoiseInvestigationSearch && Session->NoiseSearchDuration > 0.0f);
	Test.TestTrue(TEXT("Noise move and search share the same selector"), Move->Parent == Search->Parent);
	Test.TestTrue(TEXT("Explicit same-tick handoffs never recheck stale Sync child guards"), Move->EnterConditions.IsEmpty() && Search->EnterConditions.IsEmpty());
	Test.TestTrue(TEXT("Compiled runtime contains investigation and search states"), Tree.GetStateHandleFromId(Move->ID).IsValid()
		&& Tree.GetStateHandleFromId(Search->ID).IsValid());
	Test.TestTrue(TEXT("Noise search starts without LossWait"), !Search->Children.IsEmpty() && Search->Children[0]->Name == TEXT("NoiseSelectAnchor"));
	int32 ReadyTransitions = 0;
	bool bReadySuccess = false, bReadyFailure = false;
	const FGuid FlyID = Move->Tasks.IsEmpty() ? FGuid() : Move->Tasks[0].ID;
	for (const FStateTreeTransition& Transition : Move->Transitions)
	{
		if (Transition.State.ID != Search->ID) continue;
		for (const FStateTreeEditorNode& Condition : Transition.Conditions)
			if (const auto* Bool = Condition.Instance.GetPtr<FStateTreeCompareBoolConditionInstanceData>();
				Bool && Bool->bRight && HasNoiseBinding(Data, Condition.ID, TEXT("bLeft"), FlyID, TEXT("bNoiseSearchReady")))
			{
				++ReadyTransitions;
				bReadySuccess |= Transition.Trigger == EStateTreeTransitionTrigger::OnStateSucceeded;
				bReadyFailure |= Transition.Trigger == EStateTreeTransitionTrigger::OnStateFailed;
			}
	}
	Test.TestEqual(TEXT("Both move success and failure require an explicit search handoff"), ReadyTransitions, 2);
	Test.TestTrue(TEXT("Both terminal move statuses support reached/exhausted handoff"), bReadySuccess && bReadyFailure);
	TArray<UStateTreeState*> NoiseStates;
	CollectNoiseStates(Search, NoiseStates);
	bool bHasFinish = false;
	for (const UStateTreeState* State : NoiseStates)
	{
		for (const FStateTreeEditorNode& Task : State->Tasks)
		{
			const UScriptStruct* Type = Task.Node.GetScriptStruct();
			if (!Type) continue;
			Test.TestTrue(TEXT("Authored noise tasks exist in compiled runtime data"), Tree.GetNodeIndexFromId(Task.ID).IsValid());
			Test.TestFalse(TEXT("Sound investigation never installs attack tasks"), Type->GetName().Contains(TEXT("Attack")));
			if (const auto* Progress = Task.Instance.GetPtr<FEnemySearchProgressTaskInstanceData>())
				bHasFinish |= Progress->Action == EEnemySearchProgress::FinishNoiseInvestigation;
		}
	}
	Test.TestTrue(TEXT("Search completion performs authoritative evidence/return resolution"), bHasFinish);
	return !Test.HasAnyErrors();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyNoiseInvestigationStateBindingsTest,
	"Outlier.Enemy.Perception.Investigation.StateBindings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyNoiseInvestigationStateBindingsTest::RunTest(const FString& Parameters)
{
	const bool bInstall = FParse::Param(FCommandLine::Get(), TEXT("OutlierInstallNoiseInvestigationState"));
	TArray<UStateTree*> Trees;
	TArray<UStateTreeEditorData*> Data;
	for (const TCHAR* Name : {TEXT("ST_VECDrone_Common"), TEXT("ST_VECDroneBattle_Gun"), TEXT("ST_VECDroneBattle_Melee")})
	{
		UStateTree* Tree = LoadObject<UStateTree>(nullptr, *(FString(NoiseTreeFolder) + Name + TEXT(".") + Name));
		if (!TestNotNull(TEXT("Canonical noise investigation tree loads"), Tree)) return false;
		UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(Tree->EditorData);
		if (!TestNotNull(TEXT("Canonical editor graph exists"), EditorData)) return false;
		if (bInstall && !TestFalse(TEXT("Migration never saves preexisting dirty packages"), Tree->GetOutermost()->IsDirty())) return false;
		Trees.Add(Tree);
		Data.Add(EditorData);
	}
	FNoiseSearchDefaults Defaults;
	if (!ReadNoiseDefaults(*this, *Data[1], Defaults)) return false;
	TArray<UStateTree*> Changed;
	if (bInstall)
	{
		// 전 패키지를 메모리에서 연결/검증/컴파일한 뒤에만 저장 단계로 진입한다.
		for (int32 Index = 0; Index < Trees.Num(); ++Index)
		{
			if (FindNoiseState(*Data[Index], TEXT("InvestigateNoise"))) continue;
			if (!InstallNoiseBranch(*this, *Trees[Index], *Data[Index], Defaults, Index == 0)) return false;
			Changed.Add(Trees[Index]);
		}
		for (UStateTree* Tree : Changed)
		{
			FStateTreeCompilerLog Log;
			FStateTreeCompiler Compiler(Log);
			if (!Compiler.Compile(*Tree))
			{
				Log.DumpToLog(LogTemp);
				AddError(Tree->GetName() + TEXT(" noise migration failed compilation; no package has been saved"));
				return false;
			}
		}
	}
	for (int32 Index = 0; Index < Trees.Num(); ++Index)
	{
		if (!ValidateNoiseBranch(*this, *Trees[Index], *Data[Index])) return false;
		if (!TestTrue(TEXT("Saved/installed tree has compiled runtime data"), Trees[Index]->IsReadyToRun())) return false;
	}
	for (UStateTree* Tree : Changed)
	{
		Tree->MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		const FString Filename = FPackageName::LongPackageNameToFilename(Tree->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		if (!TestTrue(TEXT("Save only validated canonical noise tree"), UPackage::SavePackage(Tree->GetOutermost(), Tree, *Filename, Args))) return false;
		AddInfo(TEXT("Installed noise investigation: ") + Tree->GetPathName());
	}
	return !HasAnyErrors();
}

#endif
