#include "Enemy/EnemyStateTreeSelfDestructTasks.h"

#include "Components/ActorComponent.h"
#include "Components/CapsuleComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "Enemy/EnemyAIController.h"
#include "GameFramework/Controller.h"
#include "HAL/IConsoleManager.h"
#include "Outlier.h"

namespace
{
	DEFINE_LOG_CATEGORY_STATIC(LogEnemyCharge, Log, All);

	void LogChargeEligibility(const TCHAR* Phase, const AEnemyBase* Drone, const AActor* Target,
		EEnemyChargeEligibility Reason)
	{
		static const IConsoleVariable* Debug = IConsoleManager::Get().FindConsoleVariable(TEXT("outlier.Enemy.FlightDebug"));
		if (!Debug || Debug->GetInt() == 0) return;
		UE_LOG(LogEnemyCharge, Log, TEXT("[EnemyCharge] %s Enemy=%s Target=%s Reason=%s Distance2D=%.1f Distance3D=%.1f"),
			Phase, *GetNameSafe(Drone), *GetNameSafe(Target), *UEnum::GetValueAsString(Reason),
			Drone && Target ? FVector::Dist2D(Drone->GetActorLocation(), Target->GetActorLocation()) : 0.0,
			Drone && Target ? FVector::Dist(Drone->GetActorLocation(), Target->GetActorLocation()) : 0.0);
	}

	void ResetChargeAssessment(FEnemySelfDestructAssessmentTaskInstanceData& Data)
	{
		Data.TargetActor = nullptr;
		Data.bHasVisibleTarget = false;
		Data.bCanCharge = false;
		Data.HorizontalDistance = 0.0f;
		Data.SpatialDistance = 0.0f;
		Data.Eligibility = EEnemyChargeEligibility::Invalid;
	}

	AActor* GetChargeAssessmentTarget(const FEnemySelfDestructAssessmentTaskInstanceData& Data)
	{
		const ASelfDestructDrone* Drone = Data.CachedDrone.Get();
		const AEnemyAIController* Controller = Data.CachedController.Get();
		if (!Drone || !Drone->HasAuthority() || Drone->IsDead() || !Drone->CanUseEnemyPerception()
			|| Drone->GetCombatState() != EEnemyCombatState::Combat || !Drone->IsPlayerCurrentlyVisible()
			|| !Controller || Drone->GetController() != Controller || Controller->GetPawn() != Drone) return nullptr;
		return Controller->GetCachedVisibleTarget();
	}

	void UpdateChargeAssessment(FEnemySelfDestructAssessmentTaskInstanceData& Data)
	{
		const EEnemyChargeEligibility Previous = Data.Eligibility;
		const AActor* PreviousTarget = Data.TargetActor;
		ResetChargeAssessment(Data);
		Data.TargetActor = GetChargeAssessmentTarget(Data);
		Data.bHasVisibleTarget = IsValid(Data.TargetActor);
		Data.Eligibility = EnemyCharge::Assess(Data.CachedDrone.Get(), Data.CachedController.Get(),
			Data.TargetActor, Data.ChargeEntryDistance, Data.ChargeClearance);
		Data.bCanCharge = Data.Eligibility == EEnemyChargeEligibility::Ready;
		if (Data.bHasVisibleTarget)
		{
			Data.HorizontalDistance = FVector::Dist2D(Data.Drone->GetActorLocation(), Data.TargetActor->GetActorLocation());
			Data.SpatialDistance = FVector::Dist(Data.Drone->GetActorLocation(), Data.TargetActor->GetActorLocation());
		}
		if (Previous != Data.Eligibility || PreviousTarget != Data.TargetActor)
			LogChargeEligibility(TEXT("AssessmentChanged"), Data.Drone, Data.TargetActor, Data.Eligibility);
	}

	ASelfDestructDrone* ResolveSelfDestructDrone(
		FStateTreeExecutionContext& Context,
		AEnemyBase* BoundEnemy)
	{
		if (ASelfDestructDrone* BoundDrone = Cast<ASelfDestructDrone>(BoundEnemy))
		{
			return BoundDrone;
		}

		if (ASelfDestructDrone* OwnerDrone = Cast<ASelfDestructDrone>(Context.GetOwner()))
		{
			return OwnerDrone;
		}

		if (const UActorComponent* OwnerComponent = Cast<UActorComponent>(Context.GetOwner()))
		{
			return Cast<ASelfDestructDrone>(OwnerComponent->GetOwner());
		}

		return nullptr;
	}
}

bool EnemyCharge::IsWithinChargeRange(const FVector& Start, const FVector& Target, float Distance)
{
	if (Start.ContainsNaN() || Target.ContainsNaN() || !FMath::IsFinite(Distance) || Distance <= 0.0f || Distance > 500.0f) return false;
	return FVector::DistSquared2D(Start, Target) <= FMath::Square(Distance)
		&& FVector::DistSquared(Start, Target) <= FMath::Square(Distance);
}

bool EnemyCharge::IsStraightPathClear(UWorld& World, const UCapsuleComponent& Capsule, const AActor& Target, float Clearance)
{
	if (!World.GetPhysicsScene() || !IsValid(Capsule.GetOwner()) || !Capsule.IsQueryCollisionEnabled()
		|| !FMath::IsFinite(Clearance) || Clearance < 0.0f) return false;
	const FVector Start = Capsule.GetComponentLocation();
	const FVector Goal = Target.GetActorLocation() + Start - Capsule.GetOwner()->GetActorLocation();
	if (Start.ContainsNaN() || Goal.ContainsNaN()) return false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(EnemyChargeEntry), false, Capsule.GetOwner());
	Params.AddIgnoredActors(Capsule.GetMoveIgnoreActors());
	Params.AddIgnoredComponents(Capsule.GetMoveIgnoreComponents());
	// 타깃에 닿는 것은 돌진의 목적이다. 타깃 자체는 제외하고 중간의 차단 충돌만 검사한다.
	Params.AddIgnoredActor(&Target);
	const FCollisionResponseParams Responses(Capsule.GetCollisionResponseToChannels());
	const float Radius = Capsule.GetScaledCapsuleRadius() + Clearance;
	const float HalfHeight = Capsule.GetScaledCapsuleHalfHeight() + Clearance;
	if (!FMath::IsFinite(Radius) || !FMath::IsFinite(HalfHeight) || Radius <= 0.0f || HalfHeight < Radius) return false;
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius, HalfHeight);
	// Sweep 시작점이 이미 벽과 겹친 경우도 통과 가능한 직선으로 취급하지 않는다.
	if (World.OverlapBlockingTestByChannel(Start, Capsule.GetComponentQuat(), Capsule.GetCollisionObjectType(), Shape, Params, Responses)) return false;
	return !World.SweepTestByChannel(Start, Goal, Capsule.GetComponentQuat(), Capsule.GetCollisionObjectType(), Shape, Params, Responses);
}

EEnemyChargeEligibility EnemyCharge::Assess(const ASelfDestructDrone* Drone, const AEnemyAIController* Controller,
	const AActor* Target, float Distance, float Clearance)
{
	if (!IsValid(Drone) || !Drone->HasAuthority() || !Drone->GetWorld() || !Drone->GetCapsuleComponent()
		|| !FMath::IsFinite(Distance) || Distance <= 0.0f || Distance > 500.0f
		|| !FMath::IsFinite(Clearance) || Clearance < 0.0f) return EEnemyChargeEligibility::Invalid;
	if (Drone->IsSelfDestructCommitted()) return EEnemyChargeEligibility::AlreadyCommitted;
	if (Drone->IsDead() || !Drone->CanUseEnemyPerception() || Drone->GetCombatState() != EEnemyCombatState::Combat)
		return EEnemyChargeEligibility::NotInCombat;
	if (!Controller || Drone->GetController() != Controller || Controller->GetPawn() != Drone
		|| !Drone->IsPlayerCurrentlyVisible() || !IsValid(Target) || Controller->GetCachedVisibleTarget() != Target)
		return EEnemyChargeEligibility::NoVisibleTarget;
	if (!IsWithinChargeRange(Drone->GetActorLocation(), Target->GetActorLocation(), Distance)) return EEnemyChargeEligibility::OutOfRange;
	return IsStraightPathClear(*Drone->GetWorld(), *Drone->GetCapsuleComponent(), *Target, Clearance)
		? EEnemyChargeEligibility::Ready : EEnemyChargeEligibility::BlockedPath;
}

FEnemySelfDestructAssessmentTask::FEnemySelfDestructAssessmentTask()
{
	bShouldCallTick = true;
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldCopyBoundPropertiesOnExitState = false;
#if WITH_EDITORONLY_DATA
	bConsideredForCompletion = false;
	bCanEditConsideredForCompletion = false;
#endif
}

EStateTreeRunStatus FEnemySelfDestructAssessmentTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	ResetChargeAssessment(Data);
	Data.Elapsed = 0.0f;
	Data.CachedDrone = Cast<ASelfDestructDrone>(Data.Drone);
	Data.CachedController = IsValid(Data.Drone) ? Cast<AEnemyAIController>(Data.Drone->GetController()) : nullptr;
	if (!FMath::IsFinite(Data.UpdateInterval) || Data.UpdateInterval <= 0.0f) return EStateTreeRunStatus::Failed;
	UpdateChargeAssessment(Data);
	LogChargeEligibility(TEXT("AssessmentEnter"), Data.Drone, Data.TargetActor, Data.Eligibility);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FEnemySelfDestructAssessmentTask::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	Data.Elapsed += FMath::Max(DeltaTime, 0.0f);
	// 시야 상실/타깃 교체는 즉시 반영한다. 비싼 캡슐 Sweep은 같은 타깃에 대해 설정 간격으로만 실행한다.
	if (GetChargeAssessmentTarget(Data) != Data.TargetActor || Data.Elapsed >= Data.UpdateInterval)
	{
		Data.Elapsed = 0.0f;
		UpdateChargeAssessment(Data);
	}
	return EStateTreeRunStatus::Running;
}

void FEnemySelfDestructAssessmentTask::ExitState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	ResetChargeAssessment(Data);
	Data.CachedDrone.Reset();
	Data.CachedController.Reset();
}

FEnemyChargeLastDirectionTask::FEnemyChargeLastDirectionTask()
{
	bShouldCallTick = true;
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldStateChangeOnReselect = false;
}

FEnemyBeginPossessedSelfDestructTask::FEnemyBeginPossessedSelfDestructTask()
{
	bShouldCallTick = false;
	bShouldStateChangeOnReselect = false;
}

EStateTreeRunStatus FEnemyBeginPossessedSelfDestructTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	(void)Transition;

	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	ASelfDestructDrone* Drone = ResolveSelfDestructDrone(Context, InstanceData.Drone);
	InstanceData.Drone = Drone;
	if (AEnemyBase::IsPossessedAttackDiagnosticsEnabled())
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[EnemyPossessedAttackDiag] BeginSelfDestructEnter Owner=%s BoundEnemy=%s ResolvedDrone=%s "
				"Authority=%s Possessed=%s HasRequest=%s Committed=%s"),
			*GetNameSafe(Context.GetOwner()),
			*GetNameSafe(InstanceData.Drone),
			*GetNameSafe(Drone),
			Drone && Drone->HasAuthority() ? TEXT("true") : TEXT("false"),
			Drone && Drone->IsEnemyPossessed() ? TEXT("true") : TEXT("false"),
			Drone && Drone->HasPossessedAttackRequest() ? TEXT("true") : TEXT("false"),
			Drone && Drone->IsSelfDestructCommitted() ? TEXT("true") : TEXT("false"));
	}
	if (!Drone
		|| !Drone->HasAuthority()
		|| !Drone->IsEnemyPossessed()
		|| !Drone->HasPossessedAttackRequest()
		|| Drone->IsSelfDestructCommitted())
	{
		return EStateTreeRunStatus::Failed;
	}

	AController* ActiveController = Drone->GetController();
	if (!ActiveController)
	{
		if (AEnemyBase::IsPossessedAttackDiagnosticsEnabled())
		{
			UE_LOG(LogOutlier, Error, TEXT("[EnemyPossessedAttackDiag] BeginSelfDestructFailed Enemy=%s Reason=NoController"), *GetNameSafe(Drone));
		}
		return EStateTreeRunStatus::Failed;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	ActiveController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const float ChargeSpeed =
		FMath::Max(Drone->GetRuntimeStat().MoveSpeed, 0.0f)
		* FMath::Max(InstanceData.ChargeSpeedMultiplier, 0.0f);
	if (!Drone->BeginCommittedSelfDestructDirection(
		ViewRotation.Vector(),
		InstanceData.TelegraphDuration,
		ChargeSpeed,
		InstanceData.MaxChargeDistance))
	{
		if (AEnemyBase::IsPossessedAttackDiagnosticsEnabled())
		{
			UE_LOG(LogOutlier, Error, TEXT("[EnemyPossessedAttackDiag] BeginSelfDestructFailed Enemy=%s Reason=CommitRejected"), *GetNameSafe(Drone));
		}
		return EStateTreeRunStatus::Failed;
	}

	// 입력 소비까지 성공해야 같은 입력이 다음 공격 사이클로 중복 전달되지 않는다.
	if (!Drone->ConsumePossessedAttackRequest())
	{
		if (AEnemyBase::IsPossessedAttackDiagnosticsEnabled())
		{
			UE_LOG(LogOutlier, Error, TEXT("[EnemyPossessedAttackDiag] BeginSelfDestructFailed Enemy=%s Reason=ConsumeRequestFailed"), *GetNameSafe(Drone));
		}
		Drone->CancelCommittedSelfDestruct();
		return EStateTreeRunStatus::Failed;
	}

	Drone->SetAttackPhase(EEnemyAttackPhase::Telegraph);
	if (AEnemyBase::IsPossessedAttackDiagnosticsEnabled())
	{
		UE_LOG(LogOutlier, Warning, TEXT("[EnemyPossessedAttackDiag] BeginSelfDestructSucceeded Enemy=%s"), *GetNameSafe(Drone));
	}
	return EStateTreeRunStatus::Succeeded;
}

FEnemyUpdatePossessedSelfDestructTask::FEnemyUpdatePossessedSelfDestructTask()
{
	bShouldStateChangeOnReselect = false;
}

EStateTreeRunStatus FEnemyUpdatePossessedSelfDestructTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	(void)Transition;

	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	InstanceData.ElapsedTime = 0.0f;
	InstanceData.bCompleted = false;
	ASelfDestructDrone* Drone = ResolveSelfDestructDrone(Context, InstanceData.Drone);
	InstanceData.Drone = Drone;
	if (AEnemyBase::IsPossessedAttackDiagnosticsEnabled())
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[EnemyPossessedAttackDiag] UpdateSelfDestructEnter Enemy=%s Authority=%s Possessed=%s Committed=%s"),
			*GetNameSafe(Drone),
			Drone && Drone->HasAuthority() ? TEXT("true") : TEXT("false"),
			Drone && Drone->IsEnemyPossessed() ? TEXT("true") : TEXT("false"),
			Drone && Drone->IsSelfDestructCommitted() ? TEXT("true") : TEXT("false"));
	}
	if (!Drone
		|| !Drone->HasAuthority()
		|| !Drone->IsEnemyPossessed()
		|| !Drone->IsSelfDestructCommitted())
	{
		return EStateTreeRunStatus::Failed;
	}

	Drone->SetAttackPhase(EEnemyAttackPhase::Firing);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FEnemyUpdatePossessedSelfDestructTask::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	ASelfDestructDrone* Drone = ResolveSelfDestructDrone(Context, InstanceData.Drone);
	InstanceData.Drone = Drone;
	if (!Drone
		|| !Drone->HasAuthority()
		|| !Drone->IsEnemyPossessed()
		|| !Drone->IsSelfDestructCommitted())
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.ElapsedTime += FMath::Max(DeltaTime, 0.0f);
	const float ChargeSpeed =
		FMath::Max(Drone->GetRuntimeStat().MoveSpeed, 0.0f)
		* FMath::Max(InstanceData.ChargeSpeedMultiplier, 0.0f);
	if (!Drone->UpdateCommittedSelfDestructMovement(DeltaTime, ChargeSpeed))
	{
		return EStateTreeRunStatus::Failed;
	}

	if (InstanceData.ElapsedTime < FMath::Max(InstanceData.ChargeDuration, 0.0f))
	{
		return EStateTreeRunStatus::Running;
	}

	// 성공 전환 뒤 Detonate Task가 실행될 때까지 Commit을 유지한다.
	InstanceData.bCompleted = true;
	return EStateTreeRunStatus::Succeeded;
}

void FEnemyUpdatePossessedSelfDestructTask::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	(void)Transition;

	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	ASelfDestructDrone* Drone = ResolveSelfDestructDrone(Context, InstanceData.Drone);
	InstanceData.Drone = Drone;
	if (!InstanceData.bCompleted && IsValid(Drone) && Drone->HasAuthority())
	{
		Drone->CancelCommittedSelfDestruct();
		Drone->SetAttackPhase(EEnemyAttackPhase::Idle);
	}
}

EStateTreeRunStatus FEnemyChargeLastDirectionTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	InstanceData.ElapsedTime = 0.0f;
	InstanceData.bCompleted = false;
	ASelfDestructDrone* Drone = ResolveSelfDestructDrone(Context, InstanceData.Drone);
	InstanceData.Drone = Drone;
	if (!Drone || !Drone->HasAuthority())
	{
		return EStateTreeRunStatus::Failed;
	}

	AActor* Target = InstanceData.TargetActor.Get();
	if (!IsValid(Target))
	{
		if (const AEnemyAIController* AIController =
			Cast<AEnemyAIController>(Drone->GetController()))
		{
			Target = AIController->GetCachedVisibleTarget();
		}
	}

	// 전이 조건이 참이었던 프레임 이후 타깃/시야/벽이 바뀌었을 수 있다. Commit 직전 같은 검사를 반복한다.
	const EEnemyChargeEligibility Eligibility = EnemyCharge::Assess(Drone, Cast<AEnemyAIController>(Drone->GetController()),
		Target, InstanceData.ChargeEntryDistance, InstanceData.ChargeClearance);
	LogChargeEligibility(TEXT("ChargeEnter"), Drone, Target, Eligibility);
	if (Eligibility != EEnemyChargeEligibility::Ready) return EStateTreeRunStatus::Failed;
	if (!FMath::IsFinite(InstanceData.TelegraphDuration) || InstanceData.TelegraphDuration < 0.0f
		|| !FMath::IsFinite(InstanceData.ChargeSpeedMultiplier) || InstanceData.ChargeSpeedMultiplier <= 0.0f)
	{
		LogChargeEligibility(TEXT("ChargeRejectedConfiguration"), Drone, Target, EEnemyChargeEligibility::Invalid);
		return EStateTreeRunStatus::Failed;
	}

	const float ChargeSpeed =
		FMath::Max(Drone->GetRuntimeStat().MoveSpeed, 0.0f)
		* FMath::Max(InstanceData.ChargeSpeedMultiplier, 0.0f);
	const bool bCommitted = Drone->BeginCommittedSelfDestruct(
		Target,
		InstanceData.TelegraphDuration,
		ChargeSpeed);
	LogChargeEligibility(bCommitted ? TEXT("Committed") : TEXT("CommitRejected"), Drone, Target,
		bCommitted ? EEnemyChargeEligibility::Ready : EEnemyChargeEligibility::Invalid);
	return bCommitted ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus FEnemyChargeLastDirectionTask::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	ASelfDestructDrone* Drone = ResolveSelfDestructDrone(Context, InstanceData.Drone);
	InstanceData.Drone = Drone;
	if (!Drone || !Drone->HasAuthority() || !Drone->IsSelfDestructCommitted())
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.ElapsedTime += FMath::Max(DeltaTime, 0.0f);
	const float ChargeSpeed =
		FMath::Max(Drone->GetRuntimeStat().MoveSpeed, 0.0f)
		* FMath::Max(InstanceData.ChargeSpeedMultiplier, 0.0f);
	if (!Drone->UpdateCommittedSelfDestructMovement(DeltaTime, ChargeSpeed))
	{
		return EStateTreeRunStatus::Failed;
	}

	if (InstanceData.ElapsedTime < FMath::Max(InstanceData.TelegraphDuration, 0.0f))
	{
		return EStateTreeRunStatus::Running;
	}

	// 정상 완료 시 다음 Detonate Task까지 Commit을 유지한다.
	InstanceData.bCompleted = true;
	return EStateTreeRunStatus::Succeeded;
}

void FEnemyChargeLastDirectionTask::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	ASelfDestructDrone* Drone = ResolveSelfDestructDrone(Context, InstanceData.Drone);
	InstanceData.Drone = Drone;
	if (!InstanceData.bCompleted
		&& IsValid(Drone)
		&& Drone->HasAuthority())
	{
		// Stun, Possession, Death 또는 실패 전환은 살아 있는 드론의 전조를 취소한다.
		Drone->CancelCommittedSelfDestruct();
	}
}

FEnemyBombDetonateTask::FEnemyBombDetonateTask()
{
	bShouldCallTick = false;
	bShouldStateChangeOnReselect = false;
}

EStateTreeRunStatus FEnemyBombDetonateTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	ASelfDestructDrone* Drone = ResolveSelfDestructDrone(Context, InstanceData.Drone);
	InstanceData.Drone = Drone;
	if (AEnemyBase::IsPossessedAttackDiagnosticsEnabled())
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[EnemyPossessedAttackDiag] DetonateEnter Enemy=%s Authority=%s Committed=%s"),
			*GetNameSafe(Drone),
			Drone && Drone->HasAuthority() ? TEXT("true") : TEXT("false"),
			Drone && Drone->IsSelfDestructCommitted() ? TEXT("true") : TEXT("false"));
	}
	if (!Drone
		|| !Drone->HasAuthority()
		|| !Drone->IsSelfDestructCommitted())
	{
		if (IsValid(Drone) && Drone->HasAuthority())
		{
			Drone->CancelCommittedSelfDestruct();
		}
		return EStateTreeRunStatus::Failed;
	}

	Drone->TriggerSelfDestruct();
	return EStateTreeRunStatus::Succeeded;
}
