#pragma once

#include "CoreMinimal.h"
#include "Enemy/SelfDestructDrone.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeExecutionTypes.h"
#include "StateTreeTaskBase.h"
#include "EnemyStateTreeSelfDestructTasks.generated.h"

class UCapsuleComponent;
class AEnemyAIController;

UENUM(BlueprintType)
enum class EEnemyChargeEligibility : uint8
{
	Ready, Invalid, NotInCombat, NoVisibleTarget, OutOfRange, BlockedPath, AlreadyCommitted
};

namespace EnemyCharge
{
	OUTLIER_API bool IsWithinChargeRange(const FVector& Start, const FVector& Target, float Distance);
	OUTLIER_API bool IsStraightPathClear(UWorld& World, const UCapsuleComponent& Capsule, const AActor& Target, float Clearance);
	OUTLIER_API EEnemyChargeEligibility Assess(const ASelfDestructDrone* Drone, const AEnemyAIController* Controller,
		const AActor* Target, float Distance, float Clearance);
}

USTRUCT()
struct FEnemySelfDestructAssessmentTaskInstanceData
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Drone = nullptr;
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.01", Units = "s"))
	float UpdateInterval = 0.2f;
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "1.0", ClampMax = "500.0", Units = "cm"))
	float ChargeEntryDistance = 500.0f;
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0", Units = "cm"))
	float ChargeClearance = 5.0f;
	UPROPERTY(EditAnywhere, Category = Output)
	TObjectPtr<AActor> TargetActor = nullptr;
	UPROPERTY(EditAnywhere, Category = Output)
	bool bHasVisibleTarget = false;
	UPROPERTY(EditAnywhere, Category = Output)
	bool bCanCharge = false;
	UPROPERTY(EditAnywhere, Category = Output)
	float HorizontalDistance = 0.0f;
	UPROPERTY(EditAnywhere, Category = Output)
	float SpatialDistance = 0.0f;
	UPROPERTY(EditAnywhere, Category = Output)
	EEnemyChargeEligibility Eligibility = EEnemyChargeEligibility::Invalid;
	TWeakObjectPtr<ASelfDestructDrone> CachedDrone;
	TWeakObjectPtr<AEnemyAIController> CachedController;
	float Elapsed = 0.0f;
};

// DirectPursuit 부모에 둔다. 자식 이동/재시도 중에도 돌진 가능 여부를 갱신하되 완료 판정에는 참여하지 않는다.
USTRUCT(meta = (DisplayName = "Enemy Self Destruct Assessment", Category = "Enemy|Self Destruct"))
struct OUTLIER_API FEnemySelfDestructAssessmentTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FEnemySelfDestructAssessmentTaskInstanceData;
	FEnemySelfDestructAssessmentTask();
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};

USTRUCT()
struct FEnemyChargeLastDirectionTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Drone = nullptr;

	UPROPERTY(EditAnywhere, Category = Input)
	TObjectPtr<AActor> TargetActor = nullptr;

	// Assessment와 동일한 값을 사용한다. 전이와 실제 Commit 사이의 상태 변화를 다시 검사한다.
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "1.0", ClampMax = "500.0", Units = "cm"))
	float ChargeEntryDistance = 500.0f;
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0", Units = "cm"))
	float ChargeClearance = 5.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float TelegraphDuration = 2.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float ChargeSpeedMultiplier = 1.5f;

	// 기존 StateTree 저장값/바인딩 호환용이다. AI 돌진에는 두 거리 값을 적용하지 않는다.
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = Parameter, meta = (DeprecatedProperty,
		DeprecationMessage = "AI charge continues until collision or timeout; distance is ignored."))
	float MaxChargeDistance = 500.0f;

	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = Parameter, meta = (DeprecatedProperty,
		DeprecationMessage = "AI charge does not stop before the target; distance is ignored."))
	float TargetStopDistance = 100.0f;

	UPROPERTY(EditAnywhere, Category = Output)
	float ElapsedTime = 0.0f;

	bool bCompleted = false;
};

USTRUCT()
struct FEnemyBeginPossessedSelfDestructTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Drone = nullptr;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float TelegraphDuration = 2.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float ChargeSpeedMultiplier = 1.5f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float MaxChargeDistance = 500.0f;
};

// 빙의 공격 입력 시 플레이어의 현재 시선 방향을 고정하고 자폭 돌진을 시작한다.
USTRUCT(meta = (DisplayName = "Enemy Begin Possessed Self Destruct", Category = "Enemy|Self Destruct"))
struct OUTLIER_API FEnemyBeginPossessedSelfDestructTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemyBeginPossessedSelfDestructTaskInstanceData;

	FEnemyBeginPossessedSelfDestructTask();

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

USTRUCT()
struct FEnemyUpdatePossessedSelfDestructTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Drone = nullptr;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float ChargeDuration = 2.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float ChargeSpeedMultiplier = 1.5f;

	UPROPERTY(EditAnywhere, Category = Output)
	float ElapsedTime = 0.0f;

	bool bCompleted = false;
};

// 고정된 방향으로 돌진을 갱신하고, 충격 방향 보정과 자폭 카운트다운을 유지한다.
USTRUCT(meta = (DisplayName = "Enemy Update Possessed Self Destruct", Category = "Enemy|Self Destruct"))
struct OUTLIER_API FEnemyUpdatePossessedSelfDestructTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemyUpdatePossessedSelfDestructTaskInstanceData;

	FEnemyUpdatePossessedSelfDestructTask();

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
	virtual void ExitState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

// 진입 시 저장한 방향으로 2초 동안 돌진하며, 외부 충격은 가산 속도로만 반영한다.
USTRUCT(meta = (DisplayName = "Enemy Charge Last Direction", Category = "Enemy|Self Destruct"))
struct OUTLIER_API FEnemyChargeLastDirectionTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemyChargeLastDirectionTaskInstanceData;

	FEnemyChargeLastDirectionTask();

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
	virtual void ExitState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

USTRUCT()
struct FEnemyBombDetonateTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Drone = nullptr;
};

// 폭발 계산을 중복하지 않고 ASelfDestructDrone의 기존 서버 권한 사망 경로만 실행한다.
USTRUCT(meta = (DisplayName = "Enemy Bomb Detonate", Category = "Enemy|Self Destruct"))
struct OUTLIER_API FEnemyBombDetonateTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemyBombDetonateTaskInstanceData;

	FEnemyBombDetonateTask();

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};
