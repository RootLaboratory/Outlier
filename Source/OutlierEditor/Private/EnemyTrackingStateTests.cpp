#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Enemy/EnemyStateTreeSyncTask.h"
#include "Conditions/StateTreeCommonConditions.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "StateTree.h"
#include "StateTreeCompiler.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "Tasks/StateTreeRunParallelStateTreeTask.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace
{
constexpr TCHAR TrackingTreeFolder[] = TEXT("/Game/Blueprints/AI/VECDrone/StateTree");

bool IsTrackingSelectionTree(const UStateTree& Tree)
{
	const FName Name = Tree.GetFName();
	return Name == TEXT("ST_VECDrone_Common") || Name == TEXT("ST_VECDroneBattle_Gun")
		|| Name == TEXT("ST_VECDroneBattle_Melee") || Name == TEXT("ST_VECDroneDirectCombat_Gun")
		|| Name == TEXT("ST_VECDroneBattle_Default");
}

void CollectTrackingStates(UStateTreeState* State, TArray<UStateTreeState*>& States)
{
	if (!State) return;
	States.Add(State);
	for (UStateTreeState* Child : State->Children) CollectTrackingStates(Child, States);
}

void CollectSyncID(const FStateTreeEditorNode& Node, TSet<FGuid>& SyncIDs)
{
	if (Node.Node.GetScriptStruct() == FEnemyStateTreeSyncTask::StaticStruct()) SyncIDs.Add(Node.ID);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyTrackingStateBindingsTest,
	"Outlier.Enemy.Tracking.StateBindings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyTrackingStateBindingsTest::RunTest(const FString& Parameters)
{
	const bool bInstall = FParse::Param(FCommandLine::Get(), TEXT("OutlierInstallTrackingState"));
	if (!TestNotNull(TEXT("Sync exposes independent trackable-target output"),
		FindFProperty<FBoolProperty>(FEnemyStateTreeSyncTaskInstanceData::StaticStruct(), TEXT("bHasTrackableTarget")))) return false;
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	Registry.ScanPathsSynchronous({ TrackingTreeFolder });
	TArray<FAssetData> Assets;
	Registry.GetAssetsByClass(UStateTree::StaticClass()->GetClassPathName(), Assets, true);
	TArray<UStateTree*> Trees;
	for (const FName Name : { FName(TEXT("ST_VECDrone_Common")), FName(TEXT("ST_VECDroneBattle_Gun")), FName(TEXT("ST_VECDroneBattle_Melee")) })
	{
		TArray<FAssetData> Matches;
		for (const FAssetData& Asset : Assets)
		{
			if (Asset.AssetName == Name && Asset.PackagePath.ToString() == TrackingTreeFolder) Matches.Add(Asset);
		}
		if (!TestEqual(FString::Printf(TEXT("Canonical tracking tree %s exists exactly once"), *Name.ToString()), Matches.Num(), 1)) return false;
		UStateTree* Tree = Cast<UStateTree>(Matches[0].GetAsset());
		if (!TestNotNull(TEXT("Tracking tree loads"), Tree)) return false;
		Trees.AddUnique(Tree);
	}

	// 실제 LinkedAsset만 따라간다. 같은 이름의 이전 폴더 사본이나 다른 적 트리는 이전하지 않는다.
	for (int32 Index = 0; Index < Trees.Num(); ++Index)
	{
		UStateTreeEditorData* Data = Cast<UStateTreeEditorData>(Trees[Index]->EditorData);
		if (!TestNotNull(TEXT("Tracking tree editor graph exists"), Data)) return false;
		TArray<UStateTreeState*> States;
		for (UStateTreeState* Root : Data->SubTrees) CollectTrackingStates(Root, States);
		for (const UStateTreeState* State : States)
		{
			if (!State->LinkedAsset) continue;
			UStateTree* Linked = State->LinkedAsset;
			// 공격 트리는 별도 Attack.StateBindings 검사에서 다루고 기존 추적 이전 범위는 유지한다.
			if (!IsTrackingSelectionTree(*Linked)) continue;
			if (!TestEqual(TEXT("Linked tracking asset remains in canonical drone folder"),
				FPackageName::GetLongPackagePath(Linked->GetOutermost()->GetName()), FString(TrackingTreeFolder))) return false;
			Trees.AddUnique(Linked);
		}
	}

	// 간접 파라미터 입력은 공격 Task와 공유될 수 있어 조건만의 이전으로 추정하지 않는다.
	// 이 경로가 있으면 수정/저장 전에 실패하여 명시적인 파라미터 분리가 필요함을 알린다.
	for (UStateTree* Tree : Trees)
	{
		const UStateTreeEditorData* Data = CastChecked<UStateTreeEditorData>(Tree->EditorData);
		TSet<FGuid> SyncIDs;
		for (const FStateTreeEditorNode& Node : Data->GlobalTasks) CollectSyncID(Node, SyncIDs);
		TArray<UStateTreeState*> States;
		for (UStateTreeState* Root : Data->SubTrees) CollectTrackingStates(Root, States);
		TSet<FGuid> LinkedParameterIDs;
		for (const UStateTreeState* State : States)
		{
			CollectSyncID(State->SingleTask, SyncIDs);
			for (const FStateTreeEditorNode& Node : State->Tasks) CollectSyncID(Node, SyncIDs);
			if (State->LinkedAsset && IsTrackingSelectionTree(*State->LinkedAsset)) LinkedParameterIDs.Add(State->Parameters.ID);
		}
		for (const FStateTreePropertyPathBinding& Binding : Data->EditorBindings.GetBindings())
		{
			if (SyncIDs.Contains(Binding.GetSourcePath().GetStructID())
				&& LinkedParameterIDs.Contains(Binding.GetTargetPath().GetStructID())
				&& Binding.GetSourcePath().ToString() == TEXT("bPlayerCurrentlyVisible"))
			{
				AddError(FString::Printf(TEXT("%s has indirect visibility binding to linked parameters %s; separate tracking and attack inputs before migration"),
					*Tree->GetPathName(), *Binding.GetTargetPath().ToString()));
				return false;
			}
		}
	}

	// 저장 전에 모든 대상 패키지를 검사하여 다른 에디터 변경을 함께 저장하지 않는다.
	if (bInstall)
	{
		for (const UStateTree* Tree : Trees)
		{
			if (!TestFalse(FString::Printf(TEXT("Migration requires clean package %s"), *Tree->GetPathName()), Tree->GetOutermost()->IsDirty())) return false;
		}
	}

	int32 TrackingConditionBindings = 0;
	for (UStateTree* Tree : Trees)
	{
		UStateTreeEditorData* Data = CastChecked<UStateTreeEditorData>(Tree->EditorData);
		TSet<FGuid> SyncIDs;
		TSet<FGuid> ConditionIDs;
		for (const FStateTreeEditorNode& Node : Data->GlobalTasks) CollectSyncID(Node, SyncIDs);
		TArray<UStateTreeState*> States;
		for (UStateTreeState* Root : Data->SubTrees) CollectTrackingStates(Root, States);
		for (const UStateTreeState* State : States)
		{
			CollectSyncID(State->SingleTask, SyncIDs);
			for (const FStateTreeEditorNode& Node : State->Tasks) CollectSyncID(Node, SyncIDs);
			for (const FStateTreeEditorNode& Node : State->EnterConditions) ConditionIDs.Add(Node.ID);
			for (const FStateTreeTransition& Transition : State->Transitions)
				for (const FStateTreeEditorNode& Node : Transition.Conditions) ConditionIDs.Add(Node.ID);
		}

		bool bChanged = false;
		for (FStateTreePropertyPathBinding& Binding : Data->EditorBindings.GetMutableBindings())
		{
			if (!SyncIDs.Contains(Binding.GetSourcePath().GetStructID())
				|| !ConditionIDs.Contains(Binding.GetTargetPath().GetStructID())) continue;
			if (Binding.GetSourcePath().ToString() != TEXT("bPlayerCurrentlyVisible")) continue;
			if (!bInstall) continue;
			if (!bChanged)
			{
				Tree->Modify();
				Data->Modify();
				bChanged = true;
			}
			// 공격 Task의 실제 시야 입력은 보존하고 상태 선택/전이만 추적 가능 여부로 바꾼다.
			Binding.GetMutableSourcePath().FromString(TEXT("bHasTrackableTarget"));
		}

		for (const FStateTreePropertyPathBinding& Binding : Data->EditorBindings.GetBindings())
		{
			if (!SyncIDs.Contains(Binding.GetSourcePath().GetStructID())
				|| !ConditionIDs.Contains(Binding.GetTargetPath().GetStructID())) continue;
			TestTrue(FString::Printf(TEXT("%s condition %s no longer selects on direct visibility (run with -OutlierInstallTrackingState)"),
				*Tree->GetPathName(), *Binding.GetTargetPath().GetStructID().ToString()),
				Binding.GetSourcePath().ToString() != TEXT("bPlayerCurrentlyVisible"));
			if (Binding.GetSourcePath().ToString() == TEXT("bHasTrackableTarget")) ++TrackingConditionBindings;
		}

		if (!bChanged) continue;
		FStateTreeCompilerLog Log;
		FStateTreeCompiler Compiler(Log);
		if (!Compiler.Compile(*Tree))
		{
			Log.DumpToLog(LogTemp);
			AddError(FString::Printf(TEXT("Tracking migration failed to compile %s; this asset was not saved"), *Tree->GetPathName()));
			return false;
		}
		Tree->MarkPackageDirty();
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		const FString Filename = FPackageName::LongPackageNameToFilename(Tree->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		if (!TestTrue(TEXT("Save migrated canonical tracking tree"), UPackage::SavePackage(Tree->GetOutermost(), Tree, *Filename, SaveArgs))) return false;
		AddInfo(FString::Printf(TEXT("Installed tracking condition bindings in %s"), *Tree->GetPathName()));
	}
	TestTrue(TEXT("Configured drone graphs contain tracking condition bindings"), TrackingConditionBindings > 0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyAttackStateBindingsTest,
	"Outlier.Enemy.Attack.StateBindings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyAttackStateBindingsTest::RunTest(const FString& Parameters)
{
	const bool bInstall = FParse::Param(FCommandLine::Get(), TEXT("OutlierInstallAttackState"));
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	Registry.ScanPathsSynchronous({ TrackingTreeFolder });
	TArray<FAssetData> Assets;
	Registry.GetAssetsByClass(UStateTree::StaticClass()->GetClassPathName(), Assets, true);

	auto IsGunTree = [](const UStateTree& Tree)
	{
		return FPackageName::GetLongPackagePath(Tree.GetOutermost()->GetName()) == TrackingTreeFolder
			&& (Tree.GetFName() == TEXT("ST_VECDroneBattle_Gun") || Tree.GetFName() == TEXT("ST_VECDroneDirectCombat_Gun"));
	};
	auto CollectTasks = [](const UStateTreeEditorData& Data, TArray<const FStateTreeEditorNode*>& Tasks)
	{
		for (const FStateTreeEditorNode& Node : Data.GlobalTasks) Tasks.Add(&Node);
		TArray<UStateTreeState*> States;
		for (UStateTreeState* Root : Data.SubTrees) CollectTrackingStates(Root, States);
		for (const UStateTreeState* State : States)
		{
			Tasks.Add(&State->SingleTask);
			for (const FStateTreeEditorNode& Node : State->Tasks) Tasks.Add(&Node);
		}
	};

	UStateTree* FireCycle = LoadObject<UStateTree>(nullptr,
		TEXT("/Game/Blueprints/AI/VECDrone/StateTree/ST_EnemyFireCycle.ST_EnemyFireCycle"));
	if (!TestNotNull(TEXT("Canonical AI FireCycle loads"), FireCycle)) return false;
	UStateTreeEditorData* Data = Cast<UStateTreeEditorData>(FireCycle->EditorData);
	if (!TestNotNull(TEXT("AI FireCycle editor graph exists"), Data)) return false;

	// 이름이 같은 에셋을 추정해 수정하지 않는다. 실제 병렬 연결과 모든 등록된 참조자를 먼저 확인한다.
	// 빙의/터렛 등 다른 그래프가 공유하거나 동적 override가 있으면 저장 없이 명시적인 분리를 요구한다.
	int32 GunParallelReferences = 0;
	for (const FAssetData& Asset : Assets)
	{
		UStateTree* Referrer = Cast<UStateTree>(Asset.GetAsset());
		if (!Referrer || Referrer == FireCycle) continue;
		const UStateTreeEditorData* ReferrerData = Cast<UStateTreeEditorData>(Referrer->EditorData);
		if (!ReferrerData) continue;
		TArray<UStateTreeState*> States;
		for (UStateTreeState* Root : ReferrerData->SubTrees) CollectTrackingStates(Root, States);
		for (const UStateTreeState* State : States)
		{
			if (State->LinkedAsset == FireCycle)
			{
				AddError(FString::Printf(TEXT("FireCycle has unsupported linked-state referrer %s; inspect shared attack policy before migration"), *Referrer->GetPathName()));
				return false;
			}
		}
		TArray<const FStateTreeEditorNode*> Tasks;
		CollectTasks(*ReferrerData, Tasks);
		for (const FStateTreeEditorNode* Node : Tasks)
		{
			const auto* Parallel = Node->Node.GetPtr<FStateTreeRunParallelStateTreeTask>();
			const auto* Instance = Node->Instance.GetPtr<FStateTreeRunParallelStateTreeTaskInstanceData>();
			if (!Parallel || !Parallel->bTaskEnabled || !Instance || Instance->StateTree.GetStateTree() != FireCycle) continue;
			AddInfo(FString::Printf(TEXT("AI FireCycle parallel reference: %s task=%s"), *Referrer->GetPathName(), *Node->GetName().ToString()));
			if (!IsGunTree(*Referrer))
			{
				AddError(TEXT("FireCycle is shared with a noncanonical/non-gun tree; split attack policy before migration"));
				return false;
			}
			const FStructProperty* OverrideProperty = FindFProperty<FStructProperty>(FStateTreeRunParallelStateTreeTask::StaticStruct(), TEXT("StateTreeOverrideTag"));
			if (!TestNotNull(TEXT("Parallel override tag is inspectable"), OverrideProperty)) return false;
			const FGameplayTag* OverrideTag = OverrideProperty->ContainerPtrToValuePtr<FGameplayTag>(Parallel);
			if (!TestFalse(TEXT("FireCycle parallel reference does not dynamically override its tree"), OverrideTag->IsValid())) return false;
			++GunParallelReferences;
		}
	}
	if (!TestTrue(TEXT("Canonical gun graph really runs AI FireCycle in parallel"), GunParallelReferences > 0)) return false;

	TSet<FGuid> SyncIDs;
	TSet<FGuid> TaskIDs;
	TArray<const FStateTreeEditorNode*> Tasks;
	CollectTasks(*Data, Tasks);
	for (const FStateTreeEditorNode* Node : Tasks)
	{
		CollectSyncID(*Node, SyncIDs);
		TaskIDs.Add(Node->ID);
		const UScriptStruct* Type = Node->Node.GetScriptStruct();
		if (Type && (Type->GetName().Contains(TEXT("Possessed")) || Type->GetFName() == TEXT("EnemyAttackLocationTask")))
		{
			AddError(TEXT("AI FireCycle contains possessed/location attack policy; separate it before migrating target tracking"));
			return false;
		}
	}
	TMap<FGuid, const FStateTreeEditorNode*> Conditions;
	TArray<UStateTreeState*> States;
	for (UStateTreeState* Root : Data->SubTrees) CollectTrackingStates(Root, States);
	for (const UStateTreeState* State : States)
	{
		for (const FStateTreeEditorNode& Node : State->EnterConditions) Conditions.Add(Node.ID, &Node);
		for (const FStateTreeTransition& Transition : State->Transitions)
			for (const FStateTreeEditorNode& Node : Transition.Conditions) Conditions.Add(Node.ID, &Node);
	}

	TArray<int32> VisibilityConditions;
	const auto& Bindings = Data->EditorBindings.GetBindings();
	for (int32 Index = 0; Index < Bindings.Num(); ++Index)
	{
		const FStateTreePropertyPathBinding& Binding = Bindings[Index];
		if (!SyncIDs.Contains(Binding.GetSourcePath().GetStructID())) continue;
		if (Binding.GetSourcePath().ToString() != TEXT("bPlayerCurrentlyVisible")) continue;
		const FStateTreeEditorNode* const* Condition = Conditions.Find(Binding.GetTargetPath().GetStructID());
		AddInfo(FString::Printf(TEXT("AI FireCycle visibility binding: %s -> %s (%s)"),
			*Binding.GetSourcePath().ToString(), *Binding.GetTargetPath().ToString(), Condition ? TEXT("condition") : TEXT("task/parameter; preserved")));
		if (!Condition)
		{
			if (!TestTrue(TEXT("Visibility input belongs directly to a task, not ambiguous intermediate parameters"), TaskIDs.Contains(Binding.GetTargetPath().GetStructID()))) return false;
			continue;
		}
		if (!TestTrue(TEXT("Visibility gate is an explicit bool comparison"),
			(*Condition)->Node.GetScriptStruct() == FStateTreeCompareBoolCondition::StaticStruct())) return false;
		VisibilityConditions.Add(Index);
	}

	// 진단이 끝난 뒤에만 명시적으로 허용한 조건 바인딩을 변경한다. Task/파라미터 입력은 그대로 둔다.
	if (!bInstall)
	{
		TestEqual(TEXT("AI FireCycle conditions allow tracking grace (migrate with -OutlierInstallAttackState after reviewing diagnostics)"), VisibilityConditions.Num(), 0);
		return !HasAnyErrors();
	}
	if (VisibilityConditions.IsEmpty()) return true;
	if (!TestNotNull(TEXT("Sync exposes trackable-target output"), FindFProperty<FBoolProperty>(FEnemyStateTreeSyncTaskInstanceData::StaticStruct(), TEXT("bHasTrackableTarget")))) return false;
	if (!TestFalse(TEXT("Attack migration requires a clean FireCycle package"), FireCycle->GetOutermost()->IsDirty())) return false;
	FireCycle->Modify();
	Data->Modify();
	for (const int32 Index : VisibilityConditions)
		Data->EditorBindings.GetMutableBindings()[Index].GetMutableSourcePath().FromString(TEXT("bHasTrackableTarget"));
	FStateTreeCompilerLog Log;
	FStateTreeCompiler Compiler(Log);
	if (!Compiler.Compile(*FireCycle))
	{
		Log.DumpToLog(LogTemp);
		AddError(TEXT("AI FireCycle migration did not compile; package was not saved"));
		return false;
	}
	FireCycle->MarkPackageDirty();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	const FString Filename = FPackageName::LongPackageNameToFilename(FireCycle->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
	return TestTrue(TEXT("Save only migrated canonical AI FireCycle"), UPackage::SavePackage(FireCycle->GetOutermost(), FireCycle, *Filename, SaveArgs));
}

#endif
