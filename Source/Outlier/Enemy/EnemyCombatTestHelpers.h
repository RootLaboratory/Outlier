#pragma once

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Enemy/EnemyStateTreeComponent.h"
#include "StateTree.h"
#include "StateTreeExecutionTypes.h"
#include "StateTreeInstanceData.h"
#include "Tasks/StateTreeRunParallelStateTreeTask.h"
#include "UObject/UnrealType.h"

// 저장된 에셋과 실행 중인 인스턴스를 읽기만 한다. 테스트용 Tree나 강제 완료로 연결 오류를 숨기지 않는다.
namespace EnemyCombatTesting
{
template<typename NodeType>
const typename NodeType::FInstanceDataType* GetDefaultData(const UStateTree& Tree, const NodeType* Node)
{
	if (!Node || !Node->InstanceTemplateIndex.IsValid()) return nullptr;
	// Condition은 Task와 달리 Shared 저장소에 컴파일된다. 같은 인덱스의 다른 Task를 읽지 않는다.
	const TSharedPtr<FStateTreeInstanceData> Shared = Node->InstanceDataHandle.GetSource() == EStateTreeDataSourceType::SharedInstanceData
		? Tree.GetSharedInstanceData() : nullptr;
	const FStateTreeInstanceData& Data = Shared ? *Shared : Tree.GetDefaultInstanceData();
	if (!Data.IsValidIndex(Node->InstanceTemplateIndex.Get()) || Data.IsObject(Node->InstanceTemplateIndex.Get())) return nullptr;
	return Data.GetStruct(Node->InstanceTemplateIndex.Get())
		.template GetPtr<const typename NodeType::FInstanceDataType>();
}

inline FString StatePath(const UStateTree& Tree, const FCompactStateTreeState* State)
{
	FString Path;
	while (State)
	{
		Path = State->Name.ToString() + (Path.IsEmpty() ? TEXT("") : TEXT(".") + Path);
		State = Tree.GetStateFromHandle(State->Parent);
	}
	return Tree.GetPathName() + TEXT(" / ") + Path;
}

inline void CollectTrees(const UStateTree* Tree, const UEnemyStateTreeComponent& Component,
	TArray<const UStateTree*>& Trees)
{
	if (!Tree || Trees.Contains(Tree)) return;
	Trees.Add(Tree);
	for (const FCompactStateTreeState& State : Tree->GetStates())
	{
		const FStateTreeReference* Override = Component.FindLinkedStateTreeOverride(State.Tag);
		CollectTrees(Override ? Override->GetStateTree() : State.LinkedAsset.Get(), Component, Trees);
	}
	// 병렬 Task는 별도 인스턴스를 소유한다. Global Task와 State Task 모두 같은 노드 저장소에서 찾는다.
	for (int32 Index = 0; Index < Tree->GetNodes().Num(); ++Index)
	{
		const auto* Parallel = Tree->GetNode(Index).GetPtr<const FStateTreeRunParallelStateTreeTask>();
		if (!Parallel || !Parallel->bTaskEnabled) continue;
		const auto* Data = GetDefaultData(*Tree, Parallel);
		const FStructProperty* TagProperty = FindFProperty<FStructProperty>(
			FStateTreeRunParallelStateTreeTask::StaticStruct(), TEXT("StateTreeOverrideTag"));
		const FStateTreeReference* Override = TagProperty ? Component.FindLinkedStateTreeOverride(
			*TagProperty->ContainerPtrToValuePtr<FGameplayTag>(Parallel)) : nullptr;
		CollectTrees(Override ? Override->GetStateTree() : Data ? Data->StateTree.GetStateTree() : nullptr, Component, Trees);
	}
}

struct FActiveTask
{
	const UStateTree* Tree = nullptr;
	const FCompactStateTreeState* State = nullptr;
	FConstStructView Node;
	FConstStructView Data;
};

struct FSnapshot
{
	TArray<FActiveTask> Tasks;
	TArray<FString> States;

	template<typename TaskType>
	const FActiveTask* FindTask() const
	{
		for (const FActiveTask& Task : Tasks)
			if (Task.Node.GetPtr<const TaskType>()) return &Task;
		return nullptr;
	}

	bool HasState(FName Name) const
	{
		for (const FString& State : States)
			if (State.EndsWith(TEXT(".") + Name.ToString()) || State.EndsWith(TEXT(" / ") + Name.ToString())) return true;
		return false;
	}

	FString Describe() const { return FString::Join(States, TEXT(" | ")); }
};

inline void ReadRunningInstance(const FStateTreeInstanceData& Data, FSnapshot& Snapshot, int32 Depth = 0)
{
	// 컴파일 시 순환 참조가 누락된 에셋도 테스트 관측 과정에서 무한 재귀하지 않게 한다.
	if (Depth > 16) return;
	const FStateTreeExecutionState* Execution = Data.GetExecutionState();
	if (!Execution || Execution->TreeRunStatus != EStateTreeRunStatus::Running) return;
	for (const FStateTreeExecutionFrame& Frame : Execution->ActiveFrames)
	{
		if (!Frame.StateTree) continue;
		for (int32 Index = 0; Index < Frame.ActiveStates.Num(); ++Index)
		{
			const auto* State = Frame.StateTree->GetStateFromHandle(Frame.ActiveStates[Index]);
			if (!State) continue;
			Snapshot.States.Add(StatePath(*Frame.StateTree, State));
			for (int32 TaskIndex = 0; TaskIndex < State->TasksNum; ++TaskIndex)
			{
				const FConstStructView Node = Frame.StateTree->GetNode(State->TasksBegin + TaskIndex);
				const auto* Task = Node.GetPtr<const FStateTreeTaskBase>();
				if (!Task || !Task->bTaskEnabled) continue;
				FConstStructView TaskData;
				const FStateTreeDataHandle Handle = Task->InstanceDataHandle;
				if (Handle.GetSource() == EStateTreeDataSourceType::ActiveInstanceData
					&& Frame.ActiveInstanceIndexBase.IsValid())
				{
					const int32 DataIndex = Frame.ActiveInstanceIndexBase.Get() + Handle.GetIndex();
					if (Data.IsValidIndex(DataIndex) && !Data.IsObject(DataIndex)) TaskData = Data.GetStruct(DataIndex);
				}
				Snapshot.Tasks.Add({Frame.StateTree, State, Node, TaskData});
			}
		}
	}
	for (int32 Index = 0; Index < Data.Num(); ++Index)
	{
		if (Data.IsObject(Index)) continue;
		const auto* Parallel = Data.GetStruct(Index).GetPtr<const FStateTreeRunParallelStateTreeTaskInstanceData>();
		if (Parallel && Parallel->RunningStateTree) ReadRunningInstance(Parallel->TreeInstanceData, Snapshot, Depth + 1);
	}
}

inline FSnapshot ReadComponent(const UEnemyStateTreeComponent& Component)
{
	FSnapshot Snapshot;
	// protected 멤버를 노출하는 런타임 API 대신 기존 Reflection으로 읽는다. 게임 동작은 변경하지 않는다.
	const FStructProperty* Property = FindFProperty<FStructProperty>(UStateTreeComponent::StaticClass(), TEXT("InstanceData"));
	if (Property) ReadRunningInstance(*Property->ContainerPtrToValuePtr<FStateTreeInstanceData>(&Component), Snapshot);
	return Snapshot;
}
}

#endif
