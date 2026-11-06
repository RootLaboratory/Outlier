#pragma once

#include "CoreMinimal.h"
#include "EnemyBase.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeExecutionTypes.h"
#include "StateTreeTaskBase.h"
#include "EnemyStateTreeSyncTask.generated.h"

class AAIController;
class AActor;

USTRUCT()
struct FEnemyStateTreeSyncTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Enemy = nullptr;

	UPROPERTY(EditAnywhere, Category = Output)
	EEnemyCombatState CombatState = EEnemyCombatState::NonCombat;

	// StateTree 바인딩 호환성을 위해 bool 
	UPROPERTY(EditAnywhere, Category = Output)
	bool bIsPossessed = false;

	UPROPERTY(EditAnywhere, Category = Output)
	bool bPossessionInProgress = false;

	UPROPERTY(EditAnywhere, Category = Output)
	bool bPossessedAttackHeld = false;

	UPROPERTY(EditAnywhere, Category = Output)
	bool bPossessedAttackQueued = false;

	UPROPERTY(EditAnywhere, Category = Output)
	bool bHasPossessedAttackRequest = false;

	// StateTree 바인딩 호환성을 위해 bool 
	UPROPERTY(EditAnywhere, Category = Output)
	bool bPlayerCurrentlyVisible = false;

	// 이동/전투 분기용. 실제 Sight 출력과 분리해 유예 중에도 추적 분기를 유지한다.
	UPROPERTY(EditAnywhere, Category = Output)
	bool bHasTrackableTarget = false;

	UPROPERTY(EditAnywhere, Category = Output)
	bool bHasSharedTargetContact = false;
	// 접촉 활성 여부와 별개다. 확인한 공유 정보로 추적을 반복하지 않는다.
	UPROPERTY(EditAnywhere, Category = Output)
	bool bHasFreshSharedTargetLocation = false;

	UPROPERTY(EditAnywhere, Category = Output)
	FVector SharedTargetLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = Output)
	TObjectPtr<AAIController> AIController = nullptr;

	// 직접 감지한 대상 또는 만료 전 유예 추적 대상. 실제 시야 여부는 별도 출력한다.
	// 이동/바라보기/공격 Task의 TargetActor 입력은 이 출력 하나에 바인딩한다.
	UPROPERTY(EditAnywhere, Category = Output)
	TObjectPtr<AActor> TargetActor = nullptr;

	UPROPERTY(EditAnywhere, Category = Output)
	FVector LastKnownPlayerLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = Output)
	FVector PatternStartPlayerLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = Output)
	EEnemyType EnemyType = EEnemyType::Gun;

	UPROPERTY(EditAnywhere, Category = Output)
	EEnemyNonCombatBehavior NonCombatBehavior = EEnemyNonCombatBehavior::Stationary;

	UPROPERTY(EditAnywhere, Category = Output)
	FVector PatrolPointA = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = Output)
	FVector PatrolPointB = FVector::ZeroVector;

	// 지식 출력(TargetActor)과 분리된 행동 의도. 실제 조사 전환은 소비 Task가 담당한다.
	UPROPERTY(EditAnywhere, Category = Output)
	EEnemyPerceptionGoalType PerceptionGoalType = EEnemyPerceptionGoalType::None;

	UPROPERTY(EditAnywhere, Category = Output)
	FVector PerceptionGoalLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = Output)
	TObjectPtr<AActor> PerceptionGoalTarget = nullptr;

	UPROPERTY(EditAnywhere, Category = Output)
	bool bNoiseInvestigationActive = false;
	UPROPERTY(EditAnywhere, Category = Output)
	bool bNoiseSearchActive = false;
};

USTRUCT(meta = (
	DisplayName = "Enemy State Sync",
	Category = "Enemy"
	))
struct OUTLIER_API FEnemyStateTreeSyncTask
	: public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemyStateTreeSyncTaskInstanceData;

	FEnemyStateTreeSyncTask();

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

	virtual EStateTreeRunStatus Tick(
		FStateTreeExecutionContext& Context,
		float DeltaTime) const override;

private:
	void SyncFromEnemy(FInstanceDataType& InstanceData) const;
};
