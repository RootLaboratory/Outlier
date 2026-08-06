#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Explosion/ExplosionTypes.h"
#include "ExplosionSubsystem.generated.h"

class UExplosionComponent;
class UCameraShakeBase;

UCLASS()
class OUTLIER_API UExplosionSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// 폭발 계산은 서버에서만 필요하므로 Client World에는 Subsystem을 생성하지 않는다.
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	// 폭발 요청을 Queue에 추가하고, 다른 폭발을 처리 중이 아니면 즉시 Queue 처리를 시작한다.
	void RequestExplosion(
		UExplosionComponent* SourceComponent,
		const FVector& ExplosionLocation,
		AController* EventInstigator,
		const FExplosionProfileRow& Profile);

	// 중심과 대상 사이 거리를 기준으로 최대 피해에서 최소 피해까지 선형 감쇠한다.
	static float CalculateFalloffRatio(float Distance, float OuterRadius, float FalloffExponent);

	static float CalculateDistanceDamage(
		float Distance,
		float MaxDamage,
		float OuterRadius,
		float FalloffExponent);

private:
	struct FPendingExplosion
	{
		TWeakObjectPtr<UExplosionComponent> SourceComponent;
		TWeakObjectPtr<AController> EventInstigator;
		FVector Location = FVector::ZeroVector;
		FExplosionProfileRow Profile;
	};

	// 대기 중인 폭발을 순서대로 처리하며, 처리 도중 추가된 연쇄 폭발도 같은 흐름에서 이어서 처리한다.
	void ProcessPendingExplosions();

	// 단일 폭발의 범위 조회, 차폐 판정, 반동, 카메라 흔들림과 피해 적용을 수행한다.
	void ProcessExplosion(const FPendingExplosion& Request);

	// Occluded: 폭발 중심과 대상 사이가 벽이나 장애물로 가려졌는지 확인한다.
	bool IsTargetOccluded(const FVector& Origin, const AActor* SourceActor, const AActor* TargetActor) const;

	// 로컬 플레이어를 조종하는 Controller에 감쇠된 카메라 흔들림을 요청한다.
	void ApplyPlayerCameraShake(
		AActor* TargetActor,
		TSubclassOf<UCameraShakeBase> CameraShakeClass,
		float CameraShakeScale,
		float EffectRatio,
		bool bCameraShakeEnabled,
		bool bAllowCameraShakeForInactivePawn) const;

	TArray<FPendingExplosion> PendingExplosions;
	TSet<TWeakObjectPtr<UExplosionComponent>> QueuedComponents;
	bool bIsProcessingQueue = false;
};
