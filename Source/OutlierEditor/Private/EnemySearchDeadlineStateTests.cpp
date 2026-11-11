#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Conditions/StateTreeCommonConditions.h"
#include "Enemy/EnemyCombatTestHelpers.h"
#include "Enemy/EnemyStateTreeTacticalTasks.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
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
void CollectDeadlineStates(UStateTreeState* State, TArray<UStateTreeState*>& States)
{
	if (!State)
	{
		return;
	}
	States.Add(State);
	for (UStateTreeState* Child : State->Children)
	{
		CollectDeadlineStates(Child, States);
	}
}

bool IsDeadlineBinding(const UStateTreeEditorData& Data, const FGuid Session, const FGuid Condition)
{
	for (const auto& Binding : Data.EditorBindings.GetBindings())
	{
		if (Binding.GetSourcePath().GetStructID() == Session
			&& Binding.GetSourcePath().ToString() == TEXT("bLocalGuard")
			&& Binding.GetTargetPath().GetStructID() == Condition
			&& Binding.GetTargetPath().ToString() == TEXT("bLeft"))
		{
			return true;
		}
	}
	return false;
}

int32 CountDeadlineTransitions(const UStateTreeEditorData& Data, const UStateTreeState& State,
	const FGuid Session, const FGuid Guard)
{
	int32 Count = 0;
	for (const FStateTreeTransition& Transition : State.Transitions)
	{
		if (Transition.Trigger != EStateTreeTransitionTrigger::OnTick || Transition.State.ID != Guard
			|| Transition.Priority != EStateTreeTransitionPriority::Low || Transition.Conditions.Num() != 1)
		{
			continue;
		}
		const FStateTreeEditorNode& Condition = Transition.Conditions[0];
		const auto* Value = Condition.Instance.GetPtr<FStateTreeCompareBoolConditionInstanceData>();
		if (Value && Value->bRight && IsDeadlineBinding(Data, Session, Condition.ID))
		{
			++Count;
		}
	}
	return Count;
}

bool HasCompiledDeadlineTransition(const UStateTree& Tree, const FName From, const FName To)
{
	const FCompactStateTreeState* State = nullptr;
	for (const FCompactStateTreeState& Candidate : Tree.GetStates())
	{
		if (Candidate.Name == From)
		{
			State = &Candidate;
			break;
		}
	}
	if (!State)
	{
		return false;
	}
	for (int32 Index = 0; Index < State->TransitionsNum; ++Index)
	{
		const auto* Transition = Tree.GetTransitionFromIndex(FStateTreeIndex16(State->TransitionsBegin + Index));
		const auto* Target = Transition ? Tree.GetStateFromHandle(Transition->State) : nullptr;
		if (!Transition || !Transition->bTransitionEnabled || Transition->Trigger != EStateTreeTransitionTrigger::OnTick
			|| !Target || Target->Name != To || Transition->ConditionsNum != 1)
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
			if (Copy.SourceLeafProperty && Copy.SourceLeafProperty->GetFName() == TEXT("bLocalGuard")
				&& Copy.TargetLeafProperty && Copy.TargetLeafProperty->GetFName() == TEXT("bLeft"))
			{
				return true;
			}
		}
	}
	return false;
}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemySearchDeadlineStateBindingsTest,
	"Outlier.Enemy.Search.DeadlineStateBindings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemySearchDeadlineStateBindingsTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Type : { TEXT("Gun"), TEXT("Melee") })
	{
		Names.Add(Type);
		Commands.Add(Type);
	}
}

bool FEnemySearchDeadlineStateBindingsTest::RunTest(const FString& Parameters)
{
	const bool bInstall = FParse::Param(FCommandLine::Get(), TEXT("OutlierInstallSearchDeadlineTree"));
	const FString Name = TEXT("ST_VECDroneBattle_") + Parameters;
	const FString Path = TEXT("/Game/Blueprints/AI/VECDrone/StateTree/") + Name + TEXT(".") + Name;
	UStateTree* Tree = LoadObject<UStateTree>(nullptr, *Path);
	if (!TestNotNull(TEXT("Canonical battle tree loads"), Tree))
	{
		return false;
	}
	auto* Data = Cast<UStateTreeEditorData>(Tree->EditorData);
	if (!TestNotNull(TEXT("Battle authoring data exists"), Data)
		|| (bInstall && !TestFalse(TEXT("Migration preserves preexisting dirty packages"), Tree->GetOutermost()->IsDirty())))
	{
		return false;
	}
	TArray<UStateTreeState*> States;
	for (UStateTreeState* Root : Data->SubTrees)
	{
		CollectDeadlineStates(Root, States);
	}
	auto FindState = [&](const FName StateName) -> UStateTreeState*
	{
		auto** Found = States.FindByPredicate([&](const UStateTreeState* State) { return State->Name == StateName; });
		return Found ? *Found : nullptr;
	};
	UStateTreeState* Search = FindState(TEXT("Search"));
	UStateTreeState* Guard = FindState(TEXT("EnterLocalGuard"));
	if (!TestNotNull(TEXT("Search exists"), Search) || !TestNotNull(TEXT("LocalGuard entry exists"), Guard))
	{
		return false;
	}
	const FStateTreeEditorNode* Session = Search->Tasks.FindByPredicate([](const FStateTreeEditorNode& Node)
	{
		return Node.Node.GetScriptStruct() == FEnemySearchSessionTask::StaticStruct();
	});
	if (!TestNotNull(TEXT("Search owns its finite session"), Session))
	{
		return false;
	}
	bool bChanged = false;
	// 유한 수색 자식만 선점한다. 부모 전이는 GuardLook/Wait까지 매 Tick 재진입시키므로 사용하지 않는다.
	const FName FiniteSteps[] = { TEXT("LossWait"), TEXT("SelectAnchor"), TEXT("MoveAnchor"), TEXT("AnchorArrived"),
		TEXT("SelectPoints"), TEXT("SelectPoint"), TEXT("MovePoint"), TEXT("PointLookAround"), TEXT("PointChecked") };
	for (const FName StepName : FiniteSteps)
	{
		UStateTreeState* Step = FindState(StepName);
		if (!TestNotNull(StepName.ToString() + TEXT(" exists"), Step))
		{
			return false;
		}
		TestTrue(StepName.ToString() + TEXT(" belongs to Search"), Step->Parent == Search);
		if (bInstall && CountDeadlineTransitions(*Data, *Step, Session->ID, Guard->ID) == 0)
		{
			Tree->Modify();
			Data->Modify();
			Step->Modify();
			auto& Transition = Step->AddTransition(EStateTreeTransitionTrigger::OnTick,
				EStateTreeTransitionType::GotoState, Guard);
			// 직접 재발견/새 단서 부모 전이보다 낮게 둬 같은 평가의 새 의도를 먼저 처리한다.
			Transition.Priority = EStateTreeTransitionPriority::Low;
			auto& Condition = Transition.AddCondition<FStateTreeCompareBoolCondition>();
			Condition.GetInstanceData().bRight = true;
			FPropertyBindingPath From, To;
			From.SetStructID(Session->ID);
			From.FromString(TEXT("bLocalGuard"));
			To.SetStructID(Condition.ID);
			To.FromString(TEXT("bLeft"));
			Data->AddPropertyBinding(From, To);
			bChanged = true;
		}
		TestEqual(StepName.ToString() + TEXT(" has one deadline escape"),
			CountDeadlineTransitions(*Data, *Step, Session->ID, Guard->ID), 1);
	}
	for (const FName StepName : { FName(TEXT("Search")), FName(TEXT("EnterLocalGuard")),
		FName(TEXT("GuardLookAround")), FName(TEXT("GuardWait")) })
	{
		const UStateTreeState* Step = FindState(StepName);
		if (TestNotNull(StepName.ToString() + TEXT(" exists"), Step))
		{
			TestEqual(StepName.ToString() + TEXT(" cannot restart local guard each tick"),
				CountDeadlineTransitions(*Data, *Step, Session->ID, Guard->ID), 0);
		}
	}
	if (HasAnyErrors())
	{
		return false;
	}
	if (bChanged)
	{
		FStateTreeCompilerLog Log;
		FStateTreeCompiler Compiler(Log);
		if (!Compiler.Compile(*Tree))
		{
			Log.DumpToLog(LogTemp);
			AddError(TEXT("Deadline tree did not compile; no asset saved"));
			return false;
		}
		Tree->MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		const FString Filename = FPackageName::LongPackageNameToFilename(Tree->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		if (!TestTrue(TEXT("Save only canonical battle tree"), UPackage::SavePackage(Tree->GetOutermost(), Tree, *Filename, Args)))
		{
			return false;
		}
	}
	TestTrue(TEXT("Saved tree is compiled and ready"), Tree->IsReadyToRun());
	for (const FName StepName : FiniteSteps)
	{
		TestTrue(StepName.ToString() + TEXT(" compiled transition reads the session local guard output"),
			HasCompiledDeadlineTransition(*Tree, StepName, TEXT("EnterLocalGuard")));
	}
	return !HasAnyErrors();
}

#endif
