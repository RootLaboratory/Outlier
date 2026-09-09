// Fill out your copyright notice in the Description page of Project Settings.


#include "Weapon/MeleeWeaponBase.h"
#include "Enemy/EnemyBase.h"
#include "Shooter/ShooterCharacter.h"
#include "Team/OutlierTeamIds.h"
#include "GameFramework/Controller.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

AMeleeWeaponBase::AMeleeWeaponBase()
{
	WeaponType = EWeaponType::Melee;
}

void AMeleeWeaponBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopAttack();
	Super::EndPlay(EndPlayReason);
}

void AMeleeWeaponBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMeleeWeaponBase, AttackPhase);
}

bool AMeleeWeaponBase::CanAttack() const
{
	const AShooterCharacter* Shooter = Cast<AShooterCharacter>(WeaponOwner);
	return Super::CanAttack()
		&& AttackPhase == EMeleeAttackPhase::Idle
		&& !bIsAttacking
		&& (!Shooter || !Shooter->IsDead());
}

void AMeleeWeaponBase::StartAttack()
{
	const AShooterCharacter* Shooter = Cast<AShooterCharacter>(WeaponOwner);
	if (!HasAuthority() || !Super::CanAttack() || (Shooter && Shooter->IsDead()))
	{
		return;
	}

	// 진행 중 다시 눌러도 현재 공격은 유지하고, Recovery 종료 시 입력 유지 여부만 확인한다.
	bWantsToAttack = true;
	if (!CanAttack())
	{
		return;
	}

	Super::StartAttack();
	AttackSequence = AttackSequence == MAX_int32 ? 1 : AttackSequence + 1;
	AttackPhase = EMeleeAttackPhase::Attack;
	RefreshOwnerCombatState();
	ForceNetUpdate();

	GetWorldTimerManager().SetTimer(
		AttackTimerHandle,
		FTimerDelegate::CreateUObject(this, &AMeleeWeaponBase::CommitAttack, AttackSequence),
		FMath::Max(AttackDelay, 0.01f),
		false);
}

void AMeleeWeaponBase::ReleaseAttack()
{
	if (HasAuthority())
	{
		bWantsToAttack = false;
	}
}

void AMeleeWeaponBase::StopAttack()
{
	if (!HasAuthority())
	{
		return;
	}

	bWantsToAttack = false;
	GetWorldTimerManager().ClearTimer(AttackTimerHandle);
	GetWorldTimerManager().ClearTimer(RecoveryTimerHandle);
	AttackPhase = EMeleeAttackPhase::Idle;
	Super::StopAttack();
	RefreshOwnerCombatState();
	ForceNetUpdate();
}

void AMeleeWeaponBase::PerformAttack()
{
	CommitAttack(AttackSequence);
}

void AMeleeWeaponBase::CommitAttack(int32 ExpectedAttackSequence)
{
	// Phase rejects duplicates/cancellation; sequence rejects callbacks from a previous swing.
	if (!HasAuthority() || ExpectedAttackSequence != AttackSequence
		|| AttackPhase != EMeleeAttackPhase::Attack || !bIsAttacking)
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(AttackTimerHandle);
	AttackPhase = EMeleeAttackPhase::Recovery;
	RefreshOwnerCombatState();
	ForceNetUpdate();
	TraceMeleeHit();

	// Slice 4 replaces this fallback timing with the animation-authored recovery end.
	GetWorldTimerManager().SetTimer(
		RecoveryTimerHandle,
		FTimerDelegate::CreateUObject(this, &AMeleeWeaponBase::FinishAttack, AttackSequence),
		FMath::Max(RecoveryDuration, 0.01f),
		false);
}

void AMeleeWeaponBase::FinishAttack(int32 ExpectedAttackSequence)
{
	if (!HasAuthority() || ExpectedAttackSequence != AttackSequence
		|| AttackPhase != EMeleeAttackPhase::Recovery)
	{
		return;
	}

	// 정상 완료에서만 반복한다. 강제 취소는 StopAttack에서 입력 의도까지 정리한다.
	const bool bRepeatAttack = bWantsToAttack;
	StopAttack();
	if (!bRepeatAttack)
	{
		return;
	}

	if (AShooterCharacter* Shooter = Cast<AShooterCharacter>(WeaponOwner))
	{
		// 반복 공격도 기존 전투 진입 조건을 통과해야 하며, 교체된 무기는 재시작하지 않는다.
		if (Shooter->GetCurrentWeapon() != this || !Shooter->CanFireInCurrentState())
		{
			return;
		}
	}
	StartAttack();
}

void AMeleeWeaponBase::RefreshOwnerCombatState()
{
	if (AShooterCharacter* Shooter = Cast<AShooterCharacter>(WeaponOwner))
	{
		if (Shooter->GetCurrentWeapon() == this)
		{
			Shooter->RefreshCombatState();
		}
	}
}

void AMeleeWeaponBase::OnRep_AttackPhase()
{
	bIsAttacking = AttackPhase != EMeleeAttackPhase::Idle;
	RefreshOwnerCombatState();
}

void AMeleeWeaponBase::TraceMeleeHit()
{
	ACharacter* OwnerCharacter = Cast<ACharacter>(WeaponOwner);
	AController* OwnerController = OwnerCharacter ? OwnerCharacter->GetController() : nullptr;
	UWorld* World = GetWorld();
	if (!HasAuthority() || !OwnerCharacter || !OwnerController || !World)
	{
		return;
	}

	FVector TraceStart;
	FRotator ViewRotation;
	OwnerController->GetPlayerViewPoint(TraceStart, ViewRotation);
	const FVector TraceDirection = ViewRotation.Vector().GetSafeNormal();
	const FVector TraceEnd = TraceStart + TraceDirection * FMath::Max(AttackRange, 0.0f);

	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjectQueryParams.AddObjectTypesToQuery(ECC_PhysicsBody);

	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(MeleeAttackTrace), false, OwnerCharacter);
	TraceParams.AddIgnoredActor(this);

	TArray<FHitResult> HitResults;
	World->SweepMultiByObjectType(
		HitResults,
		TraceStart,
		TraceEnd,
		FQuat::Identity,
		ObjectQueryParams,
		FCollisionShape::MakeSphere(FMath::Max(AttackRadius, 0.0f)),
		TraceParams);

	AEnemyBase* BestTarget = nullptr;
	float BestAlignment = -1.0f;
	float BestDistanceSquared = TNumericLimits<float>::Max();
	TSet<AEnemyBase*> EvaluatedEnemies;

	for (const FHitResult& HitResult : HitResults)
	{
		AEnemyBase* Enemy = Cast<AEnemyBase>(HitResult.GetActor());
		if (!IsValid(Enemy) || EvaluatedEnemies.Contains(Enemy))
		{
			continue;
		}
		EvaluatedEnemies.Add(Enemy);

		if (Enemy->IsDead() || !Enemy->CanBeDamaged()
			|| Enemy->GetGenericTeamId().GetId() != OutlierTeamIds::Enemy)
		{
			continue;
		}

		const FVector ToTarget = Enemy->GetActorLocation() - TraceStart;
		const float ForwardDistance = FVector::DotProduct(ToTarget, TraceDirection);
		if (ForwardDistance < 0.0f)
		{
			continue;
		}

		FHitResult VisibilityHit;
		FCollisionQueryParams VisibilityParams(SCENE_QUERY_STAT(MeleeAttackVisibility), false, OwnerCharacter);
		VisibilityParams.AddIgnoredActor(this);
		const bool bVisibilityBlocked = World->LineTraceSingleByChannel(
			VisibilityHit,
			TraceStart,
			Enemy->GetActorLocation(),
			ECC_Visibility,
			VisibilityParams);
		if (bVisibilityBlocked && VisibilityHit.GetActor() != Enemy)
		{
			continue;
		}

		const float DistanceSquared = ToTarget.SizeSquared();
		const float Alignment = FVector::DotProduct(TraceDirection, ToTarget.GetSafeNormal());
		constexpr float AlignmentTolerance = 0.001f;
		if (!BestTarget
			|| Alignment > BestAlignment + AlignmentTolerance
			|| (FMath::IsNearlyEqual(Alignment, BestAlignment, AlignmentTolerance)
				&& DistanceSquared < BestDistanceSquared))
		{
			BestTarget = Enemy;
			BestAlignment = Alignment;
			BestDistanceSquared = DistanceSquared;
		}
	}

	if (BestTarget)
	{
		// Slice 3 owns normal damage and instant-kill resolution behind this boundary.
		ApplyHitToTarget(BestTarget);
	}
}

void AMeleeWeaponBase::ApplyHitToTarget(AActor* Target)
{
}
