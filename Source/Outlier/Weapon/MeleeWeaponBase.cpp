// Fill out your copyright notice in the Description page of Project Settings.


#include "Weapon/MeleeWeaponBase.h"
#include "Audio/OutlierAudioFeedback.h"
#include "Outlier.h"
#include "Damage/OutlierDamageReceiver.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerVitalityComponent.h"
#include "Enemy/EnemyBase.h"
#include "Enemy/AutoTurret.h"
#include "Explosion/ExplosiveProp.h"
#include "EngineUtils.h"
#include "GAS/Attributes/OutlierVitalAttributeSet.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "OutlierPlayerState.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterAnimInstance.h"
#include "HAL/IConsoleManager.h"
#include "Team/OutlierTeamIds.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Volume.h"
#include "Interface/MeleeIgnoreInterface.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/ActiveMontageInstanceScope.h"
#include "Animation/AnimNotifyQueue.h"
#include "Components/SkeletalMeshComponent.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "TrailEffectDefinition.h"
#include "VisualEventSubsystem.h"

#if ENABLE_DRAW_DEBUG
#include "DrawDebugHelpers.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogMeleeDiagnostics, Log, All);

static TAutoConsoleVariable<int32> CVarMeleePoseDiagnostics(
	TEXT("outlier.Weapon.MeleePoseDiagnostics"), 0,
	TEXT("Log final third-person melee sockets, attachment and aim on each authoritative trace sample."),
	ECVF_Cheat);

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorldAndArgs MeleeTraceDebugCommand(
	TEXT("outlier.Weapon.MeleeTraceDebug"),
	TEXT("Show authoritative melee sweeps on relevant clients: [1=on, 0=off] [lifetime seconds=3]. Run on owning client, or server for all equipped melee weapons."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World) return;
		const bool bEnabled = Args.IsEmpty() || FCString::Atoi(*Args[0]) != 0;
		const float Duration = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 3.0f;
		int32 Count = 0;
		for (TActorIterator<AMeleeWeaponBase> It(World); It; ++It)
		{
			const APawn* OwnerPawn = Cast<APawn>(It->GetOwner());
			if (OwnerPawn && (It->HasAuthority() || OwnerPawn->IsLocallyControlled()))
			{
				It->SetMeleeTraceDebug(bEnabled, Duration);
				++Count;
			}
		}
		UE_LOG(LogMeleeDiagnostics, Display, TEXT("[MeleeDebug] Requested=%d Weapons=%d. Red=sweep Yellow=TP sockets Cyan=server aim Green=applied damage"), bEnabled, Count);
	}), ECVF_Cheat);
#endif

void AMeleeWeaponBase::SetMeleeTraceDebug(bool bEnabled, float Duration)
{
#if !UE_BUILD_SHIPPING
	if (HasAuthority()) ServerSetMeleeTraceDebug_Implementation(bEnabled, Duration);
	else ServerSetMeleeTraceDebug(bEnabled, Duration);
#endif
}

void AMeleeWeaponBase::ServerSetMeleeTraceDebug_Implementation(bool bEnabled, float Duration)
{
#if !UE_BUILD_SHIPPING
	if (!HasAuthority() || (bEnabled && (!bIsEquipped || !IsValid(WeaponOwner)))) return;
	bServerTraceDebugRequested = bEnabled;
	ServerTraceDebugDuration = FMath::IsFinite(Duration) ? FMath::Clamp(Duration, 0.1f, 10.0f) : 3.0f;
	UE_LOG(LogMeleeDiagnostics, Display, TEXT("[MeleeDebug] Server Weapon=%s Enabled=%d Duration=%.1f"),
		*GetNameSafe(this), bEnabled, ServerTraceDebugDuration);
#endif
}

void AMeleeWeaponBase::MulticastDrawMeleeTrace_Implementation(
	FVector_NetQuantize10 PreviousStart, FVector_NetQuantize10 PreviousEnd,
	FVector_NetQuantize10 CurrentStart, FVector_NetQuantize10 CurrentEnd, float Radius, uint8 Samples,
	FVector_NetQuantize10 Eye, FVector_NetQuantizeNormal Aim, float Duration)
{
#if ENABLE_DRAW_DEBUG && !UE_BUILD_SHIPPING
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer) return;
	DrawDebugLine(World, CurrentStart, CurrentEnd, FColor::Yellow, false, Duration, 0, 1.5f);
	DrawDebugDirectionalArrow(World, Eye, FVector(Eye) + FVector(Aim) * 150.0f,
		10.0f, FColor::Cyan, false, Duration, 0, 1.0f);
	// 클라이언트 애니메이션에서 재계산하지 않고 서버가 검사한 각 Sweep 체적을 표시한다.
	for (int32 Index = 0; Index < Samples; ++Index)
	{
		const float Alpha = static_cast<float>(Index) / FMath::Max(static_cast<int32>(Samples) - 1, 1);
		const FVector Start = FMath::Lerp(FVector(PreviousStart), FVector(PreviousEnd), Alpha);
		const FVector End = FMath::Lerp(FVector(CurrentStart), FVector(CurrentEnd), Alpha);
		const FVector Delta = End - Start;
		DrawDebugCapsule(World, (Start + End) * 0.5f, Delta.Size() * 0.5f + Radius, Radius,
			FQuat::FindBetweenNormals(FVector::UpVector, Delta.GetSafeNormal(SMALL_NUMBER, FVector::UpVector)),
			FColor::Red, false, Duration, 0, 0.6f);
	}
#endif
}

void AMeleeWeaponBase::MulticastDrawMeleeDamage_Implementation(FVector_NetQuantize10 Location, float Duration)
{
#if ENABLE_DRAW_DEBUG && !UE_BUILD_SHIPPING
	if (GetWorld() && GetNetMode() != NM_DedicatedServer)
	{
		DrawDebugSphere(GetWorld(), Location, 5.0f, 12, FColor::Green, false, Duration, 0, 2.0f);
	}
#endif
}

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
	bServerTraceDebugRequested = false;
	StopTargetSearch();
	Super::OnUnequipped();
}

void AMeleeWeaponBase::OnDropped(const FTransform& DropTransform, AFirstPersonCharacter* DroppedBy)
{
	bServerTraceDebugRequested = false;
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
		&& (!Shooter || (!Shooter->IsDead()
			&& (!Shooter->IsSliding() || Shooter->AllowsMeleeAttackWhileSliding())));
}

void AMeleeWeaponBase::StartAttack()
{
	const AShooterCharacter* Shooter = Cast<AShooterCharacter>(WeaponOwner);
	UE_LOG(LogMeleeDiagnostics, Log, TEXT("[MeleeDiag][Start] Weapon=%s Owner=%s Authority=%d NetMode=%d Equipped=%d CanAttack=%d Phase=%d Sequence=%d Attacking=%d"),
		*GetNameSafe(this), *GetNameSafe(WeaponOwner), HasAuthority(), static_cast<int32>(GetNetMode()),
		bIsEquipped, CanAttack(), static_cast<int32>(AttackPhase), AttackSequence, bIsAttacking);
	if (!HasAuthority() || !Super::CanAttack()
		|| (Shooter && (Shooter->IsDead()
			|| (Shooter->IsSliding() && !Shooter->AllowsMeleeAttackWhileSliding()))))
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
	// Notify의 실제 피해까지 같은 공격 시작 자격을 보존한다.
	// 일반/자동 은신 해제는 애니메이션과 피해 실행보다 먼저 끝낸다.
	SwingStealthSession.Reset();
	if (AShooterCharacter* OwnerShooter = Cast<AShooterCharacter>(WeaponOwner))
	{
		if (UOutlierAbilitySystemComponent* AbilitySystem = OwnerShooter->GetOutlierAbilitySystemComponent())
		{
			SwingStealthSession = AbilitySystem->BeginShooterStealthAttack();
		}
	}
	else if (APartnerCharacter* Partner = Cast<APartnerCharacter>(WeaponOwner))
	{
		Partner->NotifyOffensiveActionExecuted();
	}
	else if (AEnemyBase* Enemy = Cast<AEnemyBase>(WeaponOwner); Enemy && Enemy->IsEnemyPossessed())
	{
		if (AOutlierPlayerState* PlayerState = Enemy->GetPlayerState<AOutlierPlayerState>())
		{
			if (AShooterCharacter* PairShooter = PlayerState->GetShooterCharacter())
			{
				PairShooter->NotifyOffensiveActionExecuted();
			}
		}
	}
	AttackSequence = AttackSequence == MAX_int32 ? 1 : AttackSequence + 1;
	bPlayedMeleeHitSoundThisAttack = false;
	AttackPhase = EMeleeAttackPhase::Attack;
	RefreshOwnerCombatState();
	ForceNetUpdate();

	if (AShooterCharacter* OwnerShooter = Cast<AShooterCharacter>(WeaponOwner))
	{
		OwnerShooter->SetMeleeTracePoseRefreshEnabled(true);
	}
	bUsesAnimationTiming = PlayAttackAnimation();
	UE_LOG(LogMeleeDiagnostics, Log, TEXT("[MeleeDiag][Timing] Weapon=%s Sequence=%d AnimationTiming=%d AttackDelay=%.3f RecoveryDuration=%.3f"),
		*GetNameSafe(this), AttackSequence, bUsesAnimationTiming, AttackDelay, RecoveryDuration);
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

	UE_LOG(LogMeleeDiagnostics, Log, TEXT("[MeleeDiag][Stop] Weapon=%s Sequence=%d Phase=%d TraceActive=%d UniqueHits=%d"),
		*GetNameSafe(this), AttackSequence, static_cast<int32>(AttackPhase), bMeleeTraceActive, HitActorsThisSwing.Num());
	bWantsToAttack = false;
	bUsesAnimationTiming = false;
	AttackAnimInstance.Reset();
	AttackMontageInstanceId = INDEX_NONE;
	SwingStealthSession.Reset();
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
	UE_LOG(LogMeleeDiagnostics, Log, TEXT("[MeleeDiag][Commit] Weapon=%s Authority=%d ExpectedSequence=%d Sequence=%d Phase=%d Attacking=%d AnimationTiming=%d"),
		*GetNameSafe(this), HasAuthority(), ExpectedAttackSequence, AttackSequence, static_cast<int32>(AttackPhase), bIsAttacking, bUsesAnimationTiming);
	// Phase rejects duplicates/cancellation; sequence rejects callbacks from a previous swing.
	if (!HasAuthority() || ExpectedAttackSequence != AttackSequence
		|| AttackPhase != EMeleeAttackPhase::Attack || !bIsAttacking)
	{
		return;
	}

	if (Cast<AShooterCharacter>(WeaponOwner))
		OutlierAudioFeedback::World(this, TEXT("Audio.Context.Player.01.Type.Melee.Swing"), TEXT("Audio.Type.Weapon"));
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
	// Montage_Play가 이전 몽타주를 중단하며 Notify를 전달할 수 있으므로 재생 전에 이전 수명을 닫는다.
	AttackAnimInstance.Reset();
	AttackMontageInstanceId = INDEX_NONE;
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
	UE_LOG(LogMeleeDiagnostics, Log, TEXT("[MeleeDiag][Montage] Weapon=%s Sequence=%d Authority=%d Montage=%s AnimInstance=%s Playing=%d MeshTickEnabled=%d AnimTickOption=%d"),
		*GetNameSafe(this), AttackSequence, HasAuthority(), *GetNameSafe(ThirdPersonMontage), *GetNameSafe(ThirdPersonAnimInstance),
		ThirdPersonMontage && ThirdPersonAnimInstance && ThirdPersonAnimInstance->Montage_IsPlaying(ThirdPersonMontage),
		Shooter->GetMesh() && Shooter->GetMesh()->IsComponentTickEnabled(),
		Shooter->GetMesh() ? static_cast<int32>(Shooter->GetMesh()->VisibilityBasedAnimTickOption) : -1);
	if (!ThirdPersonMontage || !ThirdPersonAnimInstance
		|| !ThirdPersonAnimInstance->Montage_IsPlaying(ThirdPersonMontage))
	{
		return false;
	}

	const FAnimMontageInstance* Instance = ThirdPersonAnimInstance->GetActiveInstanceForMontage(ThirdPersonMontage);
	if (!Instance)
	{
		return false;
	}
	AttackAnimInstance = ThirdPersonAnimInstance;
	AttackMontageInstanceId = Instance->GetInstanceID();

	FOnMontageEnded MontageEndedDelegate;
	MontageEndedDelegate.BindUObject(
		this,
		&AMeleeWeaponBase::HandleAttackMontageEnded,
		AttackSequence);
	ThirdPersonAnimInstance->Montage_SetEndDelegate(MontageEndedDelegate, ThirdPersonMontage);
	return true;
}

bool AMeleeWeaponBase::IsCurrentAttackNotify(
	const USkeletalMeshComponent* MeshComp,
	const FAnimNotifyEventReference& EventReference) const
{
	const AShooterCharacter* Shooter = Cast<AShooterCharacter>(WeaponOwner);
	const auto* MontageContext = EventReference.GetContextData<UE::Anim::FAnimNotifyMontageInstanceContext>();
	// 공유 Notify 객체에는 공격별 상태를 저장하지 않는다. TP 메시와 시작 당시 몽타주 인스턴스로 수명을 확인한다.
	return HasAuthority() && bIsEquipped && Shooter && Shooter->GetCurrentWeapon() == this
		&& MeshComp == Shooter->GetMesh() && AttackPhase != EMeleeAttackPhase::Idle
		&& AttackAnimInstance.IsValid() && MeshComp->GetAnimInstance() == AttackAnimInstance.Get()
		&& MontageContext && AttackMontageInstanceId != INDEX_NONE
		&& MontageContext->MontageInstanceID == AttackMontageInstanceId;
}

void AMeleeWeaponBase::HandleAttackMontageEnded(
	UAnimMontage* /*Montage*/,
	bool bInterrupted,
	int32 ExpectedAttackSequence)
{
	UE_LOG(LogMeleeDiagnostics, Log, TEXT("[MeleeDiag][MontageEnd] Weapon=%s ExpectedSequence=%d Sequence=%d Phase=%d Interrupted=%d TraceActive=%d UniqueHits=%d"),
		*GetNameSafe(this), ExpectedAttackSequence, AttackSequence, static_cast<int32>(AttackPhase), bInterrupted, bMeleeTraceActive, HitActorsThisSwing.Num());
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
		&& (!Shooter || (!Shooter->IsDead() && Shooter->GetCurrentWeapon() == this));
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
		|| (Shooter && (Shooter->IsDead() || Shooter->GetCurrentWeapon() != this)))
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
	SurfaceActorsThisSwing.Reset();
	SweepMeleeTrace(TraceStart, TraceEnd, TraceStart, TraceEnd);
}

bool AMeleeWeaponBase::BeginMeleeTrace()
{
	UE_LOG(LogMeleeDiagnostics, Log, TEXT("[MeleeDiag][TraceBegin] Weapon=%s Authority=%d Sequence=%d Phase=%d Attacking=%d"),
		*GetNameSafe(this), HasAuthority(), AttackSequence, static_cast<int32>(AttackPhase), bIsAttacking);
	if (!HasAuthority() || AttackPhase != EMeleeAttackPhase::Attack || !bIsAttacking)
	{
		return false;
	}
	if (AAutoTurret::IsTurretDiagnosticsEnabled() && GetWorld() && IsValid(WeaponOwner))
	{
		// 접촉 로그가 없어도 주변 터렛의 서버 충돌 설정을 타격 시작 시 한 번 확인한다.
		for (TActorIterator<AAutoTurret> It(GetWorld()); It; ++It)
		{
			AAutoTurret* Turret = *It;
			const float Distance = FVector::Distance(WeaponOwner->GetActorLocation(), Turret->GetActorLocation());
			if (Distance > 1500.0f) continue;
			UE_LOG(LogMeleeDiagnostics, Log, TEXT("[MeleeDiag][TurretSnapshot] Weapon=%s Sequence=%d Turret=%s Distance=%.1f Team=%d CanBeDamaged=%d Dead=%d HP=%.2f Lifecycle=%d Hidden=%d"),
				*GetNameSafe(this), AttackSequence, *GetNameSafe(Turret), Distance, Turret->GetGenericTeamId().GetId(), Turret->CanBeDamaged(),
				Turret->IsDead(), Turret->GetCurrentHealth(), static_cast<int32>(Turret->GetTurretLifecycleState()), Turret->IsHidden());
			TInlineComponentArray<UPrimitiveComponent*> Components;
			Turret->GetComponents(Components);
			for (const UPrimitiveComponent* Component : Components)
			{
				const ECollisionChannel ObjectType = Component->GetCollisionObjectType();
				const bool bQueriedType = ObjectType == ECC_Pawn || ObjectType == ECC_PhysicsBody || ObjectType == ECC_WorldDynamic || ObjectType == ECC_WorldStatic;
				UE_LOG(LogMeleeDiagnostics, Log, TEXT("[MeleeDiag][TurretCollision] Weapon=%s Sequence=%d Turret=%s Component=%s Registered=%d CollisionEnabled=%d QueryEnabled=%d ObjectType=%d InSweepObjectTypes=%d VisibilityResponse=%d Location=%s BoundsOrigin=%s BoundsExtent=%s"),
					*GetNameSafe(this), AttackSequence, *GetNameSafe(Turret), *GetNameSafe(Component), Component->IsRegistered(),
					static_cast<int32>(Component->GetCollisionEnabled()), Component->IsQueryCollisionEnabled(), static_cast<int32>(ObjectType), bQueriedType,
					static_cast<int32>(Component->GetCollisionResponseToChannel(ECC_Visibility)), *Component->GetComponentLocation().ToCompactString(),
					*Component->Bounds.Origin.ToCompactString(), *Component->Bounds.BoxExtent.ToCompactString());
			}
		}
	}

	FVector TraceStart;
	FVector TraceEnd;
	const bool bHasTraceSockets = GetMeleeTraceSocketLocations(TraceStart, TraceEnd);
	UE_LOG(LogMeleeDiagnostics, Log, TEXT("[MeleeDiag][Sockets] Weapon=%s Sequence=%d Mesh=%s Valid=%d StartSocket=%s EndSocket=%s Start=%s End=%s"),
		*GetNameSafe(this), AttackSequence, *GetNameSafe(GetThirdPersonWeaponMesh()), bHasTraceSockets,
		*MeleeTraceStartSocketName.ToString(), *MeleeTraceEndSocketName.ToString(),
		bHasTraceSockets ? *TraceStart.ToString() : TEXT("Unavailable"), bHasTraceSockets ? *TraceEnd.ToString() : TEXT("Unavailable"));
	if (!bHasTraceSockets)
	{
		LogMissingMeleeTraceSockets();
	}

	ResetMeleeTraceState();
	ActiveMeleeTraceSequence = AttackSequence;
	ActiveMeleeTraceOwner = WeaponOwner;
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
	// 좌표 이력은 Trace를 시작한 소유자의 손 궤적이다. 소유자가 바뀌면 새 손까지 이어서 Sweep하지 않는다.
	const AShooterCharacter* Shooter = Cast<AShooterCharacter>(WeaponOwner);
	if (!ActiveMeleeTraceOwner.IsValid() || ActiveMeleeTraceOwner.Get() != WeaponOwner
		|| !bIsEquipped || (Shooter && Shooter->GetCurrentWeapon() != this))
	{
		UE_LOG(LogMeleeDiagnostics, Warning, TEXT("[MeleeDiag][TraceRejected] Weapon=%s Sequence=%d Reason=OwnerOrEquipmentChanged"),
			*GetNameSafe(this), AttackSequence);
		ResetMeleeTraceState();
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
	UE_LOG(LogMeleeDiagnostics, Log, TEXT("[MeleeDiag][TraceEnd] Weapon=%s Sequence=%d Phase=%d Active=%d UniqueHits=%d"),
		*GetNameSafe(this), AttackSequence, static_cast<int32>(AttackPhase), bMeleeTraceActive, HitActorsThisSwing.Num());
	ResetMeleeTraceState();
}

bool AMeleeWeaponBase::GetMeleeTraceSocketLocations(FVector& OutStart, FVector& OutEnd) const
{
	USkeletalMeshComponent* WeaponMesh = GetThirdPersonWeaponMesh();
	if (!WeaponMesh
		|| !WeaponMesh->DoesSocketExist(MeleeTraceStartSocketName)
		|| !WeaponMesh->DoesSocketExist(MeleeTraceEndSocketName))
	{
		return false;
	}

	// Notify는 손 bone 확정 후, 자식 부착 변환 갱신 전에 호출될 수 있다.
	// 현재 손 소켓으로 무기 변환만 갱신한다. pose 재평가는 Notify 재진입을 만들므로 하지 않는다.
	WeaponMesh->UpdateComponentToWorld();
	OutStart = WeaponMesh->GetSocketLocation(MeleeTraceStartSocketName);
	OutEnd = WeaponMesh->GetSocketLocation(MeleeTraceEndSocketName);
	if (HasAuthority() && CVarMeleePoseDiagnostics.GetValueOnGameThread() != 0)
	{
		const AShooterCharacter* Shooter = Cast<AShooterCharacter>(WeaponOwner);
		const USceneComponent* Parent = WeaponMesh->GetAttachParent();
		const FName AttachSocket = WeaponMesh->GetAttachSocketName();
		// 부모의 최신 손 소켓과 무기 월드 변환을 함께 기록해 AO 문제와 부착 갱신 문제를 구분한다.
		// 여기서 pose를 재평가하면 Notify가 재진입할 수 있으므로 읽기만 수행한다.
		const FTransform ParentSocket = Parent
			? Parent->GetSocketTransform(AttachSocket) : FTransform::Identity;
		const FTransform ExpectedWeapon = WeaponMesh->GetRelativeTransform() * ParentSocket;
		const UShooterAnimInstance* Anim = Shooter && Shooter->GetMesh()
			? Cast<UShooterAnimInstance>(Shooter->GetMesh()->GetAnimInstance()) : nullptr;
		FVector EyeLocation = FVector::ZeroVector;
		FRotator EyeRotation = FRotator::ZeroRotator;
		if (WeaponOwner) WeaponOwner->GetActorEyesViewPoint(EyeLocation, EyeRotation);
		UE_LOG(LogMeleeDiagnostics, Log,
			TEXT("[MeleeDiag][Pose] Weapon=%s Sequence=%d Frame=%llu Pitch=%.2f MeleeAO=%.3f Anim=%s Parent=%s AttachSocket=%s ParentSocket=%s Relative=%s World=%s ExpectedWorld=%s Start=%s End=%s Eye=%s Aim=%s"),
			*GetNameSafe(this), AttackSequence, GFrameCounter,
			Shooter ? Shooter->GetAimPitchForAnimation() : EyeRotation.Pitch,
			Anim ? Anim->GetMeleeAimOffsetAlpha() : -1.0f, *GetNameSafe(Anim),
			*GetNameSafe(Parent), *AttachSocket.ToString(), *ParentSocket.ToString(),
			*WeaponMesh->GetRelativeTransform().ToString(), *WeaponMesh->GetComponentTransform().ToString(),
			*ExpectedWeapon.ToString(), *OutStart.ToString(), *OutEnd.ToString(),
			*EyeLocation.ToString(), *EyeRotation.ToString());
	}
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
	ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldStatic);

	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(MeleeSocketTrace), false, OwnerCharacter);
	AddWeaponTraceIgnoredActors(TraceParams);
	TraceParams.bReturnPhysicalMaterial = true;
	FVector ViewLocation = OwnerCharacter->GetActorLocation();
	FRotator ViewRotation = FRotator::ZeroRotator;
	if (AController* OwnerController = OwnerCharacter->GetController())
	{
		OwnerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	}
	UE_LOG(LogMeleeDiagnostics, Log, TEXT("[MeleeDiag][SweepPath] Weapon=%s Sequence=%d PreviousStart=%s PreviousEnd=%s CurrentStart=%s CurrentEnd=%s Radius=%.2f Samples=%d"),
		*GetNameSafe(this), AttackSequence, *PreviousStart.ToString(), *PreviousEnd.ToString(), *CurrentStart.ToString(), *CurrentEnd.ToString(), SafeTraceRadius, SampleCount);

#if !UE_BUILD_SHIPPING
	if (HasAuthority() && (bServerTraceDebugRequested || bDrawDebugMeleeTrace))
	{
		// 소켓 샘플을 클라이언트에 전달한다. 표시 RPC는 피해·Trace 상태를 바꾸지 않는다.
		MulticastDrawMeleeTrace(PreviousStart, PreviousEnd, CurrentStart, CurrentEnd, SafeTraceRadius,
			static_cast<uint8>(SampleCount), ViewLocation, ViewRotation.Vector(),
			bServerTraceDebugRequested ? ServerTraceDebugDuration : DebugMeleeTraceDuration);
	}
#endif

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
		UE_CLOG(!MeleeTraceHitBuffer.IsEmpty(), LogMeleeDiagnostics, Log, TEXT("[MeleeDiag][SweepHits] Weapon=%s Sequence=%d Sample=%d/%d Start=%s End=%s Radius=%.2f Hits=%d"),
			*GetNameSafe(this), AttackSequence, SampleIndex + 1, SampleCount, *SweepStart.ToString(), *SweepEnd.ToString(), SafeTraceRadius, MeleeTraceHitBuffer.Num());

		for (const FHitResult& HitResult : MeleeTraceHitBuffer)
		{
			AActor* Candidate = HitResult.GetActor();
			// 환경음/방 트리거 등 보이지 않는 Volume은 공격 판정과 표면 연출에서 제외한다.
			if (Candidate && (Candidate->IsA<AVolume>()
				|| Candidate->GetClass()->ImplementsInterface(UMeleeIgnoreInterface::StaticClass())))
			{
				continue;
			}

			if (Cast<AAutoTurret>(Candidate) && AAutoTurret::IsTurretDiagnosticsEnabled())
			{
				UE_LOG(LogMeleeDiagnostics, Log, TEXT("[MeleeDiag][TurretContact] Weapon=%s Sequence=%d Target=%s Component=%s Bone=%s Blocking=%d InitialOverlap=%d AlreadyHit=%d Impact=%s"),
					*GetNameSafe(this), AttackSequence, *GetNameSafe(Candidate), *GetNameSafe(HitResult.GetComponent()), *HitResult.BoneName.ToString(),
					HitResult.bBlockingHit, HitResult.bStartPenetrating, HitActorsThisSwing.Contains(Candidate), *HitResult.ImpactPoint.ToCompactString());
			}
			if (!IsValid(Candidate) || HitActorsThisSwing.Contains(Candidate)
				|| Candidate == WeaponOwner || Candidate == this)
			{
				continue;
			}

			if (!IsValidMeleeTarget(Candidate, true, false))
			{
				const AEnemyBase* Enemy = Cast<AEnemyBase>(Candidate);
				UE_LOG(LogMeleeDiagnostics, Log, TEXT("[MeleeDiag][RejectTarget] Weapon=%s Sequence=%d Target=%s CanBeDamaged=%d Enemy=%d Dead=%d Team=%d RequiredTeam=%d Component=%s Bone=%s"),
					*GetNameSafe(this), AttackSequence, *GetNameSafe(Candidate), Candidate->CanBeDamaged(), Enemy != nullptr,
					Enemy && Enemy->IsDead(), Enemy ? Enemy->GetGenericTeamId().GetId() : -1, OutlierTeamIds::Enemy,
					*GetNameSafe(HitResult.GetComponent()), *HitResult.BoneName.ToString());
				if (Cast<AShooterCharacter>(WeaponOwner) && !Cast<APawn>(Candidate))
				{
					PresentSurfaceImpact(HitResult);
				}
				continue;
			}
			FHitResult VisibilityHit;
			FCollisionQueryParams VisibilityParams(SCENE_QUERY_STAT(MeleeSocketVisibility), false, OwnerCharacter);
			AddWeaponTraceIgnoredActors(VisibilityParams);
			const bool bVisibilityBlocked = World->LineTraceSingleByChannel(
				VisibilityHit,
				ViewLocation,
				HitResult.ImpactPoint,
				ECC_Visibility,
				VisibilityParams);
			if (bVisibilityBlocked && VisibilityHit.GetActor() != Candidate)
			{
				UE_LOG(LogMeleeDiagnostics, Log, TEXT("[MeleeDiag][Occluded] Weapon=%s Sequence=%d Target=%s Blocker=%s BlockerComponent=%s BlockerBone=%s BlockerImpact=%s View=%s TargetLocation=%s HitPoint=%s"),
					*GetNameSafe(this), AttackSequence, *GetNameSafe(Candidate), *GetNameSafe(VisibilityHit.GetActor()), *GetNameSafe(VisibilityHit.GetComponent()),
					*VisibilityHit.BoneName.ToString(), *VisibilityHit.ImpactPoint.ToCompactString(), *ViewLocation.ToString(), *Candidate->GetActorLocation().ToString(), *HitResult.ImpactPoint.ToCompactString());
				continue;
			}

			HitActorsThisSwing.Add(Candidate);
			if (Cast<AShooterCharacter>(WeaponOwner))
			{
				PresentSurfaceImpact(HitResult);
			}
			ApplyHitToTarget(Candidate, HitResult);
			if (!bCanHitMultipleTargets)
			{
				return;
			}
		}
	}
}

void AMeleeWeaponBase::PlayMeleeHitSoundOnce(const FHitResult& HitResult, uint8 Surface)
{
	if (bPlayedMeleeHitSoundThisAttack || !Cast<AShooterCharacter>(WeaponOwner))
	{
		return;
	}

	bPlayedMeleeHitSoundThisAttack = true;
	PlaySurfaceImpactSound(HitResult, Surface);
}

void AMeleeWeaponBase::ResetMeleeTraceState()
{
	bMeleeTraceActive = false;
	ActiveMeleeTraceSequence = 0;
	ActiveMeleeTraceOwner.Reset();
	PreviousMeleeTraceStart = FVector::ZeroVector;
	PreviousMeleeTraceEnd = FVector::ZeroVector;
	HitActorsThisSwing.Reset();
	SurfaceActorsThisSwing.Reset();
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
	AddWeaponTraceIgnoredActors(TraceParams);

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
		AddWeaponTraceIgnoredActors(VisibilityParams);
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
	if (const AExplosiveProp* Explosive = Cast<AExplosiveProp>(Candidate))
	{
		// 폭발물은 실제 타격만 허용하고 적 전용 근접 인디케이터에서는 제외한다.
		return !bRequireIndicatorEligibility && Explosive->CanBeDamaged()
			&& !Explosive->IsExploded() && Explosive->GetCurrentHP() > 0.0f;
	}

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
	UE_LOG(LogMeleeDiagnostics, Log, TEXT("[MeleeDiag][DamageRequest] Weapon=%s Authority=%d Sequence=%d Target=%s CanBeDamaged=%d Impact=%s"),
		*GetNameSafe(this), HasAuthority(), AttackSequence, *GetNameSafe(Target), IsValid(Target) && Target->CanBeDamaged(), *HitResult.ImpactPoint.ToString());
	AEnemyBase* Enemy = Cast<AEnemyBase>(Target);
	APartnerCharacter* Partner = Cast<APartnerCharacter>(Target);
	AExplosiveProp* Explosive = Cast<AExplosiveProp>(Target);
	if (!HasAuthority() || (!IsValid(Enemy) && !IsValid(Partner) && !IsValid(Explosive))
		|| !Target->CanBeDamaged())
	{
		UE_LOG(LogMeleeDiagnostics, Log, TEXT("[MeleeDiag][DamageRejected] Weapon=%s Sequence=%d Reason=AuthorityOrTargetTypeOrDamageDisabled"), *GetNameSafe(this), AttackSequence);
		return;
	}

	float DamageToApply = Damage;
	EMeleeHitResultType ResultType = EMeleeHitResultType::PartnerDamage;
	if (Enemy)
	{
		if (Enemy->IsDead() || Enemy->GetCurrentHealth() <= 0.0f)
		{
			UE_LOG(LogMeleeDiagnostics, Log, TEXT("[MeleeDiag][DamageRejected] Weapon=%s Sequence=%d Reason=EnemyDead Target=%s Health=%.3f"), *GetNameSafe(this), AttackSequence, *GetNameSafe(Target), Enemy->GetCurrentHealth());
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
	else if (Partner)
	{
		const UOutlierVitalAttributeSet* Vital = Partner->GetVitalAttributeSet();
		const UPartnerVitalityComponent* Vitality = Partner->GetPartnerVitalityComponent();
		if (!Vital || Vital->GetHealth() <= 0.0f || !Vitality || Vitality->IsRebooting())
		{
			UE_LOG(LogMeleeDiagnostics, Log, TEXT("[MeleeDiag][DamageRejected] Weapon=%s Sequence=%d Reason=PartnerVitality Target=%s Health=%.3f Rebooting=%d"),
				*GetNameSafe(this), AttackSequence, *GetNameSafe(Target), Vital ? Vital->GetHealth() : -1.0f, Vitality && Vitality->IsRebooting());
			return;
		}
	}

	FOutlierDamageRequest DamageRequest;
	DamageRequest.DamageAmount = DamageToApply;
	DamageRequest.DamageTag = OutlierGameplayTags::Damage::Melee();
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
	DamageRequest.bHasDamageOrigin = true;
	DamageRequest.EventInstigator = IsValid(WeaponOwner) ? WeaponOwner->GetController() : nullptr;
	DamageRequest.DamageCauser = this;
	DamageRequest.StealthSession = SwingStealthSession;
	DamageRequest.HitResult = HitResult;
	const float AppliedDamage = OutlierDamage::Apply(Target, DamageRequest);
	UE_LOG(LogMeleeDiagnostics, Log, TEXT("[MeleeDiag][DamageResult] Weapon=%s Sequence=%d Target=%s Requested=%.3f Applied=%.3f ResultType=%d"),
		*GetNameSafe(this), AttackSequence, *GetNameSafe(Target), DamageToApply, AppliedDamage, static_cast<int32>(ResultType));
	if (AppliedDamage <= 0.0f)
	{
		return;
	}

	// 접촉 후보가 아니라 실제 피해가 적용된 경우에만 초록 표식을 전송한다.
#if !UE_BUILD_SHIPPING
	if (bServerTraceDebugRequested || bDrawDebugMeleeTrace)
	{
		MulticastDrawMeleeDamage(HitResult.ImpactPoint,
			bServerTraceDebugRequested ? ServerTraceDebugDuration : DebugMeleeTraceDuration);
	}
#endif
	// 폭발물의 피격/폭발 연출은 ReceiveOutlierDamage에서 처리한다.
	if (Explosive)
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

void AMeleeWeaponBase::PresentSurfaceImpact(const FHitResult& Hit)
{
	AActor* SurfaceActor = Hit.GetActor();
	if (!HasAuthority() || !IsValid(SurfaceActor) || SurfaceActorsThisSwing.Contains(SurfaceActor))
	{
		return;
	}
	SurfaceActorsThisSwing.Add(SurfaceActor);
	// 표면 연출은 별도로 중복을 거른다. 벽/트리거 접촉은 공격 대상 타격으로 세지 않는다.
	// 타격음은 기존 공격당 한 번 제한을 유지하고, 시각 효과는 실제 대상별로 전달한다.
	const uint8 Surface = ResolveSurfaceImpact(Hit);
	PlayMeleeHitSoundOnce(Hit, Surface);
	MulticastPlaySurfaceImpact(Hit.ImpactPoint, Hit.ImpactNormal, Hit.GetActor(),
		Surface, ImpactAudioContextTag);
}

void AMeleeWeaponBase::MulticastPlaySurfaceImpact_Implementation(FVector_NetQuantize Location,
	FVector_NetQuantizeNormal Normal, AActor* HitActor, uint8 Surface, FGameplayTag Context)
{
	if (GetNetMode() == NM_DedicatedServer) return;
	if (ImpactEffect && ImpactEffect->FXAsset && CachedVisualSubsystem)
	{
		CachedVisualSubsystem->SpawnEffectAtLocation(ImpactEffect, Location, Normal.GetSafeNormal().Rotation());
		return;
	}
	PlaySurfaceImpactVisual(Location, Normal, HitActor, Surface, Context);
}
