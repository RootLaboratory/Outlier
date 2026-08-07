#include "Enemy/SelfDestructDrone.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/VECDroneMovementComponent.h"
#include "Explosion/ExplosionComponent.h"
#include "Explosion/ExplosiveProp.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Outlier.h"

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
		SpawnAndAttachMountedExplosives();
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

	CurrentHealth = 0.0f;
	HandleDeath();
}

bool ASelfDestructDrone::BeginCommittedSelfDestruct(
	AActor* TargetActor,
	float TelegraphDuration,
	float ChargeSpeed,
	float MaxChargeDistance,
	float TargetStopDistance)
{
	if (!IsValid(TargetActor))
	{
		return false;
	}

	const FVector ToTarget = TargetActor->GetActorLocation() - GetActorLocation();
	const float ChargeDistanceLimit = FMath::Min(
		FMath::Max(MaxChargeDistance, 0.0f),
		FMath::Max(ToTarget.Size() - FMath::Max(TargetStopDistance, 0.0f), 0.0f));
	return BeginCommittedSelfDestructInternal(
		ToTarget,
		TelegraphDuration,
		ChargeSpeed,
		ChargeDistanceLimit);
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
		FMath::Max(MaxChargeDistance, 0.0f));
}

bool ASelfDestructDrone::BeginCommittedSelfDestructInternal(
	const FVector& ChargeDirection,
	float TelegraphDuration,
	float ChargeSpeed,
	float ChargeDistanceLimit)
{
	if (!HasAuthority()
		|| bDeathHandling
		|| CurrentHealth <= 0.0f
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
	CommittedChargeSpeed = FMath::Max(ChargeSpeed, 0.0f);
	CommittedChargeDistanceLimit = FMath::Max(ChargeDistanceLimit, 0.0f);
	CommittedImpactElapsedTime = 0.0f;

	bHasCommittedSelfDestruct = true;
	if (VECMovementComponent)
	{
		VECMovementComponent->ClearFlightInput();
	}
	StopCurrentAttack();
	ReleaseSearchRingSlot();
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
		|| CurrentHealth <= 0.0f)
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

	const float TraveledChargeDistance = FMath::Max(
		FVector::DotProduct(
			GetActorLocation() - CommittedChargeStartLocation,
			CommittedChargeTargetDirection),
		0.0f);
	const float RemainingChargeDistance = FMath::Max(
		CommittedChargeDistanceLimit - TraveledChargeDistance,
		0.0f);
	const float SafeDeltaTime = FMath::Max(DeltaTime, SMALL_NUMBER);
	const float LimitedChargeSpeed = FMath::Min(
		FMath::Max(ChargeSpeed, 0.0f),
		RemainingChargeDistance / SafeDeltaTime);
	const FVector ChargeVelocity = CommittedChargeDirection * LimitedChargeSpeed;
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

	bHasCommittedSelfDestruct = false;
	CommittedChargeDirection = FVector::ForwardVector;
	CommittedChargeTargetDirection = FVector::ForwardVector;
	CommittedChargeStartLocation = FVector::ZeroVector;
	CommittedChargeSpeed = 0.0f;
	CommittedChargeDistanceLimit = 0.0f;
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
		OnSelfDestructTelegraphStarted(TelegraphDuration);
	}
}

void ASelfDestructDrone::MulticastSelfDestructTelegraphCancelled_Implementation()
{
	if (GetNetMode() != NM_DedicatedServer)
	{
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
	// EnemyBase가 Actor를 제거하기 전에 폭발 Queue와 클라이언트 연출을 먼저 확정한다.
	if (ExplosionComponent)
	{
		ExplosionComponent->DetonateAt(GetActorLocation(), GetController());
	}
	DestroyMountedExplosives();

	Super::HandleDeath();
}
