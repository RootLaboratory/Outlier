#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Conditions/StateTreeCommonConditions.h"
#include "Enemy/EnemyStateTreeCombatMovementTasks.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "EnvironmentQuery/Tests/EnvQueryTest_Distance.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "StateTree.h"
#include "StateTreeCompiler.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
constexpr TCHAR GunRangeTreePath[] = TEXT("/Game/Blueprints/AI/VECDrone/StateTree/ST_VECDroneDirectCombat_Gun.ST_VECDroneDirectCombat_Gun");
constexpr TCHAR RangeOutput[] = TEXT("bTargetOutsideWeaponRange");

void CollectRangeStates(UStateTreeState* State, TArray<UStateTreeState*>& States)
{
	if (!State) return;
	States.Add(State);
	for (UStateTreeState* Child : State->Children) CollectRangeStates(Child, States);
}

bool HasRangeBinding(const UStateTreeEditorData& Data, const FGuid Assessment, const FGuid Condition)
{
	for (const FStateTreePropertyPathBinding& Binding : Data.EditorBindings.GetBindings())
		if (Binding.GetSourcePath().GetStructID() == Assessment && Binding.GetSourcePath().ToString() == RangeOutput
			&& Binding.GetTargetPath().GetStructID() == Condition && Binding.GetTargetPath().ToString() == TEXT("bLeft")) return true;
	return false;
}

int32 CountRangeConditions(const UStateTreeEditorData& Data, const UStateTreeState& State, const FGuid Assessment,
	const bool bOutside)
{
	int32 Count = 0;
	for (const FStateTreeEditorNode& Condition : State.EnterConditions)
	{
		const auto* Instance = Condition.Instance.GetPtr<FStateTreeCompareBoolConditionInstanceData>();
		if (Instance && Instance->bRight == bOutside && HasRangeBinding(Data, Assessment, Condition.ID)) ++Count;
	}
	return Count;
}

void BindRange(UStateTreeEditorData& Data, const FGuid Assessment, const FGuid Condition)
{
	FPropertyBindingPath From, To;
	From.SetStructID(Assessment);
	From.FromString(RangeOutput);
	To.SetStructID(Condition);
	To.FromString(TEXT("bLeft"));
	Data.AddPropertyBinding(From, To);
}

void AddRangeCondition(UStateTreeEditorData& Data, UStateTreeState& State, const FGuid Assessment, const bool bOutside)
{
	// 기존 거리 식 전체를 묶는다. 접근은 사거리 밖 OR 기존 조건, 나머지는 사거리 안 AND 기존 조건이다.
	for (FStateTreeEditorNode& Existing : State.EnterConditions) ++Existing.ExpressionIndent;
	auto& Condition = State.AddEnterCondition<FStateTreeCompareBoolCondition>();
	Condition.GetInstanceData().bRight = bOutside;
	Condition.ExpressionOperand = bOutside ? EStateTreeExpressionOperand::Or : EStateTreeExpressionOperand::And;
	BindRange(Data, Assessment, Condition.ID);
}

int32 CountRangeTransitions(const UStateTreeEditorData& Data, const UStateTreeState& State, const FGuid Assessment,
	const UStateTreeState& Decision)
{
	int32 Count = 0;
	for (const FStateTreeTransition& Transition : State.Transitions)
	{
		if (Transition.Trigger != EStateTreeTransitionTrigger::OnTick
			|| Transition.State.ID != Decision.ID || Transition.Priority != EStateTreeTransitionPriority::High
			|| Transition.Conditions.Num() != 1) continue;
		const FStateTreeEditorNode& Condition = Transition.Conditions[0];
		const auto* Instance = Condition.Instance.GetPtr<FStateTreeCompareBoolConditionInstanceData>();
		if (Instance && Instance->bRight && HasRangeBinding(Data, Assessment, Condition.ID)) ++Count;
	}
	return Count;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyCombatRangeStateBindingsTest,
	"Outlier.Enemy.Combat.RangeStateBindings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyCombatRangeStateBindingsTest::RunTest(const FString& Parameters)
{
	const bool bUpdate = FParse::Param(FCommandLine::Get(), TEXT("OutlierUpdateGunRangeTree"));
	UEnvQuery* ApproachQuery = LoadObject<UEnvQuery>(nullptr,
		TEXT("/Game/Blueprints/AI/VECDrone/EQS/EQS_Drone_Approach.EQS_Drone_Approach"));
	if (!TestNotNull(TEXT("Canonical approach query exists"), ApproachQuery)) return false;
	int32 DistanceTests = 0;
	bool bQueryChanged = false;
	if (bUpdate && !TestFalse(TEXT("Approach migration never saves a previously dirty package"),
		ApproachQuery->GetOutermost()->IsDirty())) return false;
	for (UEnvQueryOption* Option : ApproachQuery->GetOptions())
		for (UEnvQueryTest* Test : Option->Tests)
			if (UEnvQueryTest_Distance* Distance = Cast<UEnvQueryTest_Distance>(Test))
			{
				++DistanceTests;
				// 후보 생성기가 실제 무기 사거리로 거리를 제한한다. 기존 10m 필터가 짧은 무기의 모든 후보를 버리지 않게 한다.
				if (bUpdate && Distance->FloatValueMin.DefaultValue != 0.0f)
				{
					ApproachQuery->Modify();
					Distance->Modify();
					Distance->FloatValueMin.DefaultValue = 0.0f;
					bQueryChanged = true;
				}
				TestEqual(TEXT("Approach filter permits range-aware candidates below ten metres"), Distance->FloatValueMin.DefaultValue, 0.0f);
			}
	TestEqual(TEXT("Approach query retains one distance test"), DistanceTests, 1);
	if (HasAnyErrors()) return false;
	if (bQueryChanged)
	{
		ApproachQuery->MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		const FString Filename = FPackageName::LongPackageNameToFilename(ApproachQuery->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		if (!TestTrue(TEXT("Save only canonical approach query"), UPackage::SavePackage(ApproachQuery->GetOutermost(), ApproachQuery, *Filename, Args))) return false;
	}
	UStateTree* Tree = LoadObject<UStateTree>(nullptr, GunRangeTreePath);
	if (!TestNotNull(TEXT("Canonical linked Gun tree exists"), Tree)) return false;
	UStateTreeEditorData* Data = Cast<UStateTreeEditorData>(Tree->EditorData);
	if (!TestNotNull(TEXT("Gun tree has authoring data"), Data)) return false;
	if (bUpdate && !TestFalse(TEXT("Migration never saves a previously dirty package"), Tree->GetOutermost()->IsDirty())) return false;

	TArray<UStateTreeState*> States;
	for (UStateTreeState* Root : Data->SubTrees) CollectRangeStates(Root, States);
	FGuid Assessment;
	int32 AssessmentCount = 0;
	for (const FStateTreeEditorNode& Task : Data->GlobalTasks)
		if (Task.Node.GetScriptStruct() == FEnemyCombatAssessmentTask::StaticStruct())
		{
			Assessment = Task.ID;
			++AssessmentCount;
		}
	for (UStateTreeState* State : States)
		for (const FStateTreeEditorNode& Task : State->Tasks)
			if (Task.Node.GetScriptStruct() == FEnemyCombatAssessmentTask::StaticStruct())
			{
				Assessment = Task.ID;
				++AssessmentCount;
			}
	if (!TestEqual(TEXT("Gun tree has one combat assessment"), AssessmentCount, 1)) return false;

	const FName BranchNames[] = {TEXT("ApproachFire"), TEXT("OrbitFire"), TEXT("RetreatFire"), TEXT("PrecisionFire")};
	TArray<UStateTreeState*> Branches;
	for (const FName Name : BranchNames)
	{
		UStateTreeState** Found = States.FindByPredicate([Name](const UStateTreeState* State) { return State->Name == Name; });
		if (!TestTrue(FString::Printf(TEXT("Gun movement branch %s exists"), *Name.ToString()), Found != nullptr)) return false;
		Branches.Add(*Found);
	}
	if (!TestNotNull(TEXT("Movement decision parent exists"), Branches[0]->Parent.Get())) return false;
	UStateTreeState* Decision = Branches[0]->Parent;
	for (const UStateTreeState* Branch : Branches)
	{
		const UStateTreeState* Ancestor = Branch->Parent;
		while (Ancestor && Ancestor != Decision) Ancestor = Ancestor->Parent;
		if (!TestTrue(TEXT("Movement branches belong to the common decision"), Ancestor == Decision)) return false;
	}

	bool bChanged = false;
	for (int32 Index = 0; Index < Branches.Num(); ++Index)
	{
		UStateTreeState& Branch = *Branches[Index];
		const bool bOutside = Index == 0;
		AddInfo(FString::Printf(TEXT("[GunRange] Branch=%s ExistingConditions=%d Transitions=%d"),
			*Branch.Name.ToString(), Branch.EnterConditions.Num(), Branch.Transitions.Num()));
		if (bUpdate && CountRangeConditions(*Data, Branch, Assessment, bOutside) == 0)
		{
			Tree->Modify();
			Data->Modify();
			Branch.Modify();
			AddRangeCondition(*Data, Branch, Assessment, bOutside);
			bChanged = true;
		}
		TestEqual(FString::Printf(TEXT("%s has exactly one %s range gate"), *Branch.Name.ToString(), bOutside ? TEXT("outside") : TEXT("inside")),
			CountRangeConditions(*Data, Branch, Assessment, bOutside), 1);
	}
	for (int32 Index = 1; Index < Branches.Num(); ++Index)
	{
		UStateTreeState& Branch = *Branches[Index];
		if (bUpdate && CountRangeTransitions(*Data, Branch, Assessment, *Decision) == 0)
		{
			Tree->Modify();
			Data->Modify();
			Branch.Modify();
			// 자식 재시도는 부모 진입 조건을 다시 검사하지 않는다. 사거리 이탈은 공통 Decision에 직접 인계한다.
			auto& Transition = Branch.AddTransition(EStateTreeTransitionTrigger::OnTick,
				EStateTreeTransitionType::GotoState, Decision);
			Transition.Priority = EStateTreeTransitionPriority::High;
			auto& Condition = Transition.AddCondition<FStateTreeCompareBoolCondition>();
			Condition.GetInstanceData().bRight = true;
			BindRange(*Data, Assessment, Condition.ID);
			bChanged = true;
		}
		TestEqual(FString::Printf(TEXT("%s immediately reselects decision outside range"), *Branch.Name.ToString()),
			CountRangeTransitions(*Data, Branch, Assessment, *Decision), 1);
	}
	// 20m 진입만으로 접근을 끝내면 짧은 무기는 이동/공격 준비를 매번 다시 시작한다.
	// 목적지 도착·실패·타깃 변경은 기존 자식 상태와 Guard가 처리하고, 사격은 이동과 병행한다.
	if (bUpdate)
	{
		for (int32 Index = Branches[0]->Transitions.Num() - 1; Index >= 0; --Index)
		{
			const FStateTreeTransition& Transition = Branches[0]->Transitions[Index];
			if (Transition.Trigger != EStateTreeTransitionTrigger::OnTick || Transition.State.ID != Decision->ID) continue;
			Tree->Modify();
			Data->Modify();
			Branches[0]->Modify();
			Branches[0]->Transitions.RemoveAt(Index);
			bChanged = true;
		}
	}
	for (const FStateTreeTransition& Transition : Branches[0]->Transitions)
		TestFalse(TEXT("Approach is not canceled solely by entering the tactical distance band"),
			Transition.Trigger == EStateTreeTransitionTrigger::OnTick && Transition.State.ID == Decision->ID);
	if (HasAnyErrors()) return false;
	if (bChanged)
	{
		FStateTreeCompilerLog Log;
		FStateTreeCompiler Compiler(Log);
		if (!Compiler.Compile(*Tree))
		{
			Log.DumpToLog(LogTemp);
			AddError(TEXT("Gun range tree compilation failed; no asset was saved"));
			return false;
		}
		Tree->MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		const FString Filename = FPackageName::LongPackageNameToFilename(Tree->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		if (!TestTrue(TEXT("Save only canonical linked Gun tree"), UPackage::SavePackage(Tree->GetOutermost(), Tree, *Filename, Args))) return false;
	}
	TestTrue(TEXT("Gun range tree compiled data is ready"), Tree->IsReadyToRun());
	return !HasAnyErrors();
}

#endif
