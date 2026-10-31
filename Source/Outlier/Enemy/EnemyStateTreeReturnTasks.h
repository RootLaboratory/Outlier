#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "EnemyStateTreeReturnTasks.generated.h"

class AEnemyBase;

USTRUCT()
struct FEnemySelectReturnHomeTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Enemy = nullptr;

	UPROPERTY(EditAnywhere, Category = Output)
	FVector Destination = FVector::ZeroVector;

	float RetryElapsed = 0.0f;
};

// 실패한 이동도 이 상태로 돌아와 기존 경로 탐색 예산을 매 프레임 소모하지 않는다.
USTRUCT(meta = (DisplayName = "Enemy Select Return Home", Category = "Enemy|Movement"))
struct OUTLIER_API FEnemySelectReturnHomeTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FEnemySelectReturnHomeTaskInstanceData;
	FEnemySelectReturnHomeTask();
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
};

USTRUCT()
struct FEnemyCompleteReturnHomeTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Enemy = nullptr;
};

// Fly의 도착 허용 반경 대신 원래 등록된 Room 내부의 실제 Capsule 위치를 최종 판정한다.
USTRUCT(meta = (DisplayName = "Enemy Complete Return Home", Category = "Enemy|Movement"))
struct OUTLIER_API FEnemyCompleteReturnHomeTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FEnemyCompleteReturnHomeTaskInstanceData;
	FEnemyCompleteReturnHomeTask();
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};
