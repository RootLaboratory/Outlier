#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Enemy/EnemyEnvQuery.h"
#include "Enemy/EnemyStateTreeMovementTasks.h"
#include "Enemy/EnemyStateTreeTacticalTasks.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"
#include "StateTree.h"
#include "StateTreeInstanceData.h"
#include "StateTreePropertyBindings.h"
#include "UObject/UnrealType.h"

namespace
{
// 에셋을 수정하거나 테스트용 복사본으로 대체하지 않는다. 저장/컴파일된 실제 구성을 검사한다.
UObject* LoadSearchAsset(FAutomationTestBase& Test, FName Name, UClass* Class)
{
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	Registry.ScanPathsSynchronous({TEXT("/Game/Blueprints/AI/VECDrone")});
	TArray<FAssetData> Assets;
	Registry.GetAssetsByClass(Class->GetClassPathName(), Assets, true);
	TArray<FAssetData> Matches;
	const FString Folder = Class == UStateTree::StaticClass()
		? TEXT("/Game/Blueprints/AI/VECDrone/StateTree") : TEXT("/Game/Blueprints/AI/VECDrone/EQS");
	for (const FAssetData& Asset : Assets)
	{
		if (Asset.AssetName == Name && Asset.PackagePath.ToString() == Folder)
			Matches.Add(Asset);
	}
	if (!Test.TestEqual(FString::Printf(TEXT("Asset %s exists exactly once"), *Name.ToString()), Matches.Num(), 1)) return nullptr;
	Test.AddInfo(FString::Printf(TEXT("[SearchAsset] %s"), *Matches[0].GetSoftObjectPath().ToString()));
	return Matches[0].GetAsset();
}

const FCompactStateTreeState* FindSearchState(FAutomationTestBase& Test, const UStateTree& Tree, FName Name)
{
	const FCompactStateTreeState* Found = nullptr;
	for (const FCompactStateTreeState& State : Tree.GetStates())
	{
		if (State.Name != Name) continue;
		if (Found)
		{
			Test.AddError(FString::Printf(TEXT("State %s is ambiguous"), *Name.ToString()));
			return nullptr;
		}
		Found = &State;
	}
	Test.TestNotNull(FString::Printf(TEXT("State %s exists"), *Name.ToString()), Found);
	return Found;
}

template<typename TaskType>
const TaskType* FindSearchTask(FAutomationTestBase& Test, const UStateTree& Tree, const FCompactStateTreeState* State)
{
	if (!State) return nullptr;
	const TaskType* Found = nullptr;
	for (int32 Index = 0; Index < State->TasksNum; ++Index)
	{
		const TaskType* Task = Tree.GetNode(State->TasksBegin + Index).GetPtr<const TaskType>();
		if (Task && Task->bTaskEnabled)
		{
			if (Found)
			{
				Test.AddError(FString::Printf(TEXT("%s has duplicate enabled tasks %s"), *State->Name.ToString(), *TaskType::StaticStruct()->GetName()));
				return nullptr;
			}
			Found = Task;
		}
	}
	Test.TestNotNull(FString::Printf(TEXT("%s has enabled %s"), *State->Name.ToString(), *TaskType::StaticStruct()->GetName()), Found);
	return Found;
}

template<typename TaskType>
const typename TaskType::FInstanceDataType* GetSearchTaskData(const UStateTree& Tree, const TaskType* Task)
{
	if (!Task || !Task->InstanceTemplateIndex.IsValid()
		|| !Tree.GetDefaultInstanceData().IsValidIndex(Task->InstanceTemplateIndex.Get())) return nullptr;
	using FInstanceData = typename TaskType::FInstanceDataType;
	return Tree.GetDefaultInstanceData().GetStruct(Task->InstanceTemplateIndex.Get()).template GetPtr<const FInstanceData>();
}

void CheckSearchTransition(FAutomationTestBase& Test, const UStateTree& Tree,
	const FCompactStateTreeState* From, FName Target, EStateTreeTransitionTrigger Trigger)
{
	if (!From) return;
	bool bFound = false;
	for (int32 Index = 0; Index < From->TransitionsNum; ++Index)
	{
		const FCompactStateTransition* Transition = Tree.GetTransitionFromIndex(FStateTreeIndex16(From->TransitionsBegin + Index));
		const FCompactStateTreeState* To = Transition ? Tree.GetStateFromHandle(Transition->State) : nullptr;
		if (Transition && Transition->bTransitionEnabled && EnumHasAnyFlags(Transition->Trigger, Trigger) && To && To->Name == Target)
			bFound = true;
	}
	Test.TestTrue(FString::Printf(TEXT("%s -> %s trigger %d"), *From->Name.ToString(), *Target.ToString(), int32(Trigger)), bFound);
}

const FPropertyBindingCopyInfo* FindDestinationCopy(const UStateTree& Tree, const FStateTreeNodeBase* Task, bool bOutput)
{
	if (!Task) return nullptr;
	const FStateTreeIndex16 Index = bOutput ? Task->OutputBindingsBatch : Task->BindingsBatch;
	const FPropertyBindingBindingCollection& Bindings = Tree.GetPropertyBindings();
	if (!Index.IsValid() || !Bindings.GetCopyBatches().IsValidIndex(Index.Get())) return nullptr;
	for (const FPropertyBindingCopyInfo& Copy : Bindings.GetBatchCopies(Bindings.GetCopyBatches()[Index.Get()]))
	{
		// Output도 Task 쪽은 TargetLeaf이며, 실제 복사 방향만 반대로 설정된다.
		if (Copy.TargetLeafProperty && Copy.TargetLeafProperty->GetFName() == TEXT("Destination")
			&& Copy.bCopyFromTargetToSource == bOutput) return &Copy;
	}
	return nullptr;
}

void CheckDestinationBinding(FAutomationTestBase& Test, const UStateTree& Tree,
	const FStateTreeNodeBase* Producer, const FStateTreeNodeBase* Consumer, const TCHAR* Label)
{
	const FPropertyBindingCopyInfo* Output = FindDestinationCopy(Tree, Producer, true);
	const FPropertyBindingCopyInfo* Input = FindDestinationCopy(Tree, Consumer, false);
	if (!Test.TestNotNull(FString::Printf(TEXT("%s Destination OUT binding"), Label), Output)
		|| !Test.TestNotNull(FString::Printf(TEXT("%s Destination IN binding"), Label), Input)) return;
	const FStateTreeDataHandle* OutHandle = Output->SourceDataHandle.GetPtr<FStateTreeDataHandle>();
	const FStateTreeDataHandle* InHandle = Input->SourceDataHandle.GetPtr<FStateTreeDataHandle>();
	Test.TestTrue(FString::Printf(TEXT("%s uses the same parameter and property"), Label),
		OutHandle && InHandle && *OutHandle == *InHandle
		&& Output->SourceLeafProperty == Input->SourceLeafProperty
		&& Output->SourceIndirection.Offset == Input->SourceIndirection.Offset);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySearchAssetsTest, "Outlier.Enemy.Search.Assets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemySearchAssetsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	for (EEnemySearchQueryMode Mode : {EEnemySearchQueryMode::Anchor, EEnemySearchQueryMode::Points})
	{
		const FName Name = Mode == EEnemySearchQueryMode::Anchor ? TEXT("EQS_Drone_SearchAnchor") : TEXT("EQS_SearchPoints");
		const UEnvQuery* Query = Cast<UEnvQuery>(LoadSearchAsset(*this, Name, UEnvQuery::StaticClass()));
		if (!TestNotNull(TEXT("EQS loads"), Query)) continue;
		if (!TestEqual(TEXT("Search EQS has exactly one option"), Query->GetOptions().Num(), 1)) continue;
		const UEnvQueryOption* Option = Query->GetOptions()[0];
		if (!TestNotNull(TEXT("EQS option exists"), Option)) continue;
		const UEnemyEnvQueryGenerator_Search* Generator = Cast<UEnemyEnvQueryGenerator_Search>(Option->Generator);
		if (TestNotNull(TEXT("Search generator is selected"), Generator))
			TestEqual(TEXT("Generator mode matches query"), Generator->Mode, Mode);
		if (!TestEqual(TEXT("Only flight destination test; no hidden target trace/distance"), Option->Tests.Num(), 1)) continue;
		const UEnemyEnvQueryTest_FlightDestination* Flight = Cast<UEnemyEnvQueryTest_FlightDestination>(Option->Tests[0]);
		if (!TestNotNull(TEXT("Flight destination filter exists"), Flight)) continue;
		TestEqual(TEXT("Filter purpose"), Flight->TestPurpose.GetValue(), EEnvTestPurpose::Filter);
		TestTrue(TEXT("Bool match requires a valid destination"), Flight->BoolValue.DefaultValue);
		TestEqual(TEXT("Clearance matches search Fly"), Flight->Clearance, 5.0f);
	}

	const UStateTree* Tree = Cast<UStateTree>(LoadSearchAsset(*this, TEXT("ST_VECDroneBattle_Gun"), UStateTree::StaticClass()));
	if (!TestNotNull(TEXT("Battle StateTree loads"), Tree) || !TestTrue(TEXT("StateTree is compiled and ready"), Tree->IsReadyToRun())) return false;
	const FCompactStateTreeState* Search = FindSearchState(*this, *Tree, TEXT("Search"));
	const FEnemySearchSessionTask* Session = FindSearchTask<FEnemySearchSessionTask>(*this, *Tree, Search);
	if (Session) TestFalse(TEXT("Parent session cannot keep a child from completing"), Session->bConsideredForCompletion);
	CheckSearchTransition(*this, *Tree, Search, TEXT("DirectCombat"), EStateTreeTransitionTrigger::OnTick);
	CheckSearchTransition(*this, *Tree, Search, TEXT("RestartHint"), EStateTreeTransitionTrigger::OnTick);

	const FCompactStateTreeState* SelectAnchor = FindSearchState(*this, *Tree, TEXT("SelectAnchor"));
	const FCompactStateTreeState* SelectPoints = FindSearchState(*this, *Tree, TEXT("SelectPoints"));
	const FCompactStateTreeState* SelectPoint = FindSearchState(*this, *Tree, TEXT("SelectPoint"));
	const FCompactStateTreeState* MoveAnchor = FindSearchState(*this, *Tree, TEXT("MoveAnchor"));
	const FCompactStateTreeState* MovePoint = FindSearchState(*this, *Tree, TEXT("MovePoint"));
	const FEnemyRunEnvQueryTask* Anchor = FindSearchTask<FEnemyRunEnvQueryTask>(*this, *Tree, SelectAnchor);
	const FEnemyRunEnvQueryTask* Points = FindSearchTask<FEnemyRunEnvQueryTask>(*this, *Tree, SelectPoints);
	const FEnemySearchProgressTask* Point = FindSearchTask<FEnemySearchProgressTask>(*this, *Tree, SelectPoint);
	const FEnemyFlyToLocationTask* AnchorFly = FindSearchTask<FEnemyFlyToLocationTask>(*this, *Tree, MoveAnchor);
	const FEnemyFlyToLocationTask* PointFly = FindSearchTask<FEnemyFlyToLocationTask>(*this, *Tree, MovePoint);
	CheckDestinationBinding(*this, *Tree, Anchor, AnchorFly, TEXT("SelectAnchor -> MoveAnchor"));
	CheckDestinationBinding(*this, *Tree, Point, PointFly, TEXT("SelectPoint -> MovePoint"));

	for (const FEnemyRunEnvQueryTask* Task : {Anchor, Points})
	{
		const auto* Data = GetSearchTaskData(*Tree, Task);
		if (!TestNotNull(TEXT("Query task instance data exists"), Data)) continue;
		TestFalse(TEXT("Search does not require a currently visible target"), Data->bRequireVisibleTarget);
		TestFalse(TEXT("Search cannot also use approach validation"), Data->bValidateReachability);
		TestFalse(TEXT("Search cannot also use orbit validation"), Data->bValidateOrbitPath);
		TestFalse(TEXT("Search cannot also use retreat validation"), Data->bValidateRetreatPath);
		TestEqual(TEXT("Search returns all matching candidates"), Data->RunMode.GetValue(), EEnvQueryRunMode::AllMatching);
		const EEnemySearchQueryMode Mode = Task == Anchor ? EEnemySearchQueryMode::Anchor : EEnemySearchQueryMode::Points;
		TestEqual(TEXT("Task search mode"), Data->SearchMode, Mode);
		TestNotNull(TEXT("Query template is assigned"), Data->QueryTemplate.Get());
		if (Data->QueryTemplate)
			TestEqual(TEXT("Task uses correct search template"), Data->QueryTemplate->GetFName(),
				FName(Mode == EEnemySearchQueryMode::Anchor ? TEXT("EQS_Drone_SearchAnchor") : TEXT("EQS_SearchPoints")));
	}
	if (const auto* Data = GetSearchTaskData(*Tree, Point))
		TestEqual(TEXT("SelectPoint retrieves current point, not checked/anchor progress"), Data->Action, EEnemySearchProgress::SelectCurrentPoint);
	for (const FEnemyFlyToLocationTask* Task : {AnchorFly, PointFly})
	{
		const auto* Data = GetSearchTaskData(*Tree, Task);
		if (!TestNotNull(TEXT("Fly instance data exists"), Data)) continue;
		TestTrue(TEXT("Search Fly enables AStar"), Data->bUsePathfinding);
		TestFalse(TEXT("Search Fly is not an orbit planned path"), Data->bUsePlannedPath);
		TestEqual(TEXT("Search Fly clearance"), Data->PathClearance, 5.0f);
		TestEqual(TEXT("Preselected search destinations are not corrected again by Fly"), Data->GoalSearchRadius, 0.0f);
		TestTrue(TEXT("Move has a finite timeout"), Data->MoveTimeout > 0.0f);
		TestTrue(TEXT("Move detects lack of progress"), Data->NoProgressTimeout > 0.0f);
	}

	// 정상 완료/실패 경로를 모두 확인한다. 이름만 존재하고 전이가 빠진 상태는 통과시키지 않는다.
	struct FExpectedSearchTransition { const TCHAR* From; const TCHAR* To; const TCHAR* Failure; };
	for (const FExpectedSearchTransition& Expected : {
		FExpectedSearchTransition{TEXT("SelectAnchor"), TEXT("MoveAnchor"), TEXT("EnterLocalGuard")},
		{TEXT("MoveAnchor"), TEXT("AnchorArrived"), TEXT("EnterLocalGuard")},
		{TEXT("AnchorArrived"), TEXT("SelectPoints"), TEXT("EnterLocalGuard")},
		{TEXT("SelectPoints"), TEXT("SelectPoint"), TEXT("EnterLocalGuard")},
		{TEXT("SelectPoint"), TEXT("MovePoint"), TEXT("EnterLocalGuard")},
		{TEXT("MovePoint"), TEXT("PointLookAround"), TEXT("EnterLocalGuard")},
		{TEXT("PointLookAround"), TEXT("PointChecked"), TEXT("EnterLocalGuard")},
		{TEXT("PointChecked"), TEXT("SelectPoint"), TEXT("EnterLocalGuard")},
		{TEXT("EnterLocalGuard"), TEXT("GuardLookAround"), nullptr},
		{TEXT("GuardLookAround"), TEXT("GuardWait"), TEXT("GuardWait")},
		{TEXT("GuardWait"), TEXT("GuardLookAround"), nullptr}})
	{
		const auto* State = FindSearchState(*this, *Tree, Expected.From);
		CheckSearchTransition(*this, *Tree, State, Expected.To, EStateTreeTransitionTrigger::OnStateSucceeded);
		if (Expected.Failure) CheckSearchTransition(*this, *Tree, State, Expected.Failure, EStateTreeTransitionTrigger::OnStateFailed);
		if (State && Search) TestTrue(TEXT("Search step belongs to Search parent"), Tree->GetStateFromHandle(State->Parent) == Search);
	}
	for (const TCHAR* Name : {TEXT("PointLookAround"), TEXT("GuardLookAround")})
		FindSearchTask<FEnemyLookAroundTask>(*this, *Tree, FindSearchState(*this, *Tree, Name));
	for (const TCHAR* Name : {TEXT("Start"), TEXT("LossWait"), TEXT("GuardWait")})
	{
		const auto* State = FindSearchState(*this, *Tree, Name);
		if (!State) continue;
		FConstStructView DelayData;
		// Delay는 Engine private 타입이므로 공개 Reflection으로 읽는다. 엔진 private include에 의존하지 않는다.
		for (int32 Index = 0; Index < State->TasksNum; ++Index)
		{
			const FConstStructView Node = Tree->GetNode(State->TasksBegin + Index);
			if (!Node.GetScriptStruct() || Node.GetScriptStruct()->GetFName() != TEXT("StateTreeDelayTask")) continue;
			const FStateTreeNodeBase* Delay = Node.GetPtr<const FStateTreeNodeBase>();
			if (Delay && Delay->InstanceTemplateIndex.IsValid()
				&& Tree->GetDefaultInstanceData().IsValidIndex(Delay->InstanceTemplateIndex.Get()))
				DelayData = Tree->GetDefaultInstanceData().GetStruct(Delay->InstanceTemplateIndex.Get());
		}
		if (!TestTrue(TEXT("Delay data exists"), DelayData.IsValid())) continue;
		const FFloatProperty* DurationProperty = FindFProperty<FFloatProperty>(DelayData.GetScriptStruct(), TEXT("Duration"));
		const FBoolProperty* ForeverProperty = FindFProperty<FBoolProperty>(DelayData.GetScriptStruct(), TEXT("bRunForever"));
		if (!TestNotNull(TEXT("Delay duration property exists"), DurationProperty)
			|| !TestNotNull(TEXT("Delay forever property exists"), ForeverProperty)) continue;
		const float Duration = FCString::Strcmp(Name, TEXT("Start")) == 0 ? 0.0f : FCString::Strcmp(Name, TEXT("LossWait")) == 0 ? 2.0f : 1.0f;
		TestEqual(FString::Printf(TEXT("%s duration"), Name), DurationProperty->GetPropertyValue_InContainer(DelayData.GetMemory()), Duration);
		TestFalse(TEXT("Search delay must finish"), ForeverProperty->GetPropertyValue_InContainer(DelayData.GetMemory()));
	}
	return true;
}

#endif
