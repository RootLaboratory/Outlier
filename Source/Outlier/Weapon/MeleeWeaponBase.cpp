// Fill out your copyright notice in the Description page of Project Settings.


#include "Weapon/MeleeWeaponBase.h"
#include "Outlier.h"
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

#if ENABLE_DRAW_DEBUG
#include "DrawDebugHelpers.h"
#endif

AMeleeWeaponBase::AMeleeWeaponBase()
{
	WeaponType = EWeaponType::Melee;
	Damage = 50.0f;
	MeleeTraceHitBuffer.Reserve(8);
}

void AMeleeWeaponBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopTargetSearch();
	StopAttack();
	Super::EndPlay(EndPlayReason);
}

void AMeleeWeaponBase::OnEquipped(ACharacter* NewOwner)
{
	Super::OnEquipped(NewOwner);
	RefreshTargetSearchState();
}

void AMeleeWeaponBase::OnUnequipped()
{
	StopTargetSearch();
	Super::OnUnequipped();
}

void AMeleeWeaponBase::OnDropped(const FTransform& DropTransform, AFirstPersonCharacter* DroppedBy)
{
	StopTargetSearch();
	Super::OnDropped(DropTransform, DroppedBy);
}

void AMeleeWeaponBase::OnRep_EquippedState()
{
	Super::OnRep_EquippedState();
	RefreshTargetSearchState();
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

	if (AShooterCharacter* OwnerShooter = Cast<AShooterCharacter>(WeaponOwner))
	{
		OwnerShooter->SetMeleeTracePoseRefreshEnabled(true);
	}
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
	ResetMeleeTraceState();
	if (AShooterCharacter* OwnerShooter = Cast<AShooterCharacter>(WeaponOwner))
	{
		OwnerShooter->SetMeleeTracePoseRefreshEnabled(false);
	}
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
	if (!bUsesAnimationTiming)
	{
		TraceMeleeHit();
	}

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
	if (BeginMeleeTrace())
	{
		EndMeleeTrace();
	}
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

void AMeleeWeaponBase::RefreshTargetSearchState()
{
	ACharacter* OwnerCharacter = Cast<ACharacter>(WeaponOwner);
	const AShooterCharacter* Shooter = Cast<AShooterCharacter>(OwnerCharacter);
	const bool bShouldSearch = bIsEquipped
		&& OwnerCharacter
		&& OwnerCharacter->IsLocallyControlled()
		&& (!Shooter || Shooter->GetCurrentWeapon() == this);
	if (!bShouldSearch)
	{
		StopTargetSearch();
		return;
	}

	if (!GetWorldTimerManager().TimerExists(TargetSearchTimerHandle))
	{
		CurrentTargetSearchInterval = FMath::Max(InitialTargetSearchInterval, 0.01f);
		ScheduleTargetSearch(CurrentTargetSearchInterval);
	}
}

void AMeleeWeaponBase::ScheduleTargetSearch(float Delay)
{
	GetWorldTimerManager().SetTimer(
		TargetSearchTimerHandle,
		this,
		&AMeleeWeaponBase::RefreshMeleeTarget,
		FMath::Max(Delay, 0.01f),
		false);
}

void AMeleeWeaponBase::RefreshMeleeTarget()
{
	ACharacter* OwnerCharacter = Cast<ACharacter>(WeaponOwner);
	const AShooterCharacter* Shooter = Cast<AShooterCharacter>(OwnerCharacter);
	if (!bIsEquipped || !OwnerCharacter || !OwnerCharacter->IsLocallyControlled()
		|| (Shooter && Shooter->GetCurrentWeapon() != this))
	{
		StopTargetSearch();
		return;
	}

	FHitResult TargetHit;
	AActor* NewTarget = FindBestMeleeTarget(TargetHit, false, true)
		? TargetHit.GetActor()
		: nullptr;
	const bool bTargetChanged = CurrentMeleeTarget.Get() != NewTarget;
	SetCurrentMeleeTarget(NewTarget);

	CurrentTargetSearchInterval = CalculateNextTargetSearchInterval(
		CurrentTargetSearchInterval,
		NewTarget != nullptr,
		bTargetChanged,
		InitialTargetSearchInterval,
		TargetSearchIntervalStep,
		MaxTargetSearchInterval);
	ScheduleTargetSearch(CurrentTargetSearchInterval);
}

void AMeleeWeaponBase::StopTargetSearch()
{
	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(TargetSearchTimerHandle);
	}
	SetCurrentMeleeTarget(nullptr);
	CurrentTargetSearchInterval = FMath::Max(InitialTargetSearchInterval, 0.01f);
}

void AMeleeWeaponBase::SetCurrentMeleeTarget(AActor* NewTarget)
{
	AActor* PreviousTarget = CurrentMeleeTarget.Get();
	if (PreviousTarget == NewTarget)
	{
		return;
	}

	if (IsValid(PreviousTarget)
		&& PreviousTarget->GetClass()->ImplementsInterface(UMeleeTargetInterface::StaticClass()))
	{
		IMeleeTargetInterface::Execute_SetMeleeTargeted(PreviousTarget, WeaponOwner, false);
	}

	CurrentMeleeTarget = NewTarget;
	if (IsValid(NewTarget)
		&& NewTarget->GetClass()->ImplementsInterface(UMeleeTargetInterface::StaticClass()))
	{
		IMeleeTargetInterface::Execute_SetMeleeTargeted(NewTarget, WeaponOwner, true);
	}
}

float AMeleeWeaponBase::CalculateNextTargetSearchInterval(
	float CurrentInterval,
	bool bHasTarget,
	bool bTargetChanged,
	float InitialInterval,
	float IntervalStep,
	float MaxInterval)
{
	const float SafeInitialInterval = FMath::Max(InitialInterval, 0.01f);
	const float SafeMaxInterval = FMath::Max(MaxInterval, SafeInitialInterval);
	if (bHasTarget || bTargetChanged)
	{
		return SafeInitialInterval;
	}

	return FMath::Min(
		FMath::Max(CurrentInterval, SafeInitialInterval) + FMath::Max(IntervalStep, 0.0f),
		SafeMaxInterval);
}

void AMeleeWeaponBase::TraceMeleeHit()
{
	if (!HasAuthority())
	{
		return;
	}

	FVector TraceStart;
	FVector TraceEnd;
	if (!GetMeleeTraceSocketLocations(TraceStart, TraceEnd))
	{
		LogMissingMeleeTraceSockets();
		return;
	}

	HitActorsThisSwing.Reset();
	SweepMeleeTrace(TraceStart, TraceEnd, TraceStart, TraceEnd);
}

bool AMeleeWeaponBase::BeginMeleeTrace()
{
	if (!HasAuthority() || AttackPhase != EMeleeAttackPhase::Attack || !bIsAttacking)
	{
		return false;
	}

	FVector TraceStart;
	FVector TraceEnd;
	const bool bHasTraceSockets = GetMeleeTraceSocketLocations(TraceStart, TraceEnd);
	if (!bHasTraceSockets)
	{
		LogMissingMeleeTraceSockets();
	}

	ResetMeleeTraceState();
	ActiveMeleeTraceSequence = AttackSequence;
	PreviousMeleeTraceStart = TraceStart;
	PreviousMeleeTraceEnd = TraceEnd;
	bMeleeTraceActive = bHasTraceSockets;

	CommitAttack(AttackSequence);
	if (bMeleeTraceActive && AttackPhase == EMeleeAttackPhase::Recovery)
	{
		SweepMeleeTrace(TraceStart, TraceEnd, TraceStart, TraceEnd);
	}
	return bMeleeTraceActive;
}

void AMeleeWeaponBase::TickMeleeTrace()
{
	if (!HasAuthority() || !bMeleeTraceActive
		|| ActiveMeleeTraceSequence != AttackSequence
		|| AttackPhase != EMeleeAttackPhase::Recovery)
	{
		return;
	}

	FVector TraceStart;
	FVector TraceEnd;
	if (!GetMeleeTraceSocketLocations(TraceStart, TraceEnd))
	{
		LogMissingMeleeTraceSockets();
		ResetMeleeTraceState();
		return;
	}

	SweepMeleeTrace(
		PreviousMeleeTraceStart,
		PreviousMeleeTraceEnd,
		TraceStart,
		TraceEnd);
	PreviousMeleeTraceStart = TraceStart;
	PreviousMeleeTraceEnd = TraceEnd;
}

void AMeleeWeaponBase::EndMeleeTrace()
{
	ResetMeleeTraceState();
}

bool AMeleeWeaponBase::GetMeleeTraceSocketLocations(FVector& OutStart, FVector& OutEnd) const
{
	const USkeletalMeshComponent* WeaponMesh = GetThirdPersonWeaponMesh();
	if (!WeaponMesh
		|| !WeaponMesh->DoesSocketExist(MeleeTraceStartSocketName)
		|| !WeaponMesh->DoesSocketExist(MeleeTraceEndSocketName))
	{
		return false;
	}

	OutStart = WeaponMesh->GetSocketLocation(MeleeTraceStartSocketName);
	OutEnd = WeaponMesh->GetSocketLocation(MeleeTraceEndSocketName);
	return true;
}

void AMeleeWeaponBase::SweepMeleeTrace(
	const FVector& PreviousStart,
	const FVector& PreviousEnd,
	const FVector& CurrentStart,
	const FVector& CurrentEnd)
{
	if (!bCanHitMultipleTargets && !HitActorsThisSwing.IsEmpty())
	{
		return;
	}

	UWorld* World = GetWorld();
	ACharacter* OwnerCharacter = Cast<ACharacter>(WeaponOwner);
	if (!World || !OwnerCharacter)
	{
		return;
	}

	const float SafeTraceRadius = FMath::Max(TraceRadius, 0.0f);
	const float BladeLength = FMath::Max(
		FVector::Distance(PreviousStart, PreviousEnd),
		FVector::Distance(CurrentStart, CurrentEnd));
	const float SampleSpacing = FMath::Max(SafeTraceRadius * 1.5f, 1.0f);
	const int32 SampleCount = FMath::Clamp(FMath::CeilToInt(BladeLength / SampleSpacing) + 1, 2, 16);

	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjectQueryParams.AddObjectTypesToQuery(ECC_PhysicsBody);

	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(MeleeSocketTrace), false, OwnerCharacter);
	TraceParams.AddIgnoredActor(this);
	FVector ViewLocation = OwnerCharacter->GetActorLocation();
	FRotator ViewRotation;
	if (AController* OwnerController = OwnerCharacter->GetController())
	{
		OwnerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	}

	for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
	{
		const float Alpha = static_cast<float>(SampleIndex) / static_cast<float>(SampleCount - 1);
		const FVector SweepStart = FMath::Lerp(PreviousStart, PreviousEnd, Alpha);
		const FVector SweepEnd = FMath::Lerp(CurrentStart, CurrentEnd, Alpha);
		MeleeTraceHitBuffer.Reset();
		World->SweepMultiByObjectType(
			MeleeTraceHitBuffer,
			SweepStart,
			SweepEnd,
			FQuat::Identity,
			ObjectQueryParams,
			FCollisionShape::MakeSphere(SafeTraceRadius),
			TraceParams);

#if ENABLE_DRAW_DEBUG
		if (bDrawDebugMeleeTrace)
		{
			DrawDebugLine(World, SweepStart, SweepEnd, FColor::Red, false, DebugMeleeTraceDuration, 0, 1.0f);
			DrawDebugSphere(World, SweepEnd, SafeTraceRadius, 12, FColor::Red, false, DebugMeleeTraceDuration);
		}
#endif

		for (const FHitResult& HitResult : MeleeTraceHitBuffer)
		{
			AActor* Candidate = HitResult.GetActor();
			if (!IsValid(Candidate) || HitActorsThisSwing.Contains(Candidate)
				|| !IsValidMeleeTarget(Candidate, true, false))
			{
				continue;
			}

			FHitResult VisibilityHit;
			FCollisionQueryParams VisibilityParams(SCENE_QUERY_STAT(MeleeSocketVisibility), false, OwnerCharacter);
			VisibilityParams.AddIgnoredActor(this);
			const bool bVisibilityBlocked = World->LineTraceSingleByChannel(
				VisibilityHit,
				ViewLocation,
				Candidate->GetActorLocation(),
				ECC_Visibility,
				VisibilityParams);
			if (bVisibilityBlocked && VisibilityHit.GetActor() != Candidate)
			{
				continue;
			}

			HitActorsThisSwing.Add(Candidate);
			ApplyHitToTarget(Candidate, HitResult);
			if (!bCanHitMultipleTargets)
			{
				return;
			}
		}
	}
}

void AMeleeWeaponBase::ResetMeleeTraceState()
{
	bMeleeTraceActive = false;
	ActiveMeleeTraceSequence = 0;
	PreviousMeleeTraceStart = FVector::ZeroVector;
	PreviousMeleeTraceEnd = FVector::ZeroVector;
	HitActorsThisSwing.Reset();
}

void AMeleeWeaponBase::LogMissingMeleeTraceSockets() const
{
	const USkeletalMeshComponent* WeaponMesh = GetThirdPersonWeaponMesh();
	UE_LOG(
		LogOutlier,
		Warning,
		TEXT("[MeleeTrace] Missing trace socket. Weapon=%s Mesh=%s StartSocket=%s StartExists=%d EndSocket=%s EndExists=%d"),
		*GetNameSafe(this),
		*GetNameSafe(WeaponMesh),
		*MeleeTraceStartSocketName.ToString(),
		WeaponMesh && WeaponMesh->DoesSocketExist(MeleeTraceStartSocketName) ? 1 : 0,
		*MeleeTraceEndSocketName.ToString(),
		WeaponMesh && WeaponMesh->DoesSocketExist(MeleeTraceEndSocketName) ? 1 : 0);
}

bool AMeleeWeaponBase::FindBestMeleeTarget(
	FHitResult& OutHit,
	bool bIncludePartner,
	bool bRequireIndicatorEligibility) const
{
	OutHit = FHitResult();
	ACharacter* OwnerCharacter = Cast<ACharacter>(WeaponOwner);
	AController* OwnerController = OwnerCharacter ? OwnerCharacter->GetController() : nullptr;
	UWorld* World = GetWorld();
	if (!OwnerCharacter || !OwnerController || !World)
	{
		return false;
	}

	FVector TraceStart;
	FRotator ViewRotation;
	OwnerController->GetPlayerViewPoint(TraceStart, ViewRotation);
	const FVector TraceDirection = ViewRotation.Vector().GetSafeNormal();
	const FVector TraceEnd = TraceStart + TraceDirection * FMath::Max(TargetSearchRange, 0.0f);

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
		FCollisionShape::MakeSphere(FMath::Max(TargetSearchRadius, 0.0f)),
		TraceParams);

	float BestAlignment = -1.0f;
	float BestDistanceSquared = TNumericLimits<float>::Max();
	TArray<AActor*, TInlineAllocator<8>> EvaluatedTargets;

	for (const FHitResult& HitResult : HitResults)
	{
		AActor* Candidate = HitResult.GetActor();
		if (!IsValid(Candidate) || EvaluatedTargets.Contains(Candidate))
		{
			continue;
		}
		EvaluatedTargets.Add(Candidate);

		if (!IsValidMeleeTarget(Candidate, bIncludePartner, bRequireIndicatorEligibility))
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
		if (!OutHit.GetActor()
			|| Alignment > BestAlignment + AlignmentTolerance
			|| (FMath::IsNearlyEqual(Alignment, BestAlignment, AlignmentTolerance)
				&& DistanceSquared < BestDistanceSquared))
		{
			OutHit = HitResult;
			BestAlignment = Alignment;
			BestDistanceSquared = DistanceSquared;
		}
	}

	return OutHit.GetActor() != nullptr;
}

bool AMeleeWeaponBase::IsValidMeleeTarget(
	AActor* Candidate,
	bool bIncludePartner,
	bool bRequireIndicatorEligibility) const
{
	if (const AEnemyBase* Enemy = Cast<AEnemyBase>(Candidate))
	{
		const bool bValidEnemy = !Enemy->IsDead()
			&& Enemy->CanBeDamaged()
			&& Enemy->GetGenericTeamId().GetId() == OutlierTeamIds::Enemy;
		if (!bValidEnemy || !bRequireIndicatorEligibility)
		{
			return bValidEnemy;
		}

		return Candidate->GetClass()->ImplementsInterface(UMeleeTargetInterface::StaticClass())
			&& IMeleeTargetInterface::Execute_CanShowMeleeTargetIndicator(Candidate, WeaponOwner);
	}

	if (!bIncludePartner)
	{
		return false;
	}

	const APartnerCharacter* Partner = Cast<APartnerCharacter>(Candidate);
	const UOutlierVitalAttributeSet* Vital = Partner ? Partner->GetVitalAttributeSet() : nullptr;
	const UPartnerVitalityComponent* Vitality = Partner ? Partner->GetPartnerVitalityComponent() : nullptr;
	return Partner
		&& Partner->CanBeDamaged()
		&& Vital && Vital->GetHealth() > 0.0f
		&& Vitality && !Vitality->IsRebooting();
}

void AMeleeWeaponBase::ApplyHitToTarget(AActor* Target)
{
	FHitResult HitResult;
	HitResult.ImpactPoint = IsValid(Target) ? Target->GetActorLocation() : FVector::ZeroVector;
	HitResult.ImpactNormal = IsValid(Target) && IsValid(WeaponOwner)
		? (WeaponOwner->GetActorLocation() - Target->GetActorLocation()).GetSafeNormal()
		: FVector::ZeroVector;
	ApplyHitToTarget(Target, HitResult);
}

void AMeleeWeaponBase::ApplyHitToTarget(AActor* Target, const FHitResult& HitResult)
{
	AEnemyBase* Enemy = Cast<AEnemyBase>(Target);
	APartnerCharacter* Partner = Cast<APartnerCharacter>(Target);
	if (!HasAuthority() || (!IsValid(Enemy) && !IsValid(Partner))
		|| !Target->CanBeDamaged())
	{
		return;
	}

	float DamageToApply = Damage;
	EMeleeHitResultType ResultType = EMeleeHitResultType::PartnerDamage;
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
		ResultType = bInstantKill
			? EMeleeHitResultType::EnemyInstantKill
			: EMeleeHitResultType::EnemyDamage;
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
	const AEnemyBase* OwnerEnemy = Cast<AEnemyBase>(WeaponOwner);
	const bool bPlayerAttributedAttack = Cast<AShooterCharacter>(WeaponOwner)
		|| Cast<APartnerCharacter>(WeaponOwner)
		|| (OwnerEnemy && OwnerEnemy->IsEnemyPossessed());
	DamageRequest.AdaptationDamageCategory = bPlayerAttributedAttack
		? EOutlierAdaptationDamageCategory::NonGun
		: EOutlierAdaptationDamageCategory::Ignore;
	DamageRequest.DamageOrigin = IsValid(WeaponOwner)
		? WeaponOwner->GetActorLocation()
		: GetActorLocation();
	DamageRequest.EventInstigator = IsValid(WeaponOwner) ? WeaponOwner->GetController() : nullptr;
	DamageRequest.DamageCauser = this;
	DamageRequest.HitResult = HitResult;
	if (OutlierDamage::Apply(Target, DamageRequest) <= 0.0f)
	{
		return;
	}

	FMeleeHitContext Context;
	Context.TargetActor = Target;
	Context.HitLocation = HitResult.ImpactPoint;
	Context.HitNormal = HitResult.ImpactNormal;
	Context.AttackSequence = AttackSequence;
	Context.ResultType = ResultType;
	NotifyMeleeHitResult(Context);
}

void AMeleeWeaponBase::NotifyMeleeHitResult(const FMeleeHitContext& Context)
{
	AActor* Target = Context.TargetActor.Get();
	if (!HasAuthority() || !IsValid(Target)
		|| !Target->GetClass()->ImplementsInterface(UMeleeTargetInterface::StaticClass()))
	{
		return;
	}

	IMeleeTargetInterface::Execute_HandleMeleeHitConfirmed(Target, Context);
	ClientNotifyMeleeHitFeedback(Context);
}

void AMeleeWeaponBase::ClientNotifyMeleeHitFeedback_Implementation(const FMeleeHitContext& Context)
{
	AActor* Target = Context.TargetActor.Get();
	if (IsValid(Target)
		&& Target->GetClass()->ImplementsInterface(UMeleeTargetInterface::StaticClass()))
	{
		IMeleeTargetInterface::Execute_HandleMeleeHitFeedback(Target, Context);
	}
}
