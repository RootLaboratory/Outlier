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

	UPROPERTY(EditAnywhere, Category = Output)
	bool bHasSharedTargetContact = false;

	UPROPERTY(EditAnywhere, Category = Output)
	FVector SharedTargetLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = Output)
	TObjectPtr<AAIController> AIController = nullptr;

	// AIController가 Sight 대상 중 우선순위와 거리를 기준으로 선택한 현재 전투 대상.
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
