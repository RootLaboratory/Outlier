#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Conditions/StateTreeCommonConditions.h"
#include "Enemy/EnemyStateTreeCombatMovementTasks.h"
#include "Enemy/EnemyStateTreeMovementTasks.h"
#include "Enemy/EnemyStateTreeTacticalTasks.h"
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
void CollectTacticalStates(UStateTreeState* State, TArray<UStateTreeState*>& States)
{
	if (!State) return;
	States.Add(State);
	for (UStateTreeState* Child : State->Children) CollectTacticalStates(Child, States);
}

bool IsRepositionBinding(const UStateTreeEditorData& Data, const FGuid Assessment, const FGuid Condition,
	const TCHAR* SourceProperty = TEXT("bNeedsReposition"))
{
	for (const auto& Binding : Data.EditorBindings.GetBindings())
		if (Binding.GetSourcePath().GetStructID() == Assessment
			&& Binding.GetSourcePath().ToString() == SourceProperty
			&& Binding.GetTargetPath().GetStructID() == Condition
			&& Binding.GetTargetPath().ToString() == TEXT("bLeft")) return true;
	return false;
}

void BindReposition(UStateTreeEditorData& Data, const FGuid Assessment, const FGuid Condition,
	const TCHAR* SourceProperty = TEXT("bNeedsReposition"))
{
	FPropertyBindingPath From, To;
	From.SetStructID(Assessment);
	From.FromString(SourceProperty);
	To.SetStructID(Condition);
	To.FromString(TEXT("bLeft"));
	Data.AddPropertyBinding(From, To);
}

int32 CountRepositionGates(const UStateTreeEditorData& Data, const UStateTreeState& State,
	const FGuid Assessment, const bool bRequired)
{
	int32 Count = 0;
	for (const FStateTreeEditorNode& Condition : State.EnterConditions)
	{
		const auto* Value = Condition.Instance.GetPtr<FStateTreeCompareBoolConditionInstanceData>();
		if (Value && Value->bRight == bRequired && IsRepositionBinding(Data, Assessment, Condition.ID)) ++Count;
	}
	return Count;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyCombatTacticalStateBindingsTest,
	"Outlier.Enemy.Combat.Tactical.SavedGunStateBindings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyCombatTacticalStateBindingsTest::RunTest(const FString& Parameters)
{
	const bool bInstall = FParse::Param(FCommandLine::Get(), TEXT("OutlierInstallCombatTacticalTree"));
	UStateTree* Tree = LoadObject<UStateTree>(nullptr,
		TEXT("/Game/Blueprints/AI/VECDrone/StateTree/ST_VECDroneDirectCombat_Gun.ST_VECDroneDirectCombat_Gun"));
	if (!TestNotNull(TEXT("Canonical Gun tree loads"), Tree)) return false;
	auto* Data = Cast<UStateTreeEditorData>(Tree->EditorData);
	if (!TestNotNull(TEXT("Gun authoring data exists"), Data)) return false;
	if (bInstall && !TestFalse(TEXT("Migration does not save an already dirty package"), Tree->GetOutermost()->IsDirty())) return false;
	TArray<UStateTreeState*> States;
	for (UStateTreeState* Root : Data->SubTrees) CollectTacticalStates(Root, States);
	int32 FlightTasks = 0;
	for (const UStateTreeState* State : States)
	{
		for (const FStateTreeEditorNode& Task : State->Tasks)
		{
			const auto* Fly = Task.Node.GetPtr<FEnemyFlyToLocationTask>();
			if (!Fly) continue;
			++FlightTasks;
			TestFalse(TEXT("Saved Move reselection preserves its current route and speed clock"), Fly->bShouldStateChangeOnReselect);
			bool bQueryDestination = false;
			for (const auto& Binding : Data->EditorBindings.GetBindings())
			{
				if (Binding.IsOutputBinding() || Binding.GetTargetPath().GetStructID() != Task.ID
					|| Binding.GetTargetPath().ToString() != TEXT("Destination")) continue;
				for (const UStateTreeState* SourceState : States)
					for (const FStateTreeEditorNode& Source : SourceState->Tasks)
					{
						if (Source.Node.GetScriptStruct() != FEnemyRunEnvQueryTask::StaticStruct()) continue;
						bQueryDestination |= Source.ID == Binding.GetSourcePath().GetStructID()
							&& Binding.GetSourcePath().ToString() == TEXT("Destination");
						// 출력 Binding은 Parameter가 Source, Query가 Target으로 저장되며 값 복사는 반대로 수행된다.
						for (const auto& Publication : Data->EditorBindings.GetBindings())
							bQueryDestination |= Publication.IsOutputBinding()
								&& Publication.GetTargetPath().GetStructID() == Source.ID
								&& Publication.GetTargetPath().ToString() == TEXT("Destination")
								&& Publication.GetSourcePath().GetStructID() == Binding.GetSourcePath().GetStructID()
								&& Publication.GetSourcePath().ToString() == Binding.GetSourcePath().ToString();
					}
			}
			TestTrue(TEXT("Saved Select publishes the replacement destination to Move"), bQueryDestination);
		}
	}
	TestTrue(TEXT("Saved Gun tree contains tactical flight tasks"), FlightTasks > 0);
	FGuid Assessment;
	int32 AssessmentCount = 0;
	auto FindAssessment = [&](const TArray<FStateTreeEditorNode>& Tasks)
	{
		for (const FStateTreeEditorNode& Task : Tasks)
			if (Task.Node.GetScriptStruct() == FEnemyCombatAssessmentTask::StaticStruct())
			{
				Assessment = Task.ID;
				++AssessmentCount;
			}
	};
	FindAssessment(Data->GlobalTasks);
	for (UStateTreeState* State : States) FindAssessment(State->Tasks);
	if (!TestEqual(TEXT("One shared assessment drives the Gun tree"), AssessmentCount, 1)) return false;
	const FName Names[] = { TEXT("ApproachFire"), TEXT("OrbitFire"), TEXT("RetreatFire"), TEXT("PrecisionFire") };
	TArray<UStateTreeState*> Branches;
	for (const FName Name : Names)
	{
		auto** Found = States.FindByPredicate([Name](const UStateTreeState* State) { return State->Name == Name; });
		if (!TestTrue(FString::Printf(TEXT("Branch %s exists"), *Name.ToString()), Found != nullptr)) return false;
		Branches.Add(*Found);
	}
	UStateTreeState* Decision = Branches[0]->Parent;
	if (!TestNotNull(TEXT("Common movement decision exists"), Decision)) return false;
	bool bChanged = false;
	for (int32 Index = 0; Index < Branches.Num(); ++Index)
	{
		UStateTreeState& Branch = *Branches[Index];
		const bool bRequired = Index == 0;
		if (bInstall && CountRepositionGates(*Data, Branch, Assessment, bRequired) == 0)
		{
			Tree->Modify(); Data->Modify(); Branch.Modify();
			// 기존 거리 식을 한 그룹으로 유지한다. 차폐 재배치는 사거리 안에서도 접근을 선택한다.
			for (auto& Existing : Branch.EnterConditions) ++Existing.ExpressionIndent;
			auto& Condition = Branch.AddEnterCondition<FStateTreeCompareBoolCondition>();
			Condition.GetInstanceData().bRight = bRequired;
			Condition.ExpressionOperand = bRequired ? EStateTreeExpressionOperand::Or : EStateTreeExpressionOperand::And;
			BindReposition(*Data, Assessment, Condition.ID);
			bChanged = true;
		}
		TestEqual(FString::Printf(TEXT("%s has exactly one reposition gate"), *Branch.Name.ToString()),
			CountRepositionGates(*Data, Branch, Assessment, bRequired), 1);
	}
	UStateTreeState& Precision = *Branches[3];
	auto CountWakeTransitions = [&]()
	{
		int32 Count = 0;
		for (const auto& Transition : Precision.Transitions)
			if (Transition.Trigger == EStateTreeTransitionTrigger::OnTick && Transition.State.ID == Decision->ID
				&& Transition.Priority == EStateTreeTransitionPriority::High && Transition.Conditions.Num() == 1)
			{
				const auto& Condition = Transition.Conditions[0];
				const auto* Value = Condition.Instance.GetPtr<FStateTreeCompareBoolConditionInstanceData>();
				if (Value && Value->bRight && IsRepositionBinding(*Data, Assessment, Condition.ID)) ++Count;
			}
		return Count;
	};
	if (bInstall && CountWakeTransitions() == 0)
	{
		Tree->Modify(); Data->Modify(); Precision.Modify();
		// 정지 사격 자식의 완료를 기다리지 않는다. 지속 차폐만 공통 이동 판단으로 인계한다.
		auto& Transition = Precision.AddTransition(EStateTreeTransitionTrigger::OnTick,
			EStateTreeTransitionType::GotoState, Decision);
		Transition.Priority = EStateTreeTransitionPriority::High;
		auto& Condition = Transition.AddCondition<FStateTreeCompareBoolCondition>();
		Condition.GetInstanceData().bRight = true;
		BindReposition(*Data, Assessment, Condition.ID);
		bChanged = true;
	}
	TestEqual(TEXT("Stationary fire wakes movement decision after sustained obstruction"), CountWakeTransitions(), 1);
	UStateTreeState& Approach = *Branches[0];
	auto CountResolvedTransitions = [&]()
	{
		int32 Count = 0;
		for (const auto& Transition : Approach.Transitions)
			if (Transition.Trigger == EStateTreeTransitionTrigger::OnTick && Transition.State.ID == Decision->ID
				&& Transition.Priority == EStateTreeTransitionPriority::High && Transition.Conditions.Num() == 1)
			{
				const auto& Condition = Transition.Conditions[0];
				const auto* Value = Condition.Instance.GetPtr<FStateTreeCompareBoolConditionInstanceData>();
				if (Value && Value->bRight && IsRepositionBinding(*Data, Assessment, Condition.ID, TEXT("bRepositionResolved"))) ++Count;
			}
		return Count;
	};
	if (bInstall && CountResolvedTransitions() == 0)
	{
		Tree->Modify(); Data->Modify(); Approach.Modify();
		// 사선이 다시 열리면 진행 중인 차폐 재배치를 이탈한다. 공격 준비/회복은 Pawn이 이어 간다.
		auto& Transition = Approach.AddTransition(EStateTreeTransitionTrigger::OnTick,
			EStateTreeTransitionType::GotoState, Decision);
		Transition.Priority = EStateTreeTransitionPriority::High;
		auto& Condition = Transition.AddCondition<FStateTreeCompareBoolCondition>();
		Condition.GetInstanceData().bRight = true;
		BindReposition(*Data, Assessment, Condition.ID, TEXT("bRepositionResolved"));
		bChanged = true;
	}
	TestEqual(TEXT("Reopened shot path cancels the sight-driven approach"), CountResolvedTransitions(), 1);
	if (HasAnyErrors()) return false;
	if (bChanged)
	{
		FStateTreeCompilerLog Log;
		FStateTreeCompiler Compiler(Log);
		if (!Compiler.Compile(*Tree))
		{
			Log.DumpToLog(LogTemp);
			AddError(TEXT("Tactical Gun tree did not compile; asset was not saved"));
			return false;
		}
		Tree->MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		const FString Filename = FPackageName::LongPackageNameToFilename(Tree->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		TestTrue(TEXT("Save only canonical Gun tree"), UPackage::SavePackage(Tree->GetOutermost(), Tree, *Filename, Args));
	}
	TestTrue(TEXT("Saved tactical Gun tree is ready"), Tree->IsReadyToRun());
	return !HasAnyErrors();
}

#endif
