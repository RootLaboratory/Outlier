#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Conditions/StateTreeCommonConditions.h"
#include "Enemy/AutoTurret.h"
#include "Enemy/EnemyStateTreeSchema.h"
#include "Enemy/EnemyStateTreeSyncTask.h"
#include "Enemy/EnemyStateTreeTurretTasks.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "StateTree.h"
#include "StateTreeCompiler.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeState.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace
{
UStateTreeState* FindTurretState(UStateTreeState* State, FName Name)
{
	if (State->Name == Name) return State;
	for (UStateTreeState* Child : State->Children)
		if (UStateTreeState* Found = FindTurretState(Child, Name)) return Found;
	return nullptr;
}

bool HasTurretBinding(const UStateTreeEditorData& Data, FGuid Source, const TCHAR* Output, FGuid Target, const TCHAR* Input)
{
	for (const auto& Binding : Data.EditorBindings.GetBindings())
		if (Binding.GetSourcePath().GetStructID() == Source && Binding.GetSourcePath().ToString() == Output
			&& Binding.GetTargetPath().GetStructID() == Target && Binding.GetTargetPath().ToString() == Input) return true;
	return false;
}

void BindTurret(UStateTreeEditorData& Data, FGuid Source, const TCHAR* Output, FGuid Target, const TCHAR* Input)
{
	FPropertyBindingPath From, To;
	From.SetStructID(Source); From.FromString(Output);
	To.SetStructID(Target); To.FromString(Input);
	Data.AddPropertyBinding(From, To);
}

bool HasHackGate(const UStateTreeEditorData& Data, const TArray<FStateTreeEditorNode>& Conditions, FGuid Sync, bool bActive)
{
	for (const auto& Condition : Conditions)
	{
		const auto* Node = Condition.Node.GetPtr<FStateTreeCompareBoolCondition>();
		const auto* Instance = Condition.Instance.GetPtr<FStateTreeCompareBoolConditionInstanceData>();
		if (Node && !Node->bInvert && Instance && Instance->bRight == bActive
			&& HasTurretBinding(Data, Sync, TEXT("bHackingInProgress"), Condition.ID, TEXT("bLeft"))) return true;
	}
	return false;
}

bool HasStunGate(const UStateTreeEditorData& Data, const TArray<FStateTreeEditorNode>& Conditions, FGuid Sync, bool bStunned)
{
	for (const auto& Condition : Conditions)
	{
		const auto* Node = Condition.Node.GetPtr<FStateTreeCompareEnumCondition>();
		const auto* Instance = Condition.Instance.GetPtr<FStateTreeCompareEnumConditionInstanceData>();
		if (Node && Node->bInvert == !bStunned && Instance
			&& Instance->Right.Enum == StaticEnum<EEnemyCombatState>() && Instance->Right.Value == uint32(EEnemyCombatState::Stun)
			&& HasTurretBinding(Data, Sync, TEXT("CombatState"), Condition.ID, TEXT("Left"))) return true;
	}
	return false;
}

void AddHackGate(UStateTreeEditorData& Data, TArray<FStateTreeEditorNode>& Conditions, FGuid Sync, bool bActive)
{
	if (HasHackGate(Data, Conditions, Sync, bActive)) return;
	// 기존 식을 하나의 묶음으로 유지하고 새 제한을 AND로 연결한다.
	for (auto& Existing : Conditions) ++Existing.ExpressionIndent;
	FStateTreeTransition Builder;
	auto& Condition = Builder.AddCondition<FStateTreeCompareBoolCondition>();
	Condition.GetInstanceData().bRight = bActive;
	BindTurret(Data, Sync, TEXT("bHackingInProgress"), Condition.ID, TEXT("bLeft"));
	Conditions.Append(MoveTemp(Builder.Conditions));
}

void AddStunGate(UStateTreeEditorData& Data, TArray<FStateTreeEditorNode>& Conditions, FGuid Sync, bool bStunned)
{
	if (HasStunGate(Data, Conditions, Sync, bStunned)) return;
	for (auto& Existing : Conditions) ++Existing.ExpressionIndent;
	FStateTreeTransition Builder;
	auto& Condition = Builder.AddCondition<FStateTreeCompareEnumCondition>(!bStunned);
	Condition.GetInstanceData().Left.Initialize(StaticEnum<EEnemyCombatState>());
	Condition.GetInstanceData().Right.Initialize(StaticEnum<EEnemyCombatState>());
	Condition.GetInstanceData().Right.Value = uint32(EEnemyCombatState::Stun);
	BindTurret(Data, Sync, TEXT("CombatState"), Condition.ID, TEXT("Left"));
	Conditions.Append(MoveTemp(Builder.Conditions));
}

bool HasCombatGate(const UStateTreeEditorData& Data, const TArray<FStateTreeEditorNode>& Conditions, FGuid Sync, bool bCombat)
{
	for (const auto& Condition : Conditions)
	{
		const auto* Node = Condition.Node.GetPtr<FStateTreeCompareEnumCondition>();
		const auto* Instance = Condition.Instance.GetPtr<FStateTreeCompareEnumConditionInstanceData>();
		if (Node && Node->bInvert == !bCombat && Instance
			&& Instance->Right.Enum == StaticEnum<EEnemyCombatState>() && Instance->Right.Value == uint32(EEnemyCombatState::Combat)
			&& HasTurretBinding(Data, Sync, TEXT("CombatState"), Condition.ID, TEXT("Left"))) return true;
	}
	return false;
}

void AddCombatGate(UStateTreeEditorData& Data, TArray<FStateTreeEditorNode>& Conditions, FGuid Sync)
{
	if (HasCombatGate(Data, Conditions, Sync, true)) return;
	for (auto& Existing : Conditions) ++Existing.ExpressionIndent;
	FStateTreeTransition Builder;
	auto& Condition = Builder.AddCondition<FStateTreeCompareEnumCondition>();
	Condition.GetInstanceData().Left.Initialize(StaticEnum<EEnemyCombatState>());
	Condition.GetInstanceData().Right.Initialize(StaticEnum<EEnemyCombatState>());
	Condition.GetInstanceData().Right.Value = uint32(EEnemyCombatState::Combat);
	BindTurret(Data, Sync, TEXT("CombatState"), Condition.ID, TEXT("Left"));
	Conditions.Append(MoveTemp(Builder.Conditions));
}

FStateTreeTransition& EnsureTurretTransition(UStateTreeState& From, UStateTreeState& To,
	EStateTreeTransitionTrigger Trigger, FGameplayTag Event, EStateTreeTransitionPriority Priority)
{
	for (auto& Transition : From.Transitions)
		if (Transition.Trigger == Trigger && Transition.RequiredEvent.Tag == Event && Transition.State.ID == To.ID)
		{
			Transition.Priority = Priority;
			return Transition;
		}
	auto& Transition = From.AddTransition(Trigger, Event, EStateTreeTransitionType::GotoState, &To);
	Transition.Priority = Priority;
	return Transition;
}

void AuditTurretNode(const FStateTreeEditorNode& Node, FString& Out)
{
	if (!Node.Node.IsValid()) return;
	Out += FString::Printf(TEXT("  Node %s %s ID=%s\n"), *Node.Node.GetScriptStruct()->GetName(),
		*Node.Node.Get<FStateTreeNodeBase>().Name.ToString(), *Node.ID.ToString());
	FString Definition;
	Node.Node.GetScriptStruct()->ExportText(Definition, Node.Node.GetMemory(), nullptr, nullptr, PPF_None, nullptr);
	Out += TEXT("    Definition=") + Definition + TEXT("\n");
	if (Node.Instance.IsValid())
	{
		FString Instance;
		Node.Instance.GetScriptStruct()->ExportText(Instance, Node.Instance.GetMemory(), nullptr, nullptr, PPF_None, nullptr);
		Out += TEXT("    ") + Instance + TEXT("\n");
	}
}

void AuditTurretState(UStateTreeState* State, FString& Out)
{
	if (!State) return;
	Out += FString::Printf(TEXT("State %s ID=%s Parent=%s Type=%d Selection=%d Linked=%s\n"),
		*State->Name.ToString(), *State->ID.ToString(), State->Parent ? *State->Parent->Name.ToString() : TEXT("None"),
		int32(State->Type), int32(State->SelectionBehavior), *GetPathNameSafe(State->LinkedAsset));
	Out += FString::Printf(TEXT("  Enabled=%d RequiredEvent=%d:%s\n"), State->bEnabled,
		State->bHasRequiredEventToEnter, *State->RequiredEventToEnter.Tag.ToString());
	for (const auto& Node : State->Tasks) AuditTurretNode(Node, Out);
	AuditTurretNode(State->SingleTask, Out);
	for (const auto& Node : State->EnterConditions) AuditTurretNode(Node, Out);
	for (const auto& Transition : State->Transitions)
	{
		Out += FString::Printf(TEXT("  Transition Trigger=%d Event=%s Target=%s ID=%s Type=%d Priority=%d\n"),
			int32(Transition.Trigger), *Transition.RequiredEvent.Tag.ToString(), *Transition.State.Name.ToString(),
			*Transition.State.ID.ToString(), int32(Transition.State.LinkType), int32(Transition.Priority));
		for (const auto& Node : Transition.Conditions) AuditTurretNode(Node, Out);
	}
	for (UStateTreeState* Child : State->Children) AuditTurretState(Child, Out);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAutoTurretHackingStateBindingsTest,
	"Outlier.Enemy.Turret.HackingAsset", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAutoTurretHackingStateBindingsTest::RunTest(const FString& Parameters)
{
	const bool bInstall = FParse::Param(FCommandLine::Get(), TEXT("OutlierInstallTurretHacking"));
	const bool bRepairStunGate = FParse::Param(FCommandLine::Get(), TEXT("OutlierRepairTurretHackingStunGate"));
	const bool bSave = bInstall || bRepairStunGate;
	UStateTree* Tree = LoadObject<UStateTree>(nullptr,
		TEXT("/Game/Blueprints/AI/Turret/ST_AutoTurretCommon.ST_AutoTurretCommon"));
	if (!TestNotNull(TEXT("Canonical turret tree exists"), Tree)) return false;
	auto* Data = Cast<UStateTreeEditorData>(Tree->EditorData);
	if (!TestNotNull(TEXT("Authoring data exists"), Data)) return false;
	if (bSave && !TestFalse(TEXT("Never save a preexisting dirty tree"), Tree->GetOutermost()->IsDirty())) return false;
	if (!TestEqual(TEXT("One common root"), Data->SubTrees.Num(), 1)) return false;
	UStateTreeState* Root = Data->SubTrees[0];
	UStateTreeState* Hacking = FindTurretState(Root, TEXT("Hacking"));
	UStateTreeState* Battle = FindTurretState(Root, TEXT("Battle"));
	UStateTreeState* Stun = FindTurretState(Root, TEXT("Stun"));
	UStateTreeState* Death = FindTurretState(Root, TEXT("Death"));
	UStateTreeState* Hidden = FindTurretState(Root, TEXT("Hidden"));
	UStateTreeState* Deploy = FindTurretState(Root, TEXT("Deploy"));
	UStateTreeState* Idle = FindTurretState(Root, TEXT("Idle"));
	UStateTreeState* DirectCombat = FindTurretState(Root, TEXT("DirectCombat"));
	UStateTreeState* Resume = FindTurretState(Root, TEXT("ResumeAfterHacking"));
	if (!TestNotNull(TEXT("Hacking state exists"), Hacking) || !TestNotNull(TEXT("Battle state exists"), Battle)
		|| !TestNotNull(TEXT("Stun state exists"), Stun) || !TestNotNull(TEXT("Death state exists"), Death)
		|| !TestNotNull(TEXT("Hidden state exists"), Hidden) || !TestNotNull(TEXT("Deploy state exists"), Deploy)) return false;
	if (!TestNotNull(TEXT("Direct combat state exists"), DirectCombat)) return false;
	FGuid Sync;
	for (const auto& Task : Data->GlobalTasks)
		if (Task.Node.GetPtr<FEnemyStateTreeSyncTask>()) Sync = Task.ID;
	if (!TestTrue(TEXT("Enemy State Sync is global"), Sync.IsValid())) return false;
	const auto Contexts = Data->Schema->GetContextDataDescs();
	const FStateTreeExternalDataDesc* Actor = Contexts.FindByPredicate([](const auto& Context)
	{
		return Context.Struct && Context.Struct->IsChildOf(AAutoTurret::StaticClass());
	});
	if (!TestNotNull(TEXT("Actor context is typed as AutoTurret"), Actor)) return false;
	const FGameplayTag Started = FGameplayTag::RequestGameplayTag(TEXT("Enemy.Event.Hacking.Started"));
	const FGameplayTag StunEnded = FGameplayTag::RequestGameplayTag(TEXT("Enemy.Event.Status.StunEnded"));
	const FGameplayTag StunStarted = FGameplayTag::RequestGameplayTag(TEXT("Enemy.Event.Status.StunStarted"));
	const FGameplayTag Died = FGameplayTag::RequestGameplayTag(TEXT("Enemy.Event.Died"));
	const FGameplayTag CombatEntered = FGameplayTag::RequestGameplayTag(TEXT("Enemy.Event.Combat.Entered"));
	if (bInstall)
	{
		if (!Resume)
		{
			if (!TestEqual(TEXT("Hidden has one wait Task"), Hidden->Tasks.Num(), 1)) return false;
			Resume = &Root->AddChildState(TEXT("ResumeAfterHacking"));
			Resume->Description = TEXT("Enter 중 완료된 None 결과도 State Sync 갱신 뒤 현재 조건으로 복귀한다. 고정 시간 지연은 없다.");
			Resume->Tasks.Add(Hidden->Tasks[0]);
			Resume->Tasks[0].ID = FGuid::NewGuid();
			// 이 상태는 완료 전이로만 진입한다. Root의 기존 기본 선택 순서는 유지한다.
		}
		if (!Idle)
		{
			// Private 엔진 Task를 새로 참조하지 않고 에셋의 기존 무한 대기 Task를 재사용한다.
			if (!TestEqual(TEXT("Hidden has one wait Task"), Hidden->Tasks.Num(), 1)) return false;
			Idle = &Battle->AddChildState(TEXT("Idle"));
			Idle->Description = TEXT("해킹 종료 후 표적이 없으면 활성 대기를 유지하고 전투 상태 변경 시 다시 판단한다.");
			Idle->Tasks.Add(Hidden->Tasks[0]);
			Idle->Tasks[0].ID = FGuid::NewGuid();
			auto& Gate = Idle->AddEnterCondition<FStateTreeCompareEnumCondition>();
			Gate.GetInstanceData().Left.Initialize(StaticEnum<EEnemyCombatState>());
			Gate.GetInstanceData().Right.Initialize(StaticEnum<EEnemyCombatState>());
			Gate.GetInstanceData().Right.Value = uint32(EEnemyCombatState::NonCombat);
			BindTurret(*Data, Sync, TEXT("CombatState"), Gate.ID, TEXT("Left"));
			Battle->Children.Remove(Idle);
			Battle->Children.Insert(Idle, 0);
			auto& Wake = EnsureTurretTransition(*Idle, *Battle, EStateTreeTransitionTrigger::OnTick, {}, EStateTreeTransitionPriority::Normal);
			auto& Changed = Wake.AddCondition<FStateTreeCompareEnumCondition>(true);
			Changed.GetInstanceData().Left.Initialize(StaticEnum<EEnemyCombatState>());
			Changed.GetInstanceData().Right.Initialize(StaticEnum<EEnemyCombatState>());
			Changed.GetInstanceData().Right.Value = uint32(EEnemyCombatState::NonCombat);
			BindTurret(*Data, Sync, TEXT("CombatState"), Changed.ID, TEXT("Left"));
			EnsureTurretTransition(*Idle, *Battle, EStateTreeTransitionTrigger::OnEvent,
				FGameplayTag::RequestGameplayTag(TEXT("Enemy.Event.Perception.TargetAcquired")), EStateTreeTransitionPriority::Normal);
		}
		// 비전투 대기도 탐색하고, 적을 발견하면 Alert 인지를 확정한 뒤 공격으로 넘긴다.
		Idle->Description = TEXT("표적이 없으면 머리 탐색을 유지하고, 발견한 적의 인지 완료 후 Battle 공격을 선택한다.");
		if (Idle->Tasks.Num() != 1 || !Idle->Tasks[0].Node.GetPtr<FEnemyTurretSearchTask>())
		{
			Idle->Tasks.Reset();
			auto& Search = Idle->AddTask<FEnemyTurretSearchTask>();
			BindTurret(*Data, Actor->ID, TEXT(""), Search.ID, TEXT("Turret"));
		}
		Idle->Tasks[0].Instance.GetMutable<FEnemyTurretSearchTaskInstanceData>().bMaintainNonCombatSearch = true;
		if (!TestEqual(TEXT("Idle has one state gate"), Idle->EnterConditions.Num(), 1)) return false;
		Idle->EnterConditions[0].Node.GetMutable<FStateTreeCompareEnumCondition>().bInvert = true;
		Idle->EnterConditions[0].Instance.GetMutable<FStateTreeCompareEnumConditionInstanceData>().Right.Value = uint32(EEnemyCombatState::Combat);
		auto& WakeCombat = EnsureTurretTransition(*Idle, *Battle, EStateTreeTransitionTrigger::OnTick, {}, EStateTreeTransitionPriority::Normal);
		if (!TestEqual(TEXT("Idle wake has one state gate"), WakeCombat.Conditions.Num(), 1)) return false;
		WakeCombat.Conditions[0].Node.GetMutable<FStateTreeCompareEnumCondition>().bInvert = false;
		WakeCombat.Conditions[0].Instance.GetMutable<FStateTreeCompareEnumConditionInstanceData>().Right.Value = uint32(EEnemyCombatState::Combat);
		AddCombatGate(*Data, DirectCombat->EnterConditions, Sync);
		AddHackGate(*Data, Hacking->EnterConditions, Sync, true);
		AddStunGate(*Data, Hacking->EnterConditions, Sync, false);
		AddHackGate(*Data, Battle->EnterConditions, Sync, false);
		AddStunGate(*Data, Battle->EnterConditions, Sync, false);
		AddStunGate(*Data, Stun->EnterConditions, Sync, true);
		// Root의 기본 Hidden 진입은 보존한다. 해킹 종료는 Battle의 자식을 새로 고른다.
		// Task Enter에서 결과를 확정하면 Sync 출력은 아직 옛 값일 수 있다.
		// 완료 전이는 무조건 선택 가능한 재개 상태로 보내고 다음 평가에서 조건을 읽는다.
		Hacking->Transitions.RemoveAll([&](const auto& Transition)
		{
			return Transition.Trigger == EStateTreeTransitionTrigger::OnStateCompleted && Transition.State.ID == Battle->ID;
		});
		EnsureTurretTransition(*Hacking, *Resume, EStateTreeTransitionTrigger::OnStateCompleted, {}, EStateTreeTransitionPriority::Normal);
		// Hidden은 에셋의 기본 대기이기도 하다. 실제 수락 가능 여부는 Pawn lifecycle이 결정한다.
		// 전개/비활성 Pawn에는 회차가 생기지 않으므로 이 연결이 해킹 자격을 넓히지는 않는다.
		for (UStateTreeState* Source : { Battle, Stun, Resume, Hidden, Deploy })
		{
			for (const auto Trigger : { EStateTreeTransitionTrigger::OnEvent, EStateTreeTransitionTrigger::OnTick })
			{
				auto& Transition = EnsureTurretTransition(*Source, *Hacking, Trigger,
					Trigger == EStateTreeTransitionTrigger::OnEvent ? Started : FGameplayTag(), EStateTreeTransitionPriority::Medium);
				AddHackGate(*Data, Transition.Conditions, Sync, true);
				AddStunGate(*Data, Transition.Conditions, Sync, false);
			}
		}
		auto& ResumeHack = EnsureTurretTransition(*Stun, *Hacking, EStateTreeTransitionTrigger::OnEvent, StunEnded, EStateTreeTransitionPriority::Medium);
		AddHackGate(*Data, ResumeHack.Conditions, Sync, true);
		AddStunGate(*Data, ResumeHack.Conditions, Sync, false);
		for (const auto Trigger : { EStateTreeTransitionTrigger::OnEvent, EStateTreeTransitionTrigger::OnTick })
		{
			auto& ResumeBattle = EnsureTurretTransition(*Stun, *Battle, Trigger,
				Trigger == EStateTreeTransitionTrigger::OnEvent ? StunEnded : FGameplayTag(), EStateTreeTransitionPriority::Normal);
			AddHackGate(*Data, ResumeBattle.Conditions, Sync, false);
			AddStunGate(*Data, ResumeBattle.Conditions, Sync, false);
			auto& FinishResume = EnsureTurretTransition(*Resume, *Battle, Trigger,
				Trigger == EStateTreeTransitionTrigger::OnEvent
					? FGameplayTag::RequestGameplayTag(TEXT("Enemy.Event.Hacking.Ended")) : FGameplayTag(), EStateTreeTransitionPriority::Normal);
			AddHackGate(*Data, FinishResume.Conditions, Sync, false);
			AddStunGate(*Data, FinishResume.Conditions, Sync, false);
		}
		for (auto& Transition : Root->Transitions)
		{
			if (Transition.RequiredEvent.Tag == Died) Transition.Priority = EStateTreeTransitionPriority::Critical;
			// 해킹 시작 시 제거된 스턴의 옛 Started 이벤트로 다시 멈추지 않는다.
			if (Transition.RequiredEvent.Tag == StunStarted) AddStunGate(*Data, Transition.Conditions, Sync, true);
			if (Transition.RequiredEvent.Tag == CombatEntered)
			{
				AddHackGate(*Data, Transition.Conditions, Sync, false);
				AddStunGate(*Data, Transition.Conditions, Sync, false);
			}
		}
	}
	// 기존 편집 내용을 보존하며 이 조건만 복구한다. 일반 회귀 실행은 에셋을 저장하지 않는다.
	if (bRepairStunGate) AddStunGate(*Data, Hacking->EnterConditions, Sync, false);
	TestTrue(TEXT("Hacking is outside Battle"), Hacking->Parent == Root && Battle->Parent == Root);
	if (!TestNotNull(TEXT("Hacking completion has a sync-safe resume state"), Resume)) return false;
	TestTrue(TEXT("Resume accepts completion before Sync refresh"), Resume->Parent == Root && Resume->EnterConditions.IsEmpty()
		&& !Resume->bHasRequiredEventToEnter && Resume->Tasks.Num() == 1);
	if (!TestNotNull(TEXT("Battle has an idle fallback after hacking"), Idle)) return false;
	TestTrue(TEXT("Idle is selected before combat/search branches"), Idle->Parent == Battle && Battle->Children[0] == Idle);
	TestEqual(TEXT("Idle has one searching Task"), Idle->Tasks.Num(), 1);
	if (Idle->Tasks.Num() == 1)
	{
		const auto& Search = Idle->Tasks[0];
		const auto* Instance = Search.Instance.GetPtr<FEnemyTurretSearchTaskInstanceData>();
		TestTrue(TEXT("Idle runs search and awareness before combat"), Search.Node.GetPtr<FEnemyTurretSearchTask>()
			&& Instance && Instance->bMaintainNonCombatSearch);
		TestTrue(TEXT("Idle search binds the owning turret"), HasTurretBinding(*Data, Actor->ID, TEXT(""), Search.ID, TEXT("Turret")));
	}
	TestTrue(TEXT("Idle retains Alert until awareness commits Combat"), HasCombatGate(*Data, Idle->EnterConditions, Sync, false));
	TestTrue(TEXT("Linked attack rejects an unconfirmed Alert target"), HasCombatGate(*Data, DirectCombat->EnterConditions, Sync, true));
	TestTrue(TEXT("Hacking is selected ahead of Battle"), Root->Children.IndexOfByKey(Hacking) < Root->Children.IndexOfByKey(Battle));
	TestTrue(TEXT("Hacking has an active episode gate"), HasHackGate(*Data, Hacking->EnterConditions, Sync, true));
	TestTrue(TEXT("Hacking cannot steal an actual Stun"), HasStunGate(*Data, Hacking->EnterConditions, Sync, false));
	TestTrue(TEXT("Battle cannot resume pending hacking"), HasHackGate(*Data, Battle->EnterConditions, Sync, false));
	TestTrue(TEXT("Battle cannot resume actual Stun"), HasStunGate(*Data, Battle->EnterConditions, Sync, false));
	TestTrue(TEXT("Stale stun events cannot reenter Stun"), HasStunGate(*Data, Stun->EnterConditions, Sync, true));
	TestEqual(TEXT("Only Hacking Task participates in this state"), Hacking->Tasks.Num(), 1);
	if (Hacking->Tasks.Num() == 1)
	{
		const auto* Task = Hacking->Tasks[0].Node.GetPtr<FEnemyTurretHackingTask>();
		TestTrue(TEXT("Hacking task is enabled and completes its state"), Task && Task->bTaskEnabled && Task->bConsideredForCompletion);
		TestTrue(TEXT("Hacking Task binds the owning turret"), HasTurretBinding(*Data, Actor->ID, TEXT(""), Hacking->Tasks[0].ID, TEXT("Turret")));
	}
	const auto CheckTransition = [&](UStateTreeState* Source, UStateTreeState* Target,
		EStateTreeTransitionTrigger Trigger, FGameplayTag Event, bool bHasHackGate, bool bHacking)
	{
		for (const auto& Transition : Source->Transitions)
			if (Transition.State.ID == Target->ID && Transition.Trigger == Trigger && Transition.RequiredEvent.Tag == Event)
			{
				if (bHasHackGate)
				{
					TestTrue(TEXT("Resume transition has hacking gate"), HasHackGate(*Data, Transition.Conditions, Sync, bHacking));
					TestTrue(TEXT("Resume transition rejects actual Stun"), HasStunGate(*Data, Transition.Conditions, Sync, false));
				}
				return;
			}
		AddError(FString::Printf(TEXT("Missing transition %s -> %s (%d, %s)"), *Source->Name.ToString(),
			*Target->Name.ToString(), int32(Trigger), *Event.ToString()));
	};
	for (UStateTreeState* Source : { Battle, Stun, Resume, Hidden, Deploy })
	{
		CheckTransition(Source, Hacking, EStateTreeTransitionTrigger::OnEvent, Started, true, true);
		CheckTransition(Source, Hacking, EStateTreeTransitionTrigger::OnTick, {}, true, true);
	}
	CheckTransition(Stun, Hacking, EStateTreeTransitionTrigger::OnEvent, StunEnded, true, true);
	CheckTransition(Stun, Battle, EStateTreeTransitionTrigger::OnEvent, StunEnded, true, false);
	CheckTransition(Stun, Battle, EStateTreeTransitionTrigger::OnTick, {}, true, false);
	CheckTransition(Hacking, Resume, EStateTreeTransitionTrigger::OnStateCompleted, {}, false, false);
	CheckTransition(Resume, Battle, EStateTreeTransitionTrigger::OnTick, {}, true, false);
	CheckTransition(Resume, Battle, EStateTreeTransitionTrigger::OnEvent,
		FGameplayTag::RequestGameplayTag(TEXT("Enemy.Event.Hacking.Ended")), true, false);
	if (!bSave)
	{
		TestTrue(TEXT("Saved common tree already contains executable runtime data"), Tree->IsReadyToRun());
		TestEqual(TEXT("Saved compile stamp matches authored data"), Tree->LastCompiledEditorDataHash,
			UStateTreeEditingSubsystem::CalculateStateTreeHash(Tree));
	}
	if (HasAnyErrors()) return false;
	// 공용 및 연결되는 전투 트리를 모두 컴파일한 뒤 공용 에셋 하나만 저장한다.
	for (UStateTree* Candidate : { Tree,
		LoadObject<UStateTree>(nullptr, TEXT("/Game/Blueprints/AI/Turret/ST_AutoTurretBattle_Burst.ST_AutoTurretBattle_Burst")),
		LoadObject<UStateTree>(nullptr, TEXT("/Game/Blueprints/AI/Turret/ST_AutoTurretBattle_Sniper.ST_AutoTurretBattle_Sniper")) })
	{
		if (!TestNotNull(TEXT("Linked turret tree exists"), Candidate)) return false;
		FStateTreeCompilerLog Log;
		FStateTreeCompiler Compiler(Log);
		if (!Compiler.Compile(*Candidate)) { Log.DumpToLog(LogTemp); AddError(TEXT("Turret tree compilation failed; not saved")); return false; }
		if (bSave && Candidate == Tree)
			Tree->LastCompiledEditorDataHash = UStateTreeEditingSubsystem::CalculateStateTreeHash(Tree);
	}
	if (bSave)
	{
		Tree->MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		const FString Filename = FPackageName::LongPackageNameToFilename(Tree->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		TestTrue(TEXT("Save only canonical common turret tree"), UPackage::SavePackage(Tree->GetOutermost(), Tree, *Filename, Args));
	}
	FString Audit;
	for (const auto& Node : Data->GlobalTasks) AuditTurretNode(Node, Audit);
	AuditTurretState(Root, Audit);
	for (const auto& Binding : Data->EditorBindings.GetBindings())
		Audit += FString::Printf(TEXT("Binding %s.%s -> %s.%s\n"),
			*Binding.GetSourcePath().GetStructID().ToString(), *Binding.GetSourcePath().ToString(),
			*Binding.GetTargetPath().GetStructID().ToString(), *Binding.GetTargetPath().ToString());
	const FString AuditPath = FPaths::ProjectSavedDir() / TEXT("TurretHackingStateAudit.txt");
	TestTrue(TEXT("Write audit into Saved"), FFileHelper::SaveStringToFile(Audit, *AuditPath));
	AddInfo(AuditPath);
	return !HasAnyErrors();
}

#endif
