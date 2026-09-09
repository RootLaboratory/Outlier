// Fill out your copyright notice in the Description page of Project Settings.


#include "Weapon/MeleeWeaponBase.h"
#include "Damage/OutlierDamageReceiver.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerVitalityComponent.h"
#include "Enemy/EnemyBase.h"
#include "GAS/Attributes/OutlierVitalAttributeSet.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Shooter/ShooterCharacter.h"
#include "Team/OutlierTeamIds.h"
#include "GameFramework/Controller.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

AMeleeWeaponBase::AMeleeWeaponBase()
{
	WeaponType = EWeaponType::Melee;
	Damage = 50.0f;
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

	bUsesAnimationTiming = PlayAttackAnimation();
	if (!bUsesAnimationTiming)
	{
		GetWorldTimerManager().SetTimer(
			AttackTimerHandle,
			FTimerDelegate::CreateUObject(this, &AMeleeWeaponBase::CommitAttack, AttackSequence),
			FMath::Max(AttackDelay, 0.01f),
			false);
	}
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
	bUsesAnimationTiming = false;
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

	if (!bUsesAnimationTiming)
	{
		GetWorldTimerManager().SetTimer(
			RecoveryTimerHandle,
			FTimerDelegate::CreateUObject(this, &AMeleeWeaponBase::FinishAttack, AttackSequence),
			FMath::Max(RecoveryDuration, 0.01f),
			false);
	}
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

void AMeleeWeaponBase::HandleHitNotify()
{
	CommitAttack(AttackSequence);
}

void AMeleeWeaponBase::HandleRecoveryEndNotify()
{
	FinishAttack(AttackSequence);
}

bool AMeleeWeaponBase::PlayAttackAnimation()
{
	AShooterCharacter* Shooter = Cast<AShooterCharacter>(WeaponOwner);
	if (!Shooter)
	{
		return false;
	}

	Shooter->HandleMeleeAttackAnimation();

	UAnimMontage* ThirdPersonMontage = Shooter->GetThirdPersonMeleeAttackMontage();
	UAnimInstance* ThirdPersonAnimInstance = Shooter->GetMesh()
		? Shooter->GetMesh()->GetAnimInstance()
		: nullptr;
	if (!ThirdPersonMontage || !ThirdPersonAnimInstance
		|| !ThirdPersonAnimInstance->Montage_IsPlaying(ThirdPersonMontage))
	{
		return false;
	}

	FOnMontageEnded MontageEndedDelegate;
	MontageEndedDelegate.BindUObject(
		this,
		&AMeleeWeaponBase::HandleAttackMontageEnded,
		AttackSequence);
	ThirdPersonAnimInstance->Montage_SetEndDelegate(MontageEndedDelegate, ThirdPersonMontage);
	return true;
}

void AMeleeWeaponBase::HandleAttackMontageEnded(
	UAnimMontage* /*Montage*/,
	bool /*bInterrupted*/,
	int32 ExpectedAttackSequence)
{
	if (!HasAuthority() || ExpectedAttackSequence != AttackSequence
		|| AttackPhase == EMeleeAttackPhase::Idle)
	{
		return;
	}

	// Interruption cancels the swing; a normal end reaching here means RecoveryEnd Notify was omitted.
	StopAttack();
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

	AActor* BestTarget = nullptr;
	float BestAlignment = -1.0f;
	float BestDistanceSquared = TNumericLimits<float>::Max();
	TSet<AActor*> EvaluatedTargets;

	for (const FHitResult& HitResult : HitResults)
	{
		AActor* Candidate = HitResult.GetActor();
		if (!IsValid(Candidate) || EvaluatedTargets.Contains(Candidate))
		{
			continue;
		}
		EvaluatedTargets.Add(Candidate);

		bool bCanTarget = false;
		if (const AEnemyBase* Enemy = Cast<AEnemyBase>(Candidate))
		{
			bCanTarget = !Enemy->IsDead()
				&& Enemy->CanBeDamaged()
				&& Enemy->GetGenericTeamId().GetId() == OutlierTeamIds::Enemy;
		}
		else if (const APartnerCharacter* Partner = Cast<APartnerCharacter>(Candidate))
		{
			const UOutlierVitalAttributeSet* Vital = Partner->GetVitalAttributeSet();
			const UPartnerVitalityComponent* Vitality = Partner->GetPartnerVitalityComponent();
			bCanTarget = Partner->CanBeDamaged()
				&& Vital && Vital->GetHealth() > 0.0f
				&& Vitality && !Vitality->IsRebooting();
		}
		if (!bCanTarget)
		{
			continue;
		}

		const FVector ToTarget = Candidate->GetActorLocation() - TraceStart;
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
			Candidate->GetActorLocation(),
			ECC_Visibility,
			VisibilityParams);
		if (bVisibilityBlocked && VisibilityHit.GetActor() != Candidate)
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
			BestTarget = Candidate;
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
	AEnemyBase* Enemy = Cast<AEnemyBase>(Target);
	APartnerCharacter* Partner = Cast<APartnerCharacter>(Target);
	if (!HasAuthority() || (!IsValid(Enemy) && !IsValid(Partner))
		|| !Target->CanBeDamaged())
	{
		return;
	}

	float DamageToApply = Damage;
	if (Enemy)
	{
		if (Enemy->IsDead() || Enemy->GetCurrentHealth() <= 0.0f)
		{
			return;
		}

		const EEnemyCombatState CombatState = Enemy->GetCombatState();
		const bool bInstantKill = CombatState == EEnemyCombatState::NonCombat
			|| CombatState == EEnemyCombatState::Alert
			|| CombatState == EEnemyCombatState::Stun;
		DamageToApply = bInstantKill ? Enemy->GetCurrentHealth() : Damage;
	}
	else
	{
		const UOutlierVitalAttributeSet* Vital = Partner->GetVitalAttributeSet();
		const UPartnerVitalityComponent* Vitality = Partner->GetPartnerVitalityComponent();
		if (!Vital || Vital->GetHealth() <= 0.0f || !Vitality || Vitality->IsRebooting())
		{
			return;
		}
	}

	FOutlierDamageRequest DamageRequest;
	DamageRequest.DamageAmount = DamageToApply;
	DamageRequest.DamageTag = OutlierGameplayTags::Damage::Weapon();
	DamageRequest.DamageOrigin = IsValid(WeaponOwner)
		? WeaponOwner->GetActorLocation()
		: GetActorLocation();
	DamageRequest.EventInstigator = IsValid(WeaponOwner) ? WeaponOwner->GetController() : nullptr;
	DamageRequest.DamageCauser = this;
	OutlierDamage::Apply(Target, DamageRequest);
}
