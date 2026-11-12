#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Conditions/StateTreeCommonConditions.h"
#include "Enemy/EnemyStateTreeCombatMovementTasks.h"
#include "Enemy/EnemyStateTreeMovementTasks.h"
#include "Enemy/EnemyStateTreeTacticalTasks.h"
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
void CollectCloseRetreatStates(UStateTreeState* State, TArray<UStateTreeState*>& Out)
{
	if (!State) return;
	Out.Add(State);
	for (UStateTreeState* Child : State->Children) CollectCloseRetreatStates(Child, Out);
}

bool HasCloseRetreatBinding(const UStateTreeEditorData& Data, const FGuid Node, const TCHAR* Property)
{
	for (const auto& Binding : Data.EditorBindings.GetBindings())
		if (Binding.GetSourcePath().ToString() == Property
			&& Binding.GetTargetPath().GetStructID() == Node
			&& Binding.GetTargetPath().ToString() == TEXT("bLeft")) return true;
	return false;
}

void BindCloseRetreat(UStateTreeEditorData& Data, FGuid Source, const TCHAR* Property, FGuid Target, const TCHAR* Input)
{
	FPropertyBindingPath From, To;
	From.SetStructID(Source); From.FromString(Property);
	To.SetStructID(Target); To.FromString(Input);
	Data.AddPropertyBinding(From, To);
}

bool SaveCloseRetreatAsset(UObject* Asset)
{
	Asset->MarkPackageDirty();
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	Args.SaveFlags = SAVE_NoError;
	const FString Filename = FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
	return UPackage::SavePackage(Asset->GetOutermost(), Asset, *Filename, Args);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyCloseRetreatStateBindingsTest,
	"Outlier.Enemy.Combat.CloseRetreat.SavedStateBindings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyCloseRetreatStateBindingsTest::RunTest(const FString& Parameters)
{
	const bool bInstall = FParse::Param(FCommandLine::Get(), TEXT("OutlierInstallCloseRetreat"));
	UStateTree* Tree = LoadObject<UStateTree>(nullptr,
		TEXT("/Game/Blueprints/AI/VECDrone/StateTree/ST_VECDroneDirectCombat_Gun.ST_VECDroneDirectCombat_Gun"));
	UEnvQuery* Query = LoadObject<UEnvQuery>(nullptr,
		TEXT("/Game/Blueprints/AI/VECDrone/EQS/EQS_Drone_Retreat.EQS_Drone_Retreat"));
	if (!TestNotNull(TEXT("Gun tree exists"), Tree) || !TestNotNull(TEXT("Retreat query exists"), Query)) return false;
	UStateTreeEditorData* Data = Cast<UStateTreeEditorData>(Tree->EditorData);
	if (!TestNotNull(TEXT("Authoring data exists"), Data)) return false;
	if (bInstall && (!TestFalse(TEXT("Never save preexisting dirty tree"), Tree->GetOutermost()->IsDirty())
		|| !TestFalse(TEXT("Never save preexisting dirty query"), Query->GetOutermost()->IsDirty()))) return false;
	TArray<UStateTreeState*> States;
	for (UStateTreeState* Root : Data->SubTrees) CollectCloseRetreatStates(Root, States);
	FGuid Assessment;
	auto FindAssessment = [&](TArray<FStateTreeEditorNode>& Tasks)
	{
		for (auto& Task : Tasks)
			if (auto* Instance = Task.Instance.GetMutablePtr<FEnemyCombatAssessmentTaskInstanceData>())
			{
				Assessment = Task.ID;
				if (bInstall) { Instance->UpdateInterval = 0.2f; Instance->CloseRetreatEnterDistance = 300; Instance->CloseRetreatReleaseDistance = 500; }
				TestEqual(TEXT("Close retreat entry"), Instance->CloseRetreatEnterDistance, 300.0f);
				TestEqual(TEXT("Close retreat release"), Instance->CloseRetreatReleaseDistance, 500.0f);
				TestEqual(TEXT("Close retreat interval"), Instance->UpdateInterval, 0.2f);
			}
	};
	FindAssessment(Data->GlobalTasks);
	for (auto* State : States) FindAssessment(State->Tasks);
	if (!TestTrue(TEXT("Shared assessment exists"), Assessment.IsValid())) return false;
	UStateTreeState* Retreat = nullptr;
	UStateTreeState* Select = nullptr;
	for (auto* State : States)
	{
		if (State->Name == TEXT("RetreatFire")) Retreat = State;
		if (State->Name == TEXT("SelectRetreat")) Select = State;
	}
	if (!TestNotNull(TEXT("Retreat branch exists"), Retreat) || !TestNotNull(TEXT("Select retreat exists"), Select)) return false;
	UStateTreeState* Decision = Retreat->Parent;
	if (!TestNotNull(TEXT("Shared decision exists"), Decision)) return false;
	for (auto* State : States)
	{
		const bool bRetreat = State == Retreat;
		const bool bBranch = bRetreat || State->Name == TEXT("ApproachFire") || State->Name == TEXT("OrbitFire") || State->Name == TEXT("PrecisionFire");
		if (bBranch)
		{
			bool bGate = false;
			for (const auto& Condition : State->EnterConditions)
				bGate |= HasCloseRetreatBinding(*Data, Condition.ID, TEXT("bCloseRangeRetreatRequired"));
			if (bInstall && !bGate)
			{
				for (auto& Condition : State->EnterConditions) ++Condition.ExpressionIndent;
				auto& Gate = State->AddEnterCondition<FStateTreeCompareBoolCondition>();
				Gate.GetInstanceData().bRight = bRetreat;
				Gate.ExpressionOperand = bRetreat ? EStateTreeExpressionOperand::Or : EStateTreeExpressionOperand::And;
				BindCloseRetreat(*Data, Assessment, TEXT("bCloseRangeRetreatRequired"), Gate.ID, TEXT("bLeft"));
				bGate = true;
			}
			TestTrue(FString::Printf(TEXT("%s close-range entry gate"), *State->Name.ToString()), bGate);
			if (!bRetreat)
			{
				bool bWake = false;
				for (const auto& Transition : State->Transitions)
					for (const auto& Condition : Transition.Conditions)
						bWake |= Transition.State.ID == Decision->ID && HasCloseRetreatBinding(*Data, Condition.ID, TEXT("bCloseRangeRetreatRequired"));
				if (bInstall && !bWake)
				{
					auto& Transition = State->AddTransition(EStateTreeTransitionTrigger::OnTick, EStateTreeTransitionType::GotoState, Decision);
					Transition.Priority = EStateTreeTransitionPriority::High;
					auto& Condition = Transition.AddCondition<FStateTreeCompareBoolCondition>();
					Condition.GetInstanceData().bRight = true;
					BindCloseRetreat(*Data, Assessment, TEXT("bCloseRangeRetreatRequired"), Condition.ID, TEXT("bLeft"));
					bWake = true;
				}
				TestTrue(FString::Printf(TEXT("%s wakes during existing action"), *State->Name.ToString()), bWake);
			}
		}
		if (State == Retreat && bInstall)
			for (auto& Transition : State->Transitions)
			{
				bool bOutside = false, bCloseGate = false;
				for (const auto& Condition : Transition.Conditions)
				{
					bOutside |= HasCloseRetreatBinding(*Data, Condition.ID, TEXT("bTargetOutsideWeaponRange"));
					bCloseGate |= HasCloseRetreatBinding(*Data, Condition.ID, TEXT("bCloseRangeRetreatRequired"));
				}
				if (bOutside && !bCloseGate)
				{
					auto& Gate = Transition.AddCondition<FStateTreeCompareBoolCondition>();
					Gate.GetInstanceData().bRight = false;
					BindCloseRetreat(*Data, Assessment, TEXT("bCloseRangeRetreatRequired"), Gate.ID, TEXT("bLeft"));
				}
			}
		for (auto& Task : State->Tasks)
		{
			if (auto* Instance = Task.Instance.GetMutablePtr<FEnemyRunEnvQueryTaskInstanceData>(); Instance && Instance->bValidateRetreatPath)
			{
				if (bInstall) Instance->MaxPathCandidates = 18;
				TestTrue(TEXT("Same bounded request can examine long and short candidates"), Instance->MaxPathCandidates >= 18);
			}
			if (auto* Fly = Task.Instance.GetMutablePtr<FEnemyFlyToLocationTaskInstanceData>(); Fly && Fly->bConstrainRetreatPath)
			{
				if (bInstall)
				{
					Fly->bAllowShortRetreat = true;
					Fly->AcceptanceRadius = 25;
					// RetreatStart is captured by Fly EnterState, so no sibling-task binding is required.
				}
				TestTrue(TEXT("Short retreat is explicitly allowed"), Fly->bAllowShortRetreat);
				TestTrue(TEXT("Short retreat acceptance at most 25cm"), Fly->AcceptanceRadius <= 25);
			}
			if (Task.Node.GetScriptStruct() == FEnemyStationaryFireTask::StaticStruct() && State->Parent == Retreat)
			{
				bool bWake = false;
				for (const auto& Transition : State->Transitions)
					for (const auto& Condition : Transition.Conditions)
						bWake |= HasCloseRetreatBinding(*Data, Condition.ID, TEXT("bCloseRangeRetreatStarted"));
				if (bInstall && !bWake)
				{
					auto& Transition = State->AddTransition(EStateTreeTransitionTrigger::OnTick, EStateTreeTransitionType::GotoState, Select);
					Transition.Priority = EStateTreeTransitionPriority::High;
					auto& Condition = Transition.AddCondition<FStateTreeCompareBoolCondition>();
					Condition.GetInstanceData().bRight = true;
					BindCloseRetreat(*Data, Assessment, TEXT("bCloseRangeRetreatStarted"), Condition.ID, TEXT("bLeft"));
					bWake = true;
				}
				TestTrue(TEXT("Existing retreat hold reacts to new emergency only once"), bWake);
			}
		}
	}
	for (auto* Option : Query->GetOptions())
		for (UEnvQueryTest* Test : Option->Tests)
			if (auto* Distance = Cast<UEnvQueryTest_Distance>(Test))
			{
				if (bInstall) Distance->FloatValueMin.DefaultValue = 0;
				TestEqual(TEXT("Retreat distance filter permits short goals"), Distance->FloatValueMin.DefaultValue, 0.0f);
			}
	if (HasAnyErrors()) return false;
	if (bInstall)
	{
		FStateTreeCompilerLog Log;
		FStateTreeCompiler Compiler(Log);
		if (!Compiler.Compile(*Tree)) { Log.DumpToLog(LogTemp); AddError(TEXT("Close retreat tree compilation failed; not saved")); return false; }
		TestTrue(TEXT("Save only canonical retreat query"), SaveCloseRetreatAsset(Query));
		TestTrue(TEXT("Save only canonical Gun tree"), SaveCloseRetreatAsset(Tree));
	}
	return !HasAnyErrors();
}

#endif
