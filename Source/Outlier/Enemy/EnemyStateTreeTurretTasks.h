#pragma once

#include "CoreMinimal.h"
#include "Enemy/AutoTurret.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeExecutionTypes.h"
#include "StateTreeTaskBase.h"
#include "EnemyStateTreeTurretTasks.generated.h"

USTRUCT()
struct FEnemyDeployTurretTaskInstanceData
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AAutoTurret> Turret = nullptr;
};

USTRUCT(meta = (DisplayName = "Enemy Deploy Turret", Category = "Enemy|Turret"))
struct OUTLIER_API FEnemyDeployTurretTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FEnemyDeployTurretTaskInstanceData;
	FEnemyDeployTurretTask();
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
};

USTRUCT()
struct FEnemyRotateTurretHeadTaskInstanceData
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AAutoTurret> Turret = nullptr;
	UPROPERTY(EditAnywhere, Category = Input, meta = (Optional))
	TObjectPtr<AActor> TargetActor = nullptr;
	UPROPERTY(EditAnywhere, Category = Input)
	FVector TargetLocation = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, Category = Parameter)
	bool bUseTargetActor = true;
	UPROPERTY(EditAnywhere, Category = Parameter)
	bool bAttackRotation = false;
	UPROPERTY(EditAnywhere, Category = Parameter)
	bool bUseSearchPitch = true;
	UPROPERTY(EditAnywhere, Category = Parameter)
	bool bFinishWhenAligned = true;
};

USTRUCT(meta = (DisplayName = "Enemy Rotate Turret Head", Category = "Enemy|Turret"))
struct OUTLIER_API FEnemyRotateTurretHeadTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FEnemyRotateTurretHeadTaskInstanceData;
	FEnemyRotateTurretHeadTask();
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
};

USTRUCT()
struct FEnemyTurretSearchTaskInstanceData
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AAutoTurret> Turret = nullptr;
	UPROPERTY(EditAnywhere, Category = Input, meta = (Optional))
	FVector SearchCenter = FVector::ZeroVector;
	float ElapsedTime = 0.0f;
	FRotator CenterRotation = FRotator::ZeroRotator;
};

USTRUCT(meta = (DisplayName = "Enemy Turret Search", Category = "Enemy|Turret"))
struct OUTLIER_API FEnemyTurretSearchTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FEnemyTurretSearchTaskInstanceData;
	FEnemyTurretSearchTask();
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
};

enum class EEnemyTurretBurstTaskPhase : uint8
{
	TelegraphAndTracking,
	BurstFiring,
	Cooling
};

USTRUCT()
struct FEnemyTurretBurstFireTaskInstanceData
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AAutoTurret> Turret = nullptr;
	UPROPERTY(EditAnywhere, Category = Input, meta = (Optional))
	TObjectPtr<AActor> TargetActor = nullptr;
	float ElapsedTime = 0.0f;
	FVector LastValidAimLocation = FVector::ZeroVector;
	EEnemyTurretBurstTaskPhase Phase = EEnemyTurretBurstTaskPhase::TelegraphAndTracking;
	bool bAttackStarted = false;
};

USTRUCT(meta = (DisplayName = "Enemy Turret Burst Fire", Category = "Enemy|Turret"))
struct OUTLIER_API FEnemyTurretBurstFireTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FEnemyTurretBurstFireTaskInstanceData;
	FEnemyTurretBurstFireTask();
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

enum class EEnemyTurretSniperTaskPhase : uint8
{
	LaserTelegraphAndTracking,
	BeamPresentation,
	Cooling
};

USTRUCT()
struct FEnemyTurretSniperShotTaskInstanceData
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AAutoTurret> Turret = nullptr;
	UPROPERTY(EditAnywhere, Category = Input, meta = (Optional))
	TObjectPtr<AActor> TargetActor = nullptr;
	float ElapsedTime = 0.0f;
	FVector LockedAimLocation = FVector::ZeroVector;
	EEnemyTurretSniperTaskPhase Phase = EEnemyTurretSniperTaskPhase::LaserTelegraphAndTracking;
	bool bAttackStarted = false;
};

USTRUCT(meta = (DisplayName = "Enemy Turret Sniper Shot", Category = "Enemy|Turret"))
struct OUTLIER_API FEnemyTurretSniperShotTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FEnemyTurretSniperShotTaskInstanceData;
	FEnemyTurretSniperShotTask();
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

USTRUCT()
struct FEnemyRecoverTurretImpactOffsetTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AAutoTurret> Turret = nullptr;
};

// StateTree 비교 버전에서만 사용하며, 진입 시 공격 State를 끝내고 머리 반동을 복구한다.
USTRUCT(meta = (DisplayName = "Enemy Recover Turret Impact Offset", Category = "Enemy|Turret"))
struct OUTLIER_API FEnemyRecoverTurretImpactOffsetTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEnemyRecoverTurretImpactOffsetTaskInstanceData;

	FEnemyRecoverTurretImpactOffsetTask();
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};
