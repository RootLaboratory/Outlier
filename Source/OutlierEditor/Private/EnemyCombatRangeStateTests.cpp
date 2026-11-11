#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Conditions/StateTreeCommonConditions.h"
#include "Enemy/EnemyStateTreeCombatMovementTasks.h"
#include "Enemy/EnemyEnvQuery.h"
#include "Engine/Engine.h"
#include "EdGraph/EdGraphNode.h"
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
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

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
			// Slice 4의 사선 해제 인계는 거리 구간 진입 취소와 다르다. 기존 거리 전이만 제거한다.
			bool bSightResolved = false;
			for (const auto& Condition : Transition.Conditions)
				for (const auto& Binding : Data->EditorBindings.GetBindings())
					bSightResolved |= Binding.GetSourcePath().ToString() == TEXT("bRepositionResolved")
						&& Binding.GetTargetPath().GetStructID() == Condition.ID;
			if (bSightResolved) continue;
			Tree->Modify();
			Data->Modify();
			Branches[0]->Modify();
			Branches[0]->Transitions.RemoveAt(Index);
			bChanged = true;
		}
	}
	for (const FStateTreeTransition& Transition : Branches[0]->Transitions)
	{
		if (Transition.Trigger != EStateTreeTransitionTrigger::OnTick || Transition.State.ID != Decision->ID) continue;
		bool bSightResolved = false;
		for (const auto& Condition : Transition.Conditions)
			for (const auto& Binding : Data->EditorBindings.GetBindings())
				bSightResolved |= Binding.GetSourcePath().ToString() == TEXT("bRepositionResolved")
					&& Binding.GetTargetPath().GetStructID() == Condition.ID;
		TestTrue(TEXT("Approach reselects only for reopened sight, not tactical distance alone"), bSightResolved);
	}
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyExposedAimQueryBindingsTest,
	"Outlier.Enemy.Combat.ExposedAim.SavedQueryBindings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyExposedAimQueryBindingsTest::RunTest(const FString& Parameters)
{
	const bool bInstall = FParse::Param(FCommandLine::Get(), TEXT("OutlierInstallExposedAimQueries"));
	for (const TCHAR* Name : { TEXT("Approach"), TEXT("Retreat") })
	{
		const FString Path = FString::Printf(TEXT("/Game/Blueprints/AI/VECDrone/EQS/EQS_Drone_%s.EQS_Drone_%s"), Name, Name);
		UEnvQuery* Query = LoadObject<UEnvQuery>(nullptr, *Path);
		if (!TestNotNull(TEXT("Canonical combat query loads"), Query)) return false;
		bool bChanged = false;
		int32 SightTests = 0;
		UEnemyEnvQueryTest_CombatSight* CombatSight = nullptr;
		if (bInstall && !TestFalse(TEXT("Migration does not overwrite a dirty query"), Query->GetOutermost()->IsDirty())) return false;
		for (UEnvQueryOption* Option : Query->GetOptions())
		{
			for (TObjectPtr<UEnvQueryTest>& Test : Option->Tests)
			{
				if (!Cast<UEnvQueryTest_Trace>(Test)) continue;
				++SightTests;
				if (bInstall && !Cast<UEnemyEnvQueryTest_CombatSight>(Test))
				{
					Query->Modify();
					Option->Modify();
					auto* Replacement = NewObject<UEnemyEnvQueryTest_CombatSight>(Option);
					// 기존 채널/컨텍스트/필터 설정을 보존한다. Shooter만 Task 사선 검사로 인계한다.
					UEngine::CopyPropertiesForUnrelatedObjects(Test, Replacement);
					Test = Replacement;
					bChanged = true;
				}
				TestNotNull(TEXT("Central trace cannot reject exposed Shooter candidates before path validation"),
					Cast<UEnemyEnvQueryTest_CombatSight>(Test));
				CombatSight = Cast<UEnemyEnvQueryTest_CombatSight>(Test);
			}
		}
		// Approach의 기존 중앙 Trace만 교체한다. Retreat는 완료 callback에서 사선을 검사한다.
		TestEqual(TEXT("Saved query retains its original sight filter count"), SightTests,
			FCString::Strcmp(Name, TEXT("Approach")) == 0 ? 1 : 0);
		if (SightTests > 1 || (SightTests == 1 && !CombatSight)) return false;
		// EQS 편집기는 그래프의 NodeInstance로 런타임 Tests를 다시 만든다. 이미 Tests만
		// 교체한 에셋도 복구해야 다음 편집/저장 때 중앙 Trace로 되돌아가지 않는다.
		TArray<UObject*> Objects;
		GetObjectsWithOuter(Query, Objects, true);
		int32 GraphSightTests = 0;
		for (UObject* Object : Objects)
		{
			UEdGraphNode* Node = Cast<UEdGraphNode>(Object);
			FObjectPropertyBase* InstanceProperty = Node
				? FindFProperty<FObjectPropertyBase>(Node->GetClass(), TEXT("NodeInstance")) : nullptr;
			if (!InstanceProperty || !Cast<UEnvQueryTest_Trace>(InstanceProperty->GetObjectPropertyValue_InContainer(Node))) continue;
			const FBoolProperty* Enabled = FindFProperty<FBoolProperty>(Node->GetClass(), TEXT("bTestEnabled"));
			if (Enabled && !Enabled->GetPropertyValue_InContainer(Node)) continue;
			++GraphSightTests;
			if (!CombatSight) continue;
			if (bInstall && InstanceProperty->GetObjectPropertyValue_InContainer(Node) != CombatSight)
			{
				Node->Modify();
				InstanceProperty->SetObjectPropertyValue_InContainer(Node, CombatSight);
				// 편집기 모듈 의존성을 추가하지 않고 저장된 클래스 정보도 함께 갱신한다.
				FStructProperty* ClassData = FindFProperty<FStructProperty>(Node->GetClass(), TEXT("ClassData"));
				FStrProperty* ClassName = ClassData
					? FindFProperty<FStrProperty>(ClassData->Struct, TEXT("ClassName")) : nullptr;
				if (!TestNotNull(TEXT("Graph class metadata is available"), ClassName)) return false;
				void* Data = ClassData->ContainerPtrToValuePtr<void>(Node);
				ClassData->Struct->DestroyStruct(Data);
				ClassData->Struct->InitializeStruct(Data);
				ClassName->SetPropertyValue_InContainer(Data, CombatSight->GetClass()->GetName());
				bChanged = true;
			}
			TestTrue(TEXT("Editor graph rebuild preserves the same combat sight instance"),
				InstanceProperty->GetObjectPropertyValue_InContainer(Node) == CombatSight);
		}
		TestEqual(TEXT("Editor graph and runtime retain the same sight filter count"), GraphSightTests, SightTests);
		if (bChanged)
		{
			Query->MarkPackageDirty();
			FSavePackageArgs Args;
			Args.TopLevelFlags = RF_Public | RF_Standalone;
			Args.SaveFlags = SAVE_NoError;
			const FString Filename = FPackageName::LongPackageNameToFilename(Query->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
			if (!TestTrue(TEXT("Save only the canonical combat query"), UPackage::SavePackage(Query->GetOutermost(), Query, *Filename, Args))) return false;
		}
	}
	return !HasAnyErrors();
}

#endif
