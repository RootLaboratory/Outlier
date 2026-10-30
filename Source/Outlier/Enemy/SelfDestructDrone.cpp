#include "Enemy/SelfDestructDrone.h"
#include "Audio/OutlierAudioFeedback.h"

#include "Components/PrimitiveComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/VECDroneMovementComponent.h"
#include "Drone/Partner/HackableComponent.h"
#include "Explosion/ExplosionComponent.h"
#include "Explosion/ExplosiveProp.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Outlier.h"

DEFINE_LOG_CATEGORY_STATIC(LogEnemyChargeCollision, Log, All);

namespace
{
void CollectMatchingExplosiveSockets(
	const USkeletalMeshComponent* Mesh,
	FName SocketPattern,
	TArray<FName>& OutSocketNames)
{
	if (!Mesh || SocketPattern.IsNone())
	{
		return;
	}

	const FString Pattern = SocketPattern.ToString();
	for (const FName SocketName : Mesh->GetAllSocketNames())
	{
		const FString Name = SocketName.ToString();
		if (Name.Equals(Pattern)
			|| Name.StartsWith(Pattern)
			|| Name.EndsWith(Pattern))
		{
			OutSocketNames.AddUnique(SocketName);
		}
	}

	OutSocketNames.Sort(
		[](const FName& Left, const FName& Right)
		{
			return Left.LexicalLess(Right);
		});
}
}

ASelfDestructDrone::ASelfDestructDrone()
{
	SetDefaultEnemyType(EEnemyType::Melee);
	HitAudioContext = OutlierAudioFeedback::Tag(TEXT("Audio.Context.Enemy.Drone.Melee.State.Hit"));
	DeathAudioContext = OutlierAudioFeedback::Tag(TEXT("Audio.Context.Enemy.Drone.Melee.State.Death"));
	DetectAudioContext = OutlierAudioFeedback::Tag(TEXT("Audio.Context.Enemy.Drone.Melee.State.Detect"));
	HoverAudioContext = OutlierAudioFeedback::Tag(TEXT("Audio.Context.Enemy.Drone.Melee.Hovering.Loop"));
	AppearAudioContext = OutlierAudioFeedback::Tag(TEXT("Audio.Context.Enemy.Drone.Melee.State.Appear"));
	TelegraphAudioContext = OutlierAudioFeedback::Tag(TEXT("Audio.Context.Enemy.Drone.Melee.State.Exlode.sign"));
	SelfDestructAudioContext = OutlierAudioFeedback::Tag(TEXT("Audio.Context.Enemy.Drone.Melee.State.Exlode.Act"));


	ExplosionComponent = CreateDefaultSubobject<UExplosionComponent>(TEXT("ExplosionComponent"));
}

void ASelfDestructDrone::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASelfDestructDrone, MountedExplosives);
	DOREPLIFETIME(ASelfDestructDrone, bHasCommittedSelfDestruct);
}

float ASelfDestructDrone::GetWeakPointDamageMultiplier(const UPrimitiveComponent* HitComponent) const
{
	const AExplosiveProp* HitExplosive = HitComponent
		? Cast<AExplosiveProp>(HitComponent->GetOwner())
		: nullptr;
	if (HitExplosive && HitExplosive->GetOwner() == this)
	{
		// 별도 Actor의 HitCollision 피격도 자폭 드론 본체 HP에 크리티컬 피해로 적용한다.
		return FMath::Max(GetRuntimeStat().ExplosiveWeakPointMultiplier, 1.0f);
	}

	return Super::GetWeakPointDamageMultiplier(HitComponent);
}

void ASelfDestructDrone::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		if (!IsPoolManaged())
		{
			SpawnAndAttachMountedExplosives();
		}
	}
}

void ASelfDestructDrone::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DestroyMountedExplosives();
	Super::EndPlay(EndPlayReason);
}

void ASelfDestructDrone::SpawnAndAttachMountedExplosives()
{
	if (!HasAuthority() || !MountedExplosiveClass || !MountedExplosives.IsEmpty() || !GetMesh())
	{
		return;
	}

	TArray<FName> MatchingSockets;
	CollectMatchingExplosiveSockets(
		GetMesh(),
		MountedExplosiveSocketPattern,
		MatchingSockets);
	if (MatchingSockets.IsEmpty())
	{
		UE_LOG(
			LogOutlier,
			Error,
			TEXT("[SelfDestructDrone] No mounted explosive sockets matched. Actor=%s Pattern=%s"),
			*GetNameSafe(this),
			*MountedExplosiveSocketPattern.ToString());
		return;
	}

	for (const FName SocketName : MatchingSockets)
	{
		AExplosiveProp* MountedExplosive = GetWorld()->SpawnActorDeferred<AExplosiveProp>(
			MountedExplosiveClass,
			GetMesh()->GetSocketTransform(SocketName),
			this,
			this,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!MountedExplosive)
		{
			continue;
		}

		MountedExplosive->InitializeMountedSocket(SocketName);
		MountedExplosive->FinishSpawning(GetMesh()->GetSocketTransform(SocketName));
		MountedExplosives.Add(MountedExplosive);
	}
	ForceNetUpdate();
}

void ASelfDestructDrone::DestroyMountedExplosives()
{
	if (HasAuthority())
	{
		for (AExplosiveProp* MountedExplosive : MountedExplosives)
		{
			if (IsValid(MountedExplosive))
			{
				MountedExplosive->Destroy();
			}
		}
	}
	MountedExplosives.Reset();
}

void ASelfDestructDrone::TriggerSelfDestruct()
{
	if (!HasAuthority() || bDeathHandling)
	{
		return;
	}
	UE_LOG(LogEnemyChargeCollision, Log, TEXT("[Charge] DetonateRequested Drone=%s Committed=%d Health=%.1f"),
		*GetNameSafe(this), bHasCommittedSelfDestruct, GetCurrentHealth());

	bTriggeredSelfDestruct = true;
	ApplyDamageInternal(
		FMath::Max(GetCurrentHealth(), 1.0f),
		GetController(),
		this,
		OutlierGameplayTags::Damage::Explosion());
	if (!IsDead()) bTriggeredSelfDestruct = false;
}

void ASelfDestructDrone::NotifyHit(UPrimitiveComponent* MyComp, AActor* Other,
	UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation, FVector HitNormal,
	FVector NormalImpulse, const FHitResult& Hit)
{
	Super::NotifyHit(MyComp, Other, OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);
	// 메시/부착물 접촉은 제외한다. 커밋된 본체의 실제 차단 충돌만 폭발한다.
	if (!HasAuthority() || !bHasCommittedSelfDestruct || bDeathHandling || IsDead()
		|| MyComp != GetCapsuleComponent() || !Hit.bBlockingHit
		|| !IsValid(Other) || Other == this || Other->GetOwner() == this || Other->IsAttachedTo(this))
	{
		return;
	}
	UE_LOG(LogEnemyChargeCollision, Log, TEXT("[Charge] CollisionDetonate Drone=%s Other=%s Component=%s Location=%s"),
		*GetNameSafe(this), *GetNameSafe(Other), *GetNameSafe(OtherComp), *HitLocation.ToCompactString());
	TriggerSelfDestruct();
}

bool ASelfDestructDrone::BeginCommittedSelfDestruct(
	AActor* TargetActor,
	float TelegraphDuration,
	float ChargeSpeed)
{
	if (!IsValid(TargetActor))
	{
		return false;
	}

	const FVector ToTarget = TargetActor->GetActorLocation() - GetActorLocation();
	// AI는 목표 앞에서 멈추지 않는다. 충돌 또는 StateTree의 전조 만료까지 계속 돌진한다.
	return BeginCommittedSelfDestructInternal(
		ToTarget,
		TelegraphDuration,
		ChargeSpeed,
		false,
		0.0f);
}

bool ASelfDestructDrone::BeginCommittedSelfDestructDirection(
	const FVector& ChargeDirection,
	float TelegraphDuration,
	float ChargeSpeed,
	float MaxChargeDistance)
{
	return BeginCommittedSelfDestructInternal(
		ChargeDirection,
		TelegraphDuration,
		ChargeSpeed,
		true,
		FMath::Max(MaxChargeDistance, 0.0f));
}

bool ASelfDestructDrone::BeginCommittedSelfDestructInternal(
	const FVector& ChargeDirection,
	float TelegraphDuration,
	float ChargeSpeed,
	bool bLimitChargeDistance,
	float ChargeDistanceLimit)
{
	if (!HasAuthority()
		|| bDeathHandling
		|| IsDead()
		|| bHasCommittedSelfDestruct)
	{
		return false;
	}

	CommittedChargeDirection = ChargeDirection.GetSafeNormal();
	if (CommittedChargeDirection.IsNearlyZero())
	{
		CommittedChargeDirection = GetActorForwardVector().GetSafeNormal();
	}
	CommittedChargeTargetDirection = CommittedChargeDirection;
	CommittedChargeStartLocation = GetActorLocation();
	ChargeSpeedState.Reset();
	CommittedChargeSpeed = FMath::Max(ChargeSpeed, 0.0f);
	bCommittedChargeDistanceLimited = bLimitChargeDistance;
	CommittedChargeDistanceLimit = FMath::Max(ChargeDistanceLimit, 0.0f);
	CommittedImpactElapsedTime = 0.0f;

	bHasCommittedSelfDestruct = true;
	if (HackableComponent)
	{
		// 전조가 시작된 자폭은 더 이상 취소 가능한 해킹 대상으로 노출하지 않는다.
		HackableComponent->HackTags.AddTag(OutlierGameplayTags::State::Locked());
	}
	if (VECMovementComponent)
	{
		VECMovementComponent->ClearFlightInput();
	}
	StopCurrentAttack();
	ReleaseSearchDestinations();
	if (AEnemyAIController* EnemyAIController = Cast<AEnemyAIController>(GetController()))
	{
		EnemyAIController->StopMovement();
	}

	FRotator ChargeRotation = CommittedChargeDirection.Rotation();
	ChargeRotation.Roll = 0.0f;
	if (AController* ActiveController = GetController())
	{
		ActiveController->SetControlRotation(ChargeRotation);
	}
	SetActorRotation(FRotator(0.0f, ChargeRotation.Yaw, 0.0f));
	MulticastSelfDestructTelegraphStarted(FMath::Max(TelegraphDuration, 0.0f));
	UE_LOG(LogEnemyChargeCollision, Log, TEXT("[Charge] Commit Drone=%s Direction=%s Speed=%.1f DistanceLimited=%d Distance=%.1f Duration=%.2f"),
		*GetNameSafe(this), *CommittedChargeDirection.ToCompactString(), ChargeSpeed,
		bCommittedChargeDistanceLimited, ChargeDistanceLimit, TelegraphDuration);
	ForceNetUpdate();
	return true;
}

bool ASelfDestructDrone::UpdateCommittedSelfDestructMovement(
	float DeltaTime,
	float ChargeSpeed)
{
	if (!HasAuthority()
		|| !bHasCommittedSelfDestruct
		|| bDeathHandling
		|| IsDead())
	{
		return false;
	}

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement)
	{
		return false;
	}
	if (Movement->MovementMode != MOVE_Flying)
	{
		Movement->SetMovementMode(MOVE_Flying);
	}

	CommittedChargeSpeed = FMath::Max(ChargeSpeed, 0.0f);
	CommittedChargeDirection = FMath::RInterpTo(
		CommittedChargeDirection.Rotation(),
		CommittedChargeTargetDirection.Rotation(),
		FMath::Max(DeltaTime, 0.0f),
		FMath::Max(RuntimeImpactReactionProfile.ChargeImpactTurnInterpSpeed, 0.0f)).Vector();

	CommittedImpactElapsedTime += FMath::Max(DeltaTime, 0.0f);
	if (CommittedImpactElapsedTime >= CurrentPhysicalKnockbackDuration)
	{
		AccumulatedImpactVelocity = FVector::ZeroVector;
	}
	else
	{
		AccumulatedImpactVelocity *= FMath::Exp(
			-FMath::Max(RuntimeImpactReactionProfile.KnockbackDamping, 0.0f)
			* FMath::Max(DeltaTime, 0.0f));
	}
	CurrentImpactStrength = AccumulatedImpactVelocity.Size();

	// AI는 가속만 적용하며 거리로 감속하지 않는다. 빙의 돌진의 거리 제한은 아래에서 별도로 적용한다.
	const float ProfiledChargeSpeed = IsEnemyPossessed() ? FMath::Max(ChargeSpeed, 0.0f)
		: ChargeSpeedState.Update(AIChargeSpeedProfile, ChargeSpeed, Movement->Velocity.Size(),
			0.0f, DeltaTime, false, this, TEXT("Charge"));
	float ForwardChargeSpeed = ProfiledChargeSpeed;
	if (bCommittedChargeDistanceLimited)
	{
		const float TraveledChargeDistance = FMath::Max(
			FVector::DotProduct(GetActorLocation() - CommittedChargeStartLocation,
				CommittedChargeTargetDirection), 0.0f);
		const float RemainingChargeDistance = FMath::Max(
			CommittedChargeDistanceLimit - TraveledChargeDistance, 0.0f);
		ForwardChargeSpeed = FMath::Min(ProfiledChargeSpeed,
			RemainingChargeDistance / FMath::Max(DeltaTime, SMALL_NUMBER));
	}
	const FVector ChargeVelocity = CommittedChargeDirection * ForwardChargeSpeed;
	const FVector FinalVelocity = ChargeVelocity + AccumulatedImpactVelocity;
	Movement->MaxFlySpeed = FMath::Max(
		GetRuntimeStat().MoveSpeed,
		FinalVelocity.Size());
	Movement->Velocity = FinalVelocity;

	FRotator ChargeRotation = CommittedChargeDirection.Rotation();
	ChargeRotation.Roll = 0.0f;
	if (AController* ActiveController = GetController())
	{
		ActiveController->SetControlRotation(ChargeRotation);
	}
	SetActorRotation(FRotator(0.0f, ChargeRotation.Yaw, 0.0f));
	return true;
}

void ASelfDestructDrone::CancelCommittedSelfDestruct()
{
	if (!HasAuthority() || !bHasCommittedSelfDestruct || bDeathHandling)
	{
		return;
	}
	UE_LOG(LogEnemyChargeCollision, Log, TEXT("[Charge] Cancel Drone=%s"), *GetNameSafe(this));

	bHasCommittedSelfDestruct = false;
	ChargeSpeedState.Reset();
	if (HackableComponent)
	{
		// 기절 등으로 자폭 커밋이 취소되면 살아 있는 드론은 다시 해킹 후보가 될 수 있다.
		HackableComponent->HackTags.RemoveTag(OutlierGameplayTags::State::Locked());
	}
	CommittedChargeDirection = FVector::ForwardVector;
	CommittedChargeTargetDirection = FVector::ForwardVector;
	CommittedChargeStartLocation = FVector::ZeroVector;
	CommittedChargeSpeed = 0.0f;
	CommittedChargeDistanceLimit = 0.0f;
	bCommittedChargeDistanceLimited = false;
	CommittedImpactElapsedTime = 0.0f;
	AccumulatedImpactVelocity = FVector::ZeroVector;
	CurrentImpactStrength = 0.0f;
	CurrentPhysicalKnockbackDuration = 0.0f;
	CurrentControlRecoveryDuration = 0.0f;
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->MaxFlySpeed = FMath::Max(GetRuntimeStat().MoveSpeed, 0.0f);
	}
	MulticastSelfDestructTelegraphCancelled();
	ForceNetUpdate();
}

bool ASelfDestructDrone::TryApplyCommittedImpactVelocity(const FVector& ImpactVelocity)
{
	if (!bHasCommittedSelfDestruct)
	{
		return false;
	}

	AccumulateImpactVelocity(ImpactVelocity);
	CommittedImpactElapsedTime = 0.0f;

	const FVector CurrentDirection = CommittedChargeDirection.GetSafeNormal();
	FVector DesiredDirection = (
		CurrentDirection * FMath::Max(CommittedChargeSpeed, 1.0f)
		+ AccumulatedImpactVelocity).GetSafeNormal();
	if (DesiredDirection.IsNearlyZero())
	{
		DesiredDirection = CurrentDirection;
	}

	const float MaxTurnRadians = FMath::DegreesToRadians(FMath::Clamp(
		RuntimeImpactReactionProfile.MaxChargeImpactTurnAngleDegrees,
		0.0f,
		180.0f));
	const float TurnRadians = FMath::Acos(FMath::Clamp(
		FVector::DotProduct(CurrentDirection, DesiredDirection),
		-1.0f,
		1.0f));
	if (TurnRadians > MaxTurnRadians && MaxTurnRadians > 0.0f)
	{
		FVector TurnAxis = FVector::CrossProduct(CurrentDirection, DesiredDirection).GetSafeNormal();
		if (TurnAxis.IsNearlyZero())
		{
			TurnAxis = FVector::UpVector;
		}
		DesiredDirection = FQuat(TurnAxis, MaxTurnRadians).RotateVector(CurrentDirection);
	}

	FRotator LimitedRotation = DesiredDirection.Rotation();
	LimitedRotation.Pitch = FMath::Clamp(
		FRotator::NormalizeAxis(LimitedRotation.Pitch),
		-RuntimeImpactReactionProfile.MaxChargePitchDegrees,
		RuntimeImpactReactionProfile.MaxChargePitchDegrees);
	LimitedRotation.Roll = 0.0f;
	CommittedChargeTargetDirection = LimitedRotation.Vector();
	return true;
}

void ASelfDestructDrone::CancelCommittedAction()
{
	CancelCommittedSelfDestruct();
}

void ASelfDestructDrone::MulticastSelfDestructTelegraphStarted_Implementation(
	float TelegraphDuration)
{
	if (GetNetMode() != NM_DedicatedServer)
	{
		OutlierAudioFeedback::Loop(this, *TelegraphAudioContext.ToString(), true, TEXT("Audio.Type.Enemy"));
		OnSelfDestructTelegraphStarted(TelegraphDuration);
	}
}

void ASelfDestructDrone::MulticastSelfDestructTelegraphCancelled_Implementation()
{
	if (GetNetMode() != NM_DedicatedServer)
	{
		OutlierAudioFeedback::Loop(this, *TelegraphAudioContext.ToString(), false, TEXT("Audio.Type.Enemy"));
		OnSelfDestructTelegraphCancelled();
	}
}

void ASelfDestructDrone::HandleDeath()
{
	if (!HasAuthority() || bDeathHandling)
	{
		return;
	}

	bDeathHandling = true;
	bHasCommittedSelfDestruct = false;
	bCommittedChargeDistanceLimited = false;
	// EnemyBase가 Actor를 제거하기 전에 폭발 Queue와 클라이언트 연출을 먼저 확정한다.
	if (ExplosionComponent)
	{
		ExplosionComponent->SetAudioContextOverride(bTriggeredSelfDestruct ? SelfDestructAudioContext : FGameplayTag());
		ExplosionComponent->DetonateAt(
			GetActorLocation(),
			GetController(),
			IsEnemyPossessed()
				? EOutlierAdaptationDamageCategory::NonGun
				: EOutlierAdaptationDamageCategory::Ignore);
	}
	DestroyMountedExplosives();

	Super::HandleDeath();
}

void ASelfDestructDrone::ResetPoolRuntimeState()
{
	ChargeSpeedState.Reset();
	// 본체는 재사용하지만 부착 폭발물은 Idle 진입 때 제거한다. 새 대여마다 폭발 가능한 부품을 다시 만든다.
	Super::ResetPoolRuntimeState();
	bDeathHandling = false;
	bTriggeredSelfDestruct = false;
	bHasCommittedSelfDestruct = false;
	CommittedChargeDirection = FVector::ForwardVector;
	CommittedChargeTargetDirection = FVector::ForwardVector;
	CommittedChargeStartLocation = FVector::ZeroVector;
	CommittedChargeSpeed = 0.0f;
	CommittedChargeDistanceLimit = 0.0f;
	bCommittedChargeDistanceLimited = false;
	CommittedImpactElapsedTime = 0.0f;
	SpawnAndAttachMountedExplosives();
}

void ASelfDestructDrone::PrepareForPoolIdle()
{
	ChargeSpeedState.Reset();
	Super::PrepareForPoolIdle();
	DestroyMountedExplosives();
}
