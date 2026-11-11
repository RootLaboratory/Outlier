#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/EnemyStateTreeAttackTasks.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
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
void CollectPreparationStates(UStateTreeState* State, TArray<UStateTreeState*>& States)
{
	if (!State) return;
	States.Add(State);
	for (UStateTreeState* Child : State->Children) CollectPreparationStates(Child, States);
}

bool ValidatePreparationReferrers(FAutomationTestBase& Test, UStateTree* FireCycle, const TArray<UStateTree*>& GunTrees)
{
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	Registry.ScanPathsSynchronous({TEXT("/Game/Blueprints/AI/VECDrone/StateTree")});
	TArray<FAssetData> Assets;
	Registry.GetAssetsByClass(UStateTree::StaticClass()->GetClassPathName(), Assets, true);
	for (const FAssetData& Asset : Assets)
	{
		auto* Referrer = Cast<UStateTree>(Asset.GetAsset());
		if (!Referrer || Referrer == FireCycle) continue;
		auto* Data = Cast<UStateTreeEditorData>(Referrer->EditorData);
		if (!Data) continue;
		TArray<UStateTreeState*> States;
		for (UStateTreeState* Root : Data->SubTrees) CollectPreparationStates(Root, States);
		TArray<const FStateTreeEditorNode*> Tasks;
		for (const FStateTreeEditorNode& Node : Data->GlobalTasks) Tasks.Add(&Node);
		for (const UStateTreeState* State : States)
		{
			if (State->LinkedAsset == FireCycle)
			{
				Test.AddError(TEXT("Gun FireCycle has an unreviewed linked-state reference: ") + Referrer->GetPathName());
				return false;
			}
			Tasks.Add(&State->SingleTask);
			for (const FStateTreeEditorNode& Node : State->Tasks) Tasks.Add(&Node);
		}
		for (const FStateTreeEditorNode* Node : Tasks)
		{
			const auto* Parallel = Node->Node.GetPtr<FStateTreeRunParallelStateTreeTask>();
			const auto* Instance = Node->Instance.GetPtr<FStateTreeRunParallelStateTreeTaskInstanceData>();
			if (!Parallel || !Parallel->bTaskEnabled || !Instance || Instance->StateTree.GetStateTree() != FireCycle) continue;
			Test.AddInfo(TEXT("[GunPreparationAsset] FireCycle referrer: ") + Referrer->GetPathName());
			if (!GunTrees.Contains(Referrer))
			{
				Test.AddError(TEXT("FireCycle is shared with a non-Gun graph; duration migration requires a scoped asset: ") + Referrer->GetPathName());
				return false;
			}
		}
	}
	return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyAttackPreparationAssetTest,
	"Outlier.Enemy.AttackPreparation.SavedGunTelegraph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyAttackPreparationAssetTest::RunTest(const FString& Parameters)
{
	const bool bInstall = FParse::Param(FCommandLine::Get(), TEXT("OutlierInstallGunAttackPreparation"));
	TArray<UStateTree*> Changed;
	TArray<UStateTree*> Trees;
	TArray<UStateTree*> GunTrees;
	int32 TelegraphCount = 0;
	for (const TCHAR* Name : {TEXT("ST_VECDroneBattle_Gun"), TEXT("ST_VECDroneDirectCombat_Gun")})
	{
		const FString Path = FString(TEXT("/Game/Blueprints/AI/VECDrone/StateTree/")) + Name + TEXT(".") + Name;
		auto* Tree = LoadObject<UStateTree>(nullptr, *Path);
		if (!TestNotNull(TEXT("Saved Gun tree loads"), Tree)) return false;
		Trees.Add(Tree);
		GunTrees.Add(Tree);
	}
	for (int32 Index = 0; Index < Trees.Num(); ++Index)
	{
		UStateTree* Tree = Trees[Index];
		if (!GunTrees.Contains(Tree) && !ValidatePreparationReferrers(*this, Tree, GunTrees)) return false;
		auto* Data = Cast<UStateTreeEditorData>(Tree->EditorData);
		if (!TestNotNull(TEXT("Authored Gun graph exists"), Data)) return false;
		if (bInstall && !TestFalse(TEXT("Migration cannot overwrite preexisting dirty packages"), Tree->GetOutermost()->IsDirty())) return false;
		TArray<UStateTreeState*> States;
		for (UStateTreeState* Root : Data->SubTrees) CollectPreparationStates(Root, States);
		TArray<FStateTreeEditorNode*> Tasks;
		for (FStateTreeEditorNode& Node : Data->GlobalTasks) Tasks.Add(&Node);
		for (UStateTreeState* State : States)
		{
			Tasks.Add(&State->SingleTask);
			for (FStateTreeEditorNode& Node : State->Tasks) Tasks.Add(&Node);
		}
		for (FStateTreeEditorNode* Task : Tasks)
		{
			FStateTreeEditorNode& Node = *Task;
			const auto* Parallel = Node.Node.GetPtr<FStateTreeRunParallelStateTreeTask>();
			const auto* ParallelData = Node.Instance.GetPtr<FStateTreeRunParallelStateTreeTaskInstanceData>();
			if (Parallel && Parallel->bTaskEnabled && ParallelData && ParallelData->StateTree.GetStateTree())
			{
				UStateTree* Linked = LoadObject<UStateTree>(nullptr, *ParallelData->StateTree.GetStateTree()->GetPathName());
				if (!TestNotNull(TEXT("Actual parallel graph loads as an authored asset"), Linked)) return false;
				AddInfo(TEXT("[GunPreparationAsset] Actual parallel reference: ") + Tree->GetPathName() + TEXT(" -> ") + Linked->GetPathName());
				if (!TestEqual(TEXT("Gun preparation follows its canonical actual FireCycle"), Linked->GetPathName(),
					FString(TEXT("/Game/Blueprints/AI/VECDrone/StateTree/ST_EnemyFireCycle.ST_EnemyFireCycle")))) return false;
				const FStructProperty* Override = FindFProperty<FStructProperty>(FStateTreeRunParallelStateTreeTask::StaticStruct(), TEXT("StateTreeOverrideTag"));
				if (!TestNotNull(TEXT("Parallel override policy is inspectable"), Override)) return false;
				if (!TestFalse(TEXT("Gun FireCycle has no dynamic graph override"),
					Override->ContainerPtrToValuePtr<FGameplayTag>(Parallel)->IsValid())) return false;
				Trees.AddUnique(Linked);
			}
			{
				auto* Wait = Node.Instance.GetMutablePtr<FEnemyAttackPhaseWaitTaskInstanceData>();
				if (!Wait || Wait->Phase != EEnemyAttackPhase::Telegraph) continue;
				++TelegraphCount;
				AddInfo(FString::Printf(TEXT("[GunPreparationAsset] Tree=%s State=%s Min=%.3f Max=%.3f"),
					*Tree->GetName(), *Node.GetName().ToString(), Wait->MinDuration, Wait->MaxDuration));
				// 바인딩이 있으면 Task 기본값은 실제 설정 소유자가 아니다. 임의로 덮어쓰지 않고 드러낸다.
				for (const FStateTreePropertyPathBinding& Binding : Data->EditorBindings.GetBindings())
				{
					const FString Target = Binding.GetTargetPath().ToString();
					if (Binding.GetTargetPath().GetStructID() == Node.ID
						&& (Target == TEXT("MinDuration") || Target == TEXT("MaxDuration")))
					{
						AddError(TEXT("Gun telegraph duration has a conflicting binding: ") + Target);
						return false;
					}
				}
				if (bInstall && (!FMath::IsNearlyEqual(Wait->MinDuration, 0.25f)
					|| !FMath::IsNearlyEqual(Wait->MaxDuration, 0.25f)))
				{
					Wait->MinDuration = Wait->MaxDuration = 0.25f;
					Changed.AddUnique(Tree);
				}
				TestEqual(TEXT("Saved Gun first telegraph minimum is 0.25 seconds"), Wait->MinDuration, 0.25f);
				TestEqual(TEXT("Saved Gun first telegraph maximum is 0.25 seconds"), Wait->MaxDuration, 0.25f);
			}
		}
		TestTrue(TEXT("Saved Gun tree has compiled runtime data"), Tree->IsReadyToRun());
	}
	if (!TestTrue(TEXT("At least one authored Gun telegraph was inspected"), TelegraphCount > 0) || HasAnyErrors()) return false;
	// 전부 검증·컴파일한 뒤 프로젝트 Content만 저장한다. 기본 테스트 실행은 읽기 전용이다.
	for (UStateTree* Tree : Changed)
	{
		FStateTreeCompilerLog Log;
		FStateTreeCompiler Compiler(Log);
		if (!Compiler.Compile(*Tree))
		{
			Log.DumpToLog(LogTemp);
			AddError(TEXT("Gun preparation migration failed compilation; no package was saved"));
			return false;
		}
	}
	for (UStateTree* Tree : Changed)
	{
		Tree->MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		const FString Filename = FPackageName::LongPackageNameToFilename(Tree->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		if (!TestTrue(TEXT("Migration destination stays inside project Content"),
			FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Filename), FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir())))) return false;
		if (!TestTrue(TEXT("Validated Gun preparation saves to project Content"),
			UPackage::SavePackage(Tree->GetOutermost(), Tree, *Filename, Args))) return false;
		AddInfo(TEXT("Installed Gun preparation: ") + Filename);
	}
	return !HasAnyErrors();
}

#endif
