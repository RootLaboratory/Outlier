#pragma once

#include "CoreMinimal.h"
#include "EnemyBase.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeExecutionTypes.h"
#include "StateTreeTaskBase.h"
#include "EnemyStateTreeTacticalTasks.generated.h"

UENUM(BlueprintType)
enum class EEnemyOrbitDirection : uint8
{
	Left,
	Right
};

// RoomSubsystem에서 LKP 원형 슬롯을 요청하고 Move To/Maintain Facing에 바인딩할 좌표를 출력한다.
USTRUCT()
struct FEnemyRequestSearchRingSlotTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Enemy = nullptr;

	UPROPERTY(EditAnywhere, Category = Input)
	FVector SearchCenter = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float RingRadius = 1200.0f;

	UPROPERTY(EditAnywhere, Category = Parameter)
	float FlightHeightOffset = 300.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float ReassignmentDistance = 200.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "1.0"))
	float FloorTraceHalfHeight = 10000.0f;

	UPROPERTY(EditAnywhere, Category = Output)
	FVector Destination = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = Output)
	FVector FacingLocation = FVector::ZeroVector;
};

// 슬롯을 보유하는 동안 Running을 유지한다.
// 재감지 등으로 State를 이탈하면 ExitState에서 슬롯을 자동 반환한다.
USTRUCT(meta = (DisplayName = "Enemy Request Search Ring Slot", Category = "Enemy|Tactical"))
struct OUTLIER_API FEnemyRequestSearchRingSlotTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemyRequestSearchRingSlotTaskInstanceData;

	FEnemyRequestSearchRingSlotTask();

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

	virtual void ExitState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

USTRUCT()
struct FEnemyReleaseSearchRingSlotTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Enemy = nullptr;
};

USTRUCT(meta = (DisplayName = "Enemy Release Search Ring Slot", Category = "Enemy|Tactical"))
struct OUTLIER_API FEnemyReleaseSearchRingSlotTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemyReleaseSearchRingSlotTaskInstanceData;

	FEnemyReleaseSearchRingSlotTask();

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

// 현재 위치를 기준으로 수평 원의 다음 후보를 계산한다.
// 1차 구현은 바닥 Trace + 고정 높이를 사용하고, 향후 3D EQS가 이 위치 선택 단계만 대체한다.
USTRUCT()
struct FEnemySelectHorizontalOrbitLocationTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AEnemyBase> Enemy = nullptr;

	UPROPERTY(EditAnywhere, Category = Input)
	FVector OrbitCenter = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float OrbitRadius = 800.0f;

	UPROPERTY(EditAnywhere, Category = Parameter)
	float FlightHeightOffset = 300.0f;

	UPROPERTY(EditAnywhere, Category = Parameter)
	float AngularStepDegrees = 45.0f;

	UPROPERTY(EditAnywhere, Category = Parameter)
	EEnemyOrbitDirection OrbitDirection = EEnemyOrbitDirection::Left;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "1.0"))
	float FloorTraceHalfHeight = 10000.0f;

	UPROPERTY(EditAnywhere, Category = Output)
	FVector Destination = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = Output)
	FVector FacingLocation = FVector::ZeroVector;
};

USTRUCT(meta = (DisplayName = "Enemy Select Horizontal Orbit Location", Category = "Enemy|Tactical"))
struct OUTLIER_API FEnemySelectHorizontalOrbitLocationTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemySelectHorizontalOrbitLocationTaskInstanceData;

	FEnemySelectHorizontalOrbitLocationTask();

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

// 실제 플레이어 위치를 조회하지 않고 LKP 주변의 임의 좌표를 만들어 위협 사격에 사용한다.
USTRUCT()
struct FEnemySelectSuppressiveFireLocationTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Input)
	FVector LastKnownPlayerLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float SpreadRadius = 200.0f;

	UPROPERTY(EditAnywhere, Category = Output)
	FVector AttackLocation = FVector::ZeroVector;
};

USTRUCT(meta = (DisplayName = "Enemy Select Suppressive Fire Location", Category = "Enemy|Tactical"))
struct OUTLIER_API FEnemySelectSuppressiveFireLocationTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemySelectSuppressiveFireLocationTaskInstanceData;

	FEnemySelectSuppressiveFireLocationTask();

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};
