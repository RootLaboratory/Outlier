#include "Explosion/ExplosionSubsystem.h"

#include "Damage/OutlierDamageReceiver.h"
#include "Enemy/EnemyBase.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Explosion/ExplosionComponent.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTags/OutlierGameplayTags.h"

bool UExplosionSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// 피해와 연쇄 폭발은 서버 권한으로만 처리하므로 클라이언트에는 Subsystem을 만들지 않는다.
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->GetNetMode() != NM_Client && Super::ShouldCreateSubsystem(Outer);
}

void UExplosionSubsystem::RequestExplosion(
	UExplosionComponent* SourceComponent,
	const FVector& ExplosionLocation,
	AController* EventInstigator,
	const FExplosionProfileRow& Profile)
{
	if (!IsValid(SourceComponent) || !GetWorld() || GetWorld()->GetNetMode() == NM_Client)
	{
		return;
	}

	const TWeakObjectPtr<UExplosionComponent> WeakSource(SourceComponent);
	// 처리 중이거나 이미 대기 중인 폭발은 같은 Queue에 다시 넣지 않는다.
	if (QueuedComponents.Contains(WeakSource))
	{
		return;
	}

	QueuedComponents.Add(WeakSource);
	FPendingExplosion& Request = PendingExplosions.AddDefaulted_GetRef();
	Request.SourceComponent = SourceComponent;
	Request.EventInstigator = EventInstigator;
	Request.Location = ExplosionLocation;
	Request.Profile = Profile;

	if (!bIsProcessingQueue)
	{
		ProcessPendingExplosions();
	}
}

float UExplosionSubsystem::CalculateFalloffRatio(
	float Distance,
	float OuterRadius,
	float FalloffExponent)
{
	const float SafeOuterRadius = FMath::Max(OuterRadius, 0.0f);
	if (SafeOuterRadius <= 0.0f || Distance >= SafeOuterRadius)
	{
		return 0.0f;
	}

	const float NormalizedDistance = FMath::Clamp(Distance / SafeOuterRadius, 0.0f, 1.0f);
	return FMath::Pow(1.0f - NormalizedDistance, FMath::Max(FalloffExponent, 0.01f));
}

float UExplosionSubsystem::CalculateDistanceDamage(
	float Distance,
	float MaxDamage,
	float OuterRadius,
	float FalloffExponent)
{
	return FMath::Max(MaxDamage, 0.0f)
		* CalculateFalloffRatio(Distance, OuterRadius, FalloffExponent);
}

void UExplosionSubsystem::ProcessPendingExplosions()
{
	// 폭발 피해로 새 폭발이 등록되면 배열 뒤에 추가된다. 재귀 호출 없이 같은 서버 흐름에서 순서대로 처리한다.
	bIsProcessingQueue = true;
	for (int32 RequestIndex = 0; RequestIndex < PendingExplosions.Num(); ++RequestIndex)
	{
		// ProcessExplosion 도중 배열이 확장될 수 있으므로 현재 요청은 값으로 복사한다.
		const FPendingExplosion Request = PendingExplosions[RequestIndex];
		ProcessExplosion(Request);
	}

	PendingExplosions.Reset();
	QueuedComponents.Reset();
	bIsProcessingQueue = false;
}

void UExplosionSubsystem::ProcessExplosion(const FPendingExplosion& Request)
{
	UExplosionComponent* SourceComponent = Request.SourceComponent.Get();
	AActor* SourceActor = SourceComponent ? SourceComponent->GetOwner() : nullptr;
	if (!IsValid(SourceComponent) || !IsValid(SourceActor))
	{
		return;
	}

	const float OuterRadius = FMath::Max(Request.Profile.OuterRadiusCm, 0.0f);
	if (OuterRadius > 0.0f)
	{
		FCollisionObjectQueryParams ObjectQueryParams;
		ObjectQueryParams.AddObjectTypesToQuery(ECC_Pawn);
		ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldDynamic);
		ObjectQueryParams.AddObjectTypesToQuery(ECC_PhysicsBody);

		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(OutlierExplosionOverlap), false, SourceActor);
		TArray<FOverlapResult> OverlapResults;
		GetWorld()->OverlapMultiByObjectType(
			OverlapResults,
			Request.Location,
			FQuat::Identity,
			ObjectQueryParams,
			FCollisionShape::MakeSphere(OuterRadius),
			QueryParams);

		// 여러 Hitbox가 범위에 잡혀도 Actor 하나에는 피해를 한 번만 적용한다.
		TSet<TWeakObjectPtr<AActor>> ProcessedActors;
		for (const FOverlapResult& OverlapResult : OverlapResults)
		{
			AActor* TargetActor = OverlapResult.GetActor();
			const TWeakObjectPtr<AActor> WeakTarget(TargetActor);
			if (!IsValid(TargetActor) || TargetActor == SourceActor || ProcessedActors.Contains(WeakTarget))
			{
				continue;
			}

			ProcessedActors.Add(WeakTarget);
			const float Distance = FVector::Distance(Request.Location, TargetActor->GetActorLocation());
			const float DistanceDamage = CalculateDistanceDamage(
				Distance,
				Request.Profile.MaxDamage,
				Request.Profile.OuterRadiusCm,
				Request.Profile.DamageFalloffExponent);
			const float ImpulseRatio = CalculateFalloffRatio(
				Distance,
				Request.Profile.OuterRadiusCm,
				Request.Profile.ImpulseFalloffExponent);

			if (DistanceDamage <= 0.0f && ImpulseRatio <= 0.0f)
			{
				continue;
			}

			// 초기 범위에서는 대상마다 단일 Trace만 사용해 차폐 감쇠를 계산한다.
			const float OcclusionMultiplier = IsTargetOccluded(Request.Location, SourceActor, TargetActor)
				? FMath::Clamp(Request.Profile.OccludedMultiplier, 0.0f, 1.0f)
				: 1.0f;
			const float FinalDamage = DistanceDamage * OcclusionMultiplier;
			const float FinalImpulseRatio = ImpulseRatio * OcclusionMultiplier;

			// 피해와 동일한 감쇠 비율을 반동과 카메라 흔들림에도 사용한다.
			if (AEnemyBase* Enemy = Cast<AEnemyBase>(TargetActor))
			{
				Enemy->ApplyExplosionReaction(
					Request.Location,
					Request.Profile.EnemyImpulseScale,
					Request.Profile.TurretReactionScale,
					FinalImpulseRatio);
			}
			ApplyPlayerCameraShake(
				TargetActor,
				SourceComponent->GetCameraShakeClass(),
				Request.Profile.CameraShakeScale,
				FinalImpulseRatio,
				SourceComponent->IsCameraShakeEnabled(),
				SourceComponent->AllowsCameraShakeForInactivePawn());

			if (FinalDamage > 0.0f)
			{
				FOutlierDamageRequest DamageRequest;
				DamageRequest.DamageAmount = FinalDamage;
				DamageRequest.DamageTag = OutlierGameplayTags::Damage::Explosion();
				DamageRequest.DamageOrigin = Request.Location;
				DamageRequest.EventInstigator = Request.EventInstigator.Get();
				DamageRequest.DamageCauser = SourceActor;
				OutlierDamage::Apply(TargetActor, DamageRequest);
			}
		}
	}

	SourceComponent->NotifyExplosionProcessed(Request.Location, Request.Profile);
}

bool UExplosionSubsystem::IsTargetOccluded(
	const FVector& Origin,
	const AActor* SourceActor,
	const AActor* TargetActor) const
{
	if (!GetWorld() || !TargetActor)
	{
		return false;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(OutlierExplosionOcclusion), false, SourceActor);
	FHitResult Hit;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit,
		Origin,
		TargetActor->GetActorLocation(),
		ECC_Visibility,
		QueryParams);

	return bHit && Hit.GetActor() != TargetActor;
}

void UExplosionSubsystem::ApplyPlayerCameraShake(
	AActor* TargetActor,
	TSubclassOf<UCameraShakeBase> CameraShakeClass,
	float CameraShakeScale,
	float EffectRatio,
	bool bCameraShakeEnabled,
	bool bAllowCameraShakeForInactivePawn) const
{
	const APawn* Pawn = Cast<APawn>(TargetActor);
	AFirstPersonPlayerController* PlayerController = Pawn
		? Cast<AFirstPersonPlayerController>(Pawn->GetController())
		: nullptr;
	if (!bCameraShakeEnabled || !PlayerController || !CameraShakeClass || CameraShakeScale <= 0.0f)
	{
		return;
	}

	float TargetScale = 1.0f;
	if (const AEnemyBase* PossessedEnemy = Cast<AEnemyBase>(Pawn))
	{
		TargetScale = PossessedEnemy->GetImpactCameraShakeScale();
	}

	PlayerController->ClientPlayExplosionCameraShake(
		CameraShakeClass,
		CameraShakeScale * EffectRatio * TargetScale,
		bAllowCameraShakeForInactivePawn);
}
