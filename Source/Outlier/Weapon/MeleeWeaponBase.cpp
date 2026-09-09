// Fill out your copyright notice in the Description page of Project Settings.


#include "Weapon/MeleeWeaponBase.h"
#include "Shooter/ShooterCharacter.h"
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

	// Slice 2 adds the single server hit query at this transition.
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
}

void AMeleeWeaponBase::ApplyHitToTarget(AActor* Target)
{
}
