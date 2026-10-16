#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Enemy/EnemyEnvQuery.h"
#include "Enemy/EnemyStateTreeMovementTasks.h"
#include "Enemy/EnemyStateTreeTacticalTasks.h"
#include "Enemy/EnemyStateTreeComponent.h"
#include "Enemy/EnemyCombatTestHelpers.h"
#include "Enemy/EnemyStateTreeCombatMovementTasks.h"
#include "Enemy/EnemyStateTreeSelfDestructTasks.h"
#include "Conditions/StateTreeCommonConditions.h"
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

void CheckHeightQueryConfiguration(FAutomationTestBase& Test, const FEnemyRunEnvQueryTaskInstanceData& Data,
	const UEnvQuery& Query, const FString& Location)
{
	// 수색은 현재 고도 범위를 유지한다. 전투 높이 스냅샷을 사용하는 질의만 추가 검사한다.
	if (Data.SearchMode != EEnemySearchQueryMode::None) return;
	if (!Test.TestEqual(Location + TEXT(" flight height requires exactly one EQS option"), Query.GetOptions().Num(), 1)) return;
	const UEnvQueryOption* Option = Query.GetOptions()[0];
	if (!Option || !Option->Generator) return;
	const UEnvQueryGenerator* Generator = Option->Generator.Get();
	const bool bApproach = Generator->IsA<UEnemyEnvQueryGenerator_Approach>();
	const bool bOrbit = Generator->IsA<UEnemyEnvQueryGenerator_Orbit>();
	const bool bRetreat = Generator->GetClass() == UEnemyEnvQueryGenerator_Retreat::StaticClass();
	const bool bChase = Generator->GetClass() == UEnemyEnvQueryGenerator_Chase::StaticClass();
	Test.TestTrue(Location + TEXT(" flight height requires a supported combat generator"), bApproach || bOrbit || bRetreat || bChase);
	Test.TestTrue(Location + TEXT(" flight height requires the visible-target snapshot"), Data.bRequireVisibleTarget);
	Test.TestEqual(Location + TEXT(" flight height returns all candidates for path validation"),
		Data.RunMode.GetValue(), EEnvQueryRunMode::AllMatching);
	Test.TestTrue(Location + TEXT(" flight height generator matches exactly one path validation mode"),
		Data.bValidateReachability == bApproach && Data.bValidateOrbitPath == bOrbit
		&& Data.bValidateRetreatPath == bRetreat && Data.bValidateChasePath == bChase);
	// 높이 보정과 EQS 필터의 캡슐 여유가 다르면 안전 후보가 다시 탈락할 수 있다.
	for (const UEnvQueryTest* QueryTest : Option->Tests)
	{
		if (const auto* Flight = Cast<UEnemyEnvQueryTest_FlightDestination>(QueryTest))
			Test.TestTrue(Location + TEXT(" flight height and destination filter use the same finite clearance"),
				FMath::IsFinite(Data.Clearance) && Data.Clearance >= 0.0f && Flight->Clearance == Data.Clearance);
	}
}

void CheckLinkedQueries(FAutomationTestBase& Test, const UStateTree& Tree, TSet<const UStateTree*>& Visited,
	bool bHeightEnabled)
{
	// Linked Tree와 BP Override를 함께 검사한다. 순환 참조는 한 번만 방문한다.
	if (Visited.Contains(&Tree)) return;
	Visited.Add(&Tree);
	if (!Test.TestTrue(FString::Printf(TEXT("%s is compiled and ready"), *Tree.GetPathName()), Tree.IsReadyToRun())) return;
	for (const FCompactStateTreeState& State : Tree.GetStates())
	{
		for (int32 Index = 0; Index < State.TasksNum; ++Index)
		{
			const FEnemyRunEnvQueryTask* Task = Tree.GetNode(State.TasksBegin + Index).GetPtr<const FEnemyRunEnvQueryTask>();
			if (!Task || !Task->bTaskEnabled) continue;
			const FString Location = FString::Printf(TEXT("%s / %s / Run Env Query"), *Tree.GetPathName(), *State.Name.ToString());
			const auto* Data = GetSearchTaskData(Tree, Task);
			if (!Test.TestNotNull(Location + TEXT(" instance data"), Data)) continue;
			const UEnvQuery* Query = Data->QueryTemplate.Get();
			if (!Test.TestNotNull(Location + TEXT(" QueryTemplate must be assigned in Details"), Query)) continue;
			if (bHeightEnabled) CheckHeightQueryConfiguration(Test, *Data, *Query, Location);
			Test.AddInfo(FString::Printf(TEXT("[DroneAsset] %s -> %s"), *Location, *Query->GetPathName()));
			Test.TestTrue(Location + TEXT(" query has an option"), !Query->GetOptions().IsEmpty());
			for (const UEnvQueryOption* Option : Query->GetOptions())
			{
				if (!Test.TestNotNull(Location + TEXT(" EQS option"), Option)) continue;
				Test.TestNotNull(Location + TEXT(" EQS generator"), Option->Generator.Get());
				bool bHasFlightFilter = false;
				bool bHasFlightTest = false;
				for (const UEnvQueryTest* QueryTest : Option->Tests)
				{
					const auto* Flight = Cast<UEnemyEnvQueryTest_FlightDestination>(QueryTest);
					bHasFlightTest |= Flight != nullptr;
					// 실패 시 필터 누락과 잘못된 목적/Bool 설정을 로그에서 구분한다.
					Test.AddInfo(FString::Printf(TEXT("[DroneAsset] Query=%s Test=%s FlightDestination=%d Purpose=%d BoolMatch=%d"),
						*Query->GetPathName(), *GetNameSafe(QueryTest), Flight != nullptr,
						QueryTest ? int32(QueryTest->TestPurpose.GetValue()) : -1,
						Flight ? int32(Flight->BoolValue.DefaultValue) : -1));
					bHasFlightFilter |= Flight && Flight->BoolValue.DefaultValue
						&& Flight->TestPurpose != EEnvTestPurpose::Score;
				}
				// 선회는 목적지 단독 필터 없이도 Task가 실제 곡선의 모든 구간을 검사한다.
				// 단, 존재하는 FlightDestination 필터의 잘못된 설정까지 예외로 숨기지는 않는다.
				const bool bUsesOrbitRouteValidation = Data->bValidateOrbitPath
					&& Cast<UEnemyEnvQueryGenerator_Orbit>(Option->Generator) != nullptr;
				Test.TestTrue(Location + TEXT(" destination needs a valid FlightDestination filter or dedicated orbit route validation"),
					bHasFlightFilter || (!bHasFlightTest && bUsesOrbitRouteValidation));
				if (!bHasFlightTest && bUsesOrbitRouteValidation)
					Test.AddInfo(FString::Printf(TEXT("[DroneAsset] Query=%s Validation=OrbitCapsuleSegmentsAndRoom"), *Query->GetPathName()));
			}
		}
		if (State.LinkedAsset) CheckLinkedQueries(Test, *State.LinkedAsset, Visited, bHeightEnabled);
	}
}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyDroneAssetReferencesTest, "Outlier.Enemy.Regression.AssetReferences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyDroneAssetReferencesTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Type : {TEXT("Gun"), TEXT("Melee")})
	{
		Names.Add(Type);
		Commands.Add(Type);
	}
}

bool FEnemyDroneAssetReferencesTest::RunTest(const FString& Parameters)
{
	const bool bMelee = Parameters == TEXT("Melee");
	const UClass* Class = LoadClass<AEnemyBase>(nullptr, bMelee
		? TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Explosion.BP_VECDrone_Explosion_C")
		: TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"));
	if (!TestNotNull(TEXT("Actual enemy BP class loads"), Class)) return false;
	const AEnemyBase* Enemy = Class->GetDefaultObject<AEnemyBase>();
	// 높이는 BP별 선택 사항이다. 비활성 BP에 활성화나 새 수치를 강제하지 않는다.
	const bool bHeightEnabled = Enemy->FlightHeightSettings.bEnabled;
	if (bHeightEnabled) TestTrue(TEXT("Enabled BP flight height settings are valid"), Enemy->FlightHeightSettings.IsValid());
	const auto* Component = Cast<UEnemyStateTreeComponent>(Enemy->GetStateTreeComponent());
	if (!TestNotNull(TEXT("BP StateTreeComponent uses EnemyStateTreeComponent"), Component)) return false;
	TSet<const UStateTree*> Visited;
	const UStateTree* Common = Component->GetConfiguredStateTreeReference().GetStateTree();
	if (TestNotNull(TEXT("BP StateTreeComponent common tree is assigned"), Common)) CheckLinkedQueries(*this, *Common, Visited, bHeightEnabled);
	for (const FName PropertyName : {FName(TEXT("BattleStateTreeReference")), FName(TEXT("PossessedAttackStateTreeReference"))})
	{
		const FStructProperty* Property = FindFProperty<FStructProperty>(Class, PropertyName);
		if (!TestNotNull(PropertyName.ToString() + TEXT(" property"), Property)) continue;
		const FStateTreeReference* Reference = Property->ContainerPtrToValuePtr<FStateTreeReference>(Enemy);
		const UStateTree* Tree = Reference->GetStateTree();
		if (!TestNotNull(PropertyName.ToString() + TEXT(" must be assigned on BP"), Tree)) continue;
		if (PropertyName == TEXT("BattleStateTreeReference"))
			TestEqual(TEXT("BP uses its own battle type, not the other drone's tree"), Tree->GetFName(),
				FName(bMelee ? TEXT("ST_VECDroneBattle_Melee") : TEXT("ST_VECDroneBattle_Gun")));
		CheckLinkedQueries(*this, *Tree, Visited, bHeightEnabled);
	}
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemySearchAssetsTest, "Outlier.Enemy.Search.Assets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemySearchAssetsTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Type : {TEXT("Gun"), TEXT("Melee")})
	{
		Names.Add(Type);
		Commands.Add(Type);
	}
}

bool FEnemySearchAssetsTest::RunTest(const FString& Parameters)
{
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

	// 두 유형 모두 저장된 실제 에셋의 수색 Task / 바인딩 / 전이를 같은 기준으로 확인한다.
	const FName TreeName = Parameters == TEXT("Melee") ? TEXT("ST_VECDroneBattle_Melee") : TEXT("ST_VECDroneBattle_Gun");
	const UStateTree* Tree = Cast<UStateTree>(LoadSearchAsset(*this, TreeName, UStateTree::StaticClass()));
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

namespace
{
const FPropertyBindingCopyInfo* FindCombatCopy(const UStateTree& Tree, const FStateTreeNodeBase& Node,
	FName Property, bool bOutput)
{
	const FStateTreeIndex16 Batch = bOutput ? Node.OutputBindingsBatch : Node.BindingsBatch;
	// 파생 타입에는 같은 이름의 구형 API가 남아 있어 공용 컬렉션의 CopyInfo API를 명시한다.
	const FPropertyBindingBindingCollection& Bindings = Tree.GetPropertyBindings();
	if (!Batch.IsValid() || !Bindings.GetCopyBatches().IsValidIndex(Batch.Get())) return nullptr;
	for (const auto& Copy : Bindings.GetBatchCopies(Bindings.GetCopyBatches()[Batch.Get()]))
		if (Copy.TargetLeafProperty && Copy.TargetLeafProperty->GetFName() == Property
			&& Copy.bCopyFromTargetToSource == bOutput) return &Copy;
	return nullptr;
}

bool IsCombatBinding(const UStateTree& Tree, const FStateTreeNodeBase& Producer, FName OutputName,
	const FStateTreeNodeBase& Consumer, FName InputName)
{
	const auto* Input = FindCombatCopy(Tree, Consumer, InputName, false);
	if (!Input || !Input->SourceLeafProperty) return false;
	const auto* InputHandle = Input->SourceDataHandle.GetPtr<FStateTreeDataHandle>();
	if (!InputHandle) return false;
	// Task 출력을 직접 참조하거나 부모 파라미터를 경유하는 두 에디터 바인딩 방식을 모두 허용한다.
	if (*InputHandle == Producer.InstanceDataHandle && Input->SourceLeafProperty->GetFName() == OutputName) return true;
	const auto* Output = FindCombatCopy(Tree, Producer, OutputName, true);
	const auto* OutputHandle = Output ? Output->SourceDataHandle.GetPtr<FStateTreeDataHandle>() : nullptr;
	return OutputHandle && *OutputHandle == *InputHandle && Output->SourceLeafProperty == Input->SourceLeafProperty
		&& Output->SourceIndirection.Offset == Input->SourceIndirection.Offset;
}

const FCompactStateTransition* FindCombatTransition(const UStateTree& Tree, const FCompactStateTreeState& State,
	EStateTreeTransitionTrigger Trigger)
{
	for (int32 Index = 0; Index < State.TransitionsNum; ++Index)
	{
		const auto* Transition = Tree.GetTransitionFromIndex(FStateTreeIndex16(State.TransitionsBegin + Index));
		if (Transition && Transition->bTransitionEnabled && EnumHasAnyFlags(Transition->Trigger, Trigger)) return Transition;
	}
	return nullptr;
}

bool HasCombatBoolCondition(const UStateTree& Tree, const FCompactStateTransition& Transition,
	const FStateTreeNodeBase& Producer, FName OutputName)
{
	// 재선정/물리적 후보 없음은 단일 true 조건이어야 한다. OR 그룹에 매달린 무관한 조건은 인정하지 않는다.
	if (Transition.ConditionsNum != 1) return false;
	const auto* Condition = Tree.GetNode(Transition.ConditionsBegin).GetPtr<const FStateTreeCompareBoolCondition>();
	const auto* Data = EnemyCombatTesting::GetDefaultData(Tree, Condition);
	return Data && Data->bRight && !Condition->bInvert
		&& Condition->EvaluationMode == EStateTreeConditionEvaluationMode::Evaluated
		&& !FindCombatCopy(Tree, *Condition, TEXT("bRight"), false)
		&& IsCombatBinding(Tree, Producer, OutputName, *Condition, TEXT("bLeft"));
}

bool HasTimedCombatRetry(const UStateTree& Tree, const FCompactStateTreeState* State,
	const FCompactStateTreeState& Select)
{
	if (!State) return false;
	bool bFiniteDelay = false;
	for (int32 Index = 0; Index < State->TasksNum; ++Index)
	{
		const auto* Node = Tree.GetNode(State->TasksBegin + Index).GetPtr<const FStateTreeTaskBase>();
		if (!Node || !Node->bTaskEnabled || !Node->InstanceTemplateIndex.IsValid()
			|| !Tree.GetDefaultInstanceData().IsValidIndex(Node->InstanceTemplateIndex.Get())) continue;
		const FConstStructView Data = Tree.GetDefaultInstanceData().GetStruct(Node->InstanceTemplateIndex.Get());
		if (!Data.IsValid()) continue;
		const FFloatProperty* Duration = FindFProperty<FFloatProperty>(Data.GetScriptStruct(), TEXT("Duration"));
		const FBoolProperty* Forever = FindFProperty<FBoolProperty>(Data.GetScriptStruct(), TEXT("bRunForever"));
		if (!Duration || !Forever) continue;
		const float Seconds = Duration->GetPropertyValue_InContainer(Data.GetMemory());
		bFiniteDelay |= FMath::IsFinite(Seconds) && Seconds > 0 && !Forever->GetPropertyValue_InContainer(Data.GetMemory());
	}
	const auto* Retry = FindCombatTransition(Tree, *State, EStateTreeTransitionTrigger::OnStateSucceeded);
	return bFiniteDelay && Retry && Retry->ConditionsNum == 0 && Tree.GetStateFromHandle(Retry->State) == &Select;
}

void CheckCombatAssessments(FAutomationTestBase& Test, const UStateTree& Tree)
{
	// Global Task도 포함한다. 계속 Running인 판단 Task가 자식 이동의 완료를 붙잡지 않아야 한다.
	for (int32 Index = 0; Index < Tree.GetNodes().Num(); ++Index)
	{
		const FConstStructView Node = Tree.GetNode(Index);
		const auto* Task = Node.GetPtr<const FStateTreeTaskBase>();
		if (!Task || !Task->bTaskEnabled) continue;
		if (Node.GetPtr<const FEnemyCombatAssessmentTask>() || Node.GetPtr<const FEnemySelfDestructAssessmentTask>())
			Test.TestFalse(Tree.GetPathName() + TEXT(" assessment does not block child completion"), Task->bConsideredForCompletion);
	}
}

void CheckCombatQueryWiring(FAutomationTestBase& Test, const UStateTree& Tree,
	const FCompactStateTreeState& Select, const FEnemyRunEnvQueryTask& Query)
{
	const FString Label = EnemyCombatTesting::StatePath(Tree, &Select);
	const auto* Data = GetSearchTaskData(Tree, &Query);
	if (!Test.TestNotNull(Label + TEXT(" query data"), Data)) return;
	const int32 Modes = int32(Data->bValidateReachability) + int32(Data->bValidateOrbitPath)
		+ int32(Data->bValidateRetreatPath) + int32(Data->bValidateChasePath);
	Test.TestEqual(Label + TEXT(" exactly one combat validation mode"), Modes, 1);
	Test.TestTrue(Label + TEXT(" requires direct sight"), Data->bRequireVisibleTarget);
	Test.TestEqual(Label + TEXT(" all candidates reach path validation"), Data->RunMode.GetValue(), EEnvQueryRunMode::AllMatching);
	Test.TestTrue(Label + TEXT(" finite positive query interval and timeout"),
		FMath::IsFinite(Data->QueryInterval) && Data->QueryInterval > 0
		&& FMath::IsFinite(Data->QueryTimeout) && Data->QueryTimeout > 0);
	const auto* Success = FindCombatTransition(Tree, Select, EStateTreeTransitionTrigger::OnStateSucceeded);
	const auto* Move = Success ? Tree.GetStateFromHandle(Success->State) : nullptr;
	if (!Test.TestNotNull(Label + TEXT(" success leads to Move"), Move)) return;
	const auto* Fly = FindSearchTask<FEnemyFlyToLocationTask>(Test, Tree, Move);
	const auto* Guard = FindSearchTask<FEnemyCombatDestinationGuardTask>(Test, Tree, Move);
	const auto* FlyData = GetSearchTaskData(Tree, Fly);
	const auto* GuardData = GetSearchTaskData(Tree, Guard);
	if (!Test.TestNotNull(Label + TEXT(" Fly instance data"), FlyData)
		|| !Test.TestNotNull(Label + TEXT(" Guard instance data"), GuardData)) return;
	Test.TestTrue(Label + TEXT(" Destination reaches Fly"), IsCombatBinding(Tree, Query, TEXT("Destination"), *Fly, TEXT("Destination")));
	for (FName Property : {FName(TEXT("SelectedTargetActor")), FName(TEXT("SelectedTargetLocation"))})
		Test.TestTrue(Label + TEXT(" Guard snapshot binding: ") + Property.ToString(), IsCombatBinding(Tree, Query, Property, *Guard, Property));
	Test.TestFalse(Label + TEXT(" Guard does not block Move completion"), Guard->bConsideredForCompletion);
	Test.TestTrue(Label + TEXT(" Fly completes Move"), Fly->bConsideredForCompletion);
	Test.AddInfo(FString::Printf(TEXT("[CombatAssetMove] %s MoveTimeout=%.2f NoProgressTimeout=%.2f Planned=%d GoalSearchRadius=%.1f"),
		*EnemyCombatTesting::StatePath(Tree, Move), FlyData->MoveTimeout, FlyData->NoProgressTimeout,
		FlyData->bUsePlannedPath, FlyData->GoalSearchRadius));
	Test.TestTrue(Label + TEXT(" finite movement/progress timeouts"),
		FMath::IsFinite(FlyData->MoveTimeout) && FlyData->MoveTimeout > 0
		&& FMath::IsFinite(FlyData->NoProgressTimeout) && FlyData->NoProgressTimeout > 0);
	Test.TestTrue(Label + TEXT(" finite positive target-change threshold"),
		FMath::IsFinite(GuardData->TargetChangeDistance) && GuardData->TargetChangeDistance > 0);
	Test.TestEqual(Label + TEXT(" orbit uses planned path; others use AStar"), FlyData->bUsePlannedPath, Data->bValidateOrbitPath);
	Test.TestEqual(Label + TEXT(" only non-orbit uses AStar"), FlyData->bUsePathfinding, !Data->bValidateOrbitPath);
	// 선회는 전달받은 경유점만 사용한다. A* 목적지 보정 반경은 실제로 사용하는 이동에서만 검사한다.
	if (!Data->bValidateOrbitPath)
		Test.TestEqual(Label + TEXT(" exact validated destination"), FlyData->GoalSearchRadius, 0.0f);
	if (Data->bValidateOrbitPath)
		Test.TestTrue(Label + TEXT(" arc waypoints reach Fly"), IsCombatBinding(Tree, Query, TEXT("SelectedWaypoints"), *Fly, TEXT("PlannedWaypoints")));
	if (Data->bValidateRetreatPath)
	{
		Test.TestTrue(Label + TEXT(" retreat cannot cross the target half-space"), FlyData->bConstrainRetreatPath);
		for (FName Property : {FName(TEXT("RetreatOrigin")), FName(TEXT("RetreatDirection"))})
			Test.TestTrue(Label + TEXT(" retreat binding: ") + Property.ToString(), IsCombatBinding(Tree, Query, Property, *Fly, Property));
	}

	const auto* Reselect = FindCombatTransition(Tree, *Move, EStateTreeTransitionTrigger::OnTick);
	Test.TestTrue(Label + TEXT(" Move OnTick uses Guard.ShouldReselect == true"),
		Reselect && HasCombatBoolCondition(Tree, *Reselect, *Guard, TEXT("bShouldReselect")));
	if (Reselect)
	{
		const auto* Target = Tree.GetStateFromHandle(Reselect->State);
		Test.AddInfo(FString::Printf(TEXT("[CombatAssetReselect] From=%s To=%s Expected=%s"),
			*EnemyCombatTesting::StatePath(Tree, Move), *EnemyCombatTesting::StatePath(Tree, Target), *Label));
		Test.TestTrue(Label + TEXT(" reselection returns directly or through a timed retry"),
			Target == &Select || HasTimedCombatRetry(Tree, Target, Select));
	}
	const auto* Failed = FindCombatTransition(Tree, Select, EStateTreeTransitionTrigger::OnStateFailed);
	Test.TestNotNull(Label + TEXT(" query failure has a fallback"), Failed);
	if (Data->bValidateChasePath)
	{
		Test.TestTrue(Label + TEXT(" failed chase retries with a finite delay, not Search/LocalGuard"),
			Failed && Failed->ConditionsNum == 0 && HasTimedCombatRetry(Tree, Tree.GetStateFromHandle(Failed->State), Select));
		const auto* MoveFailed = FindCombatTransition(Tree, *Move, EStateTreeTransitionTrigger::OnStateFailed);
		Test.TestTrue(Label + TEXT(" blocked chase movement retries with a delay"), MoveFailed
			&& HasTimedCombatRetry(Tree, Tree.GetStateFromHandle(MoveFailed->State), Select));
	}
	else if (Data->bValidateOrbitPath || Data->bValidateRetreatPath)
	{
		const FName NoDestination = Data->bValidateOrbitPath ? TEXT("bNoOrbitDestination") : TEXT("bNoRetreatDestination");
		Test.TestTrue(Label + TEXT(" physical fallback cannot be triggered by budget exhaustion"),
			Failed && HasCombatBoolCondition(Tree, *Failed, Query, NoDestination));
	}
	if (Data->bValidateOrbitPath)
	{
		const auto* Arrived = FindCombatTransition(Tree, *Move, EStateTreeTransitionTrigger::OnStateSucceeded);
		const auto* HoldState = Arrived ? Tree.GetStateFromHandle(Arrived->State) : nullptr;
		const auto* Hold = FindSearchTask<FEnemyStationaryFireTask>(Test, Tree, HoldState);
		const auto* HoldData = GetSearchTaskData(Tree, Hold);
		if (Test.TestNotNull(Label + TEXT(" orbit arrival has stationary fire hold"), HoldData))
			Test.TestTrue(Label + TEXT(" stationary fire lasts 0.5..1 seconds"),
				FMath::IsFinite(HoldData->MinDuration) && FMath::IsFinite(HoldData->MaxDuration)
				&& HoldData->MinDuration >= 0.5f && HoldData->MaxDuration <= 1.0f && HoldData->MinDuration <= HoldData->MaxDuration);
	}
}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyCombatAssetsTest, "Outlier.Enemy.Combat.Assets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyCombatAssetsTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Type : {TEXT("Gun"), TEXT("Melee")}) { Names.Add(Type); Commands.Add(Type); }
}

bool FEnemyCombatAssetsTest::RunTest(const FString& Parameters)
{
	const bool bMelee = Parameters == TEXT("Melee");
	const UClass* Class = LoadClass<AEnemyBase>(nullptr, bMelee
		? TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Explosion.BP_VECDrone_Explosion_C")
		: TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"));
	if (!TestNotNull(TEXT("Actual combat BP loads"), Class)) return false;
	const AEnemyBase* Enemy = Class->GetDefaultObject<AEnemyBase>();
	const auto* Component = Cast<UEnemyStateTreeComponent>(Enemy->GetStateTreeComponent());
	const FStructProperty* Battle = FindFProperty<FStructProperty>(Class, TEXT("BattleStateTreeReference"));
	if (!TestNotNull(TEXT("Enemy StateTree component"), Component) || !TestNotNull(TEXT("BP battle reference"), Battle)) return false;
	const UStateTree* Common = Component->GetConfiguredStateTreeReference().GetStateTree();
	if (!TestNotNull(TEXT("BP has a configured common tree"), Common)) return false;
	if (!TestTrue(Common->GetPathName() + TEXT(" common tree compiled and ready"), Common->IsReadyToRun())) return false;
	CheckCombatAssessments(*this, *Common);
	TArray<const UStateTree*> Trees;
	EnemyCombatTesting::CollectTrees(Battle->ContainerPtrToValuePtr<FStateTreeReference>(Enemy)->GetStateTree(), *Component, Trees);
	int32 Approach = 0, Orbit = 0, Retreat = 0, Chase = 0, Charge = 0;
	for (const UStateTree* Tree : Trees)
	{
		if (!TestTrue(Tree->GetPathName() + TEXT(" compiled and ready"), Tree->IsReadyToRun())) continue;
		CheckCombatAssessments(*this, *Tree);
		for (const auto& State : Tree->GetStates())
		{
			const FString Label = EnemyCombatTesting::StatePath(*Tree, &State);
			for (int32 Index = 0; Index < State.TasksNum; ++Index)
			{
				const FConstStructView Node = Tree->GetNode(State.TasksBegin + Index);
				const auto* Task = Node.GetPtr<const FStateTreeTaskBase>();
				if (!Task || !Task->bTaskEnabled) continue;
				if (const auto* Query = Node.GetPtr<const FEnemyRunEnvQueryTask>())
				{
					const auto* Data = GetSearchTaskData(*Tree, Query);
					if (!Data || Data->SearchMode != EEnemySearchQueryMode::None) continue;
					Approach += Data->bValidateReachability; Orbit += Data->bValidateOrbitPath;
					Retreat += Data->bValidateRetreatPath; Chase += Data->bValidateChasePath;
					AddInfo(TEXT("[CombatAsset] ") + Label);
					CheckCombatQueryWiring(*this, *Tree, State, *Query);
				}
				if (const auto* ChargeTask = Node.GetPtr<const FEnemyChargeLastDirectionTask>())
				{
					++Charge;
					const auto* Data = GetSearchTaskData(*Tree, ChargeTask);
					if (TestNotNull(Label + TEXT(" charge data"), Data))
						TestTrue(Label + TEXT(" charge distance is capped at 5m"), Data->ChargeEntryDistance > 0 && Data->ChargeEntryDistance <= 500);
					const auto* Success = FindCombatTransition(*Tree, State, EStateTreeTransitionTrigger::OnStateSucceeded);
					const auto* Detonate = Success ? Tree->GetStateFromHandle(Success->State) : nullptr;
					if (TestNotNull(Label + TEXT(" charge success leads to detonation"), Detonate))
						FindSearchTask<FEnemyBombDetonateTask>(*this, *Tree, Detonate);
				}
			}
		}
	}
	TestEqual(TEXT("Enabled approach query count"), Approach, bMelee ? 0 : 1);
	TestEqual(TEXT("Enabled orbit query count"), Orbit, bMelee ? 0 : 1);
	TestEqual(TEXT("Enabled retreat query count"), Retreat, bMelee ? 0 : 1);
	TestEqual(TEXT("Enabled chase query count"), Chase, bMelee ? 1 : 0);
	TestEqual(TEXT("Enabled AI charge count"), Charge, bMelee ? 1 : 0);
	return true;
}

#endif
