#pragma once

#include "CoreMinimal.h"
#include "Enemy/VECDrone.h"
#include "SelfDestructDrone.generated.h"

class UExplosionComponent;
class AExplosiveProp;

UCLASS()
class OUTLIER_API ASelfDestructDrone : public AVECDrone
{
	GENERATED_BODY()

public:
	ASelfDestructDrone();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual float GetWeakPointDamageMultiplier(const UPrimitiveComponent* HitComponent) const override;

	// StateTree 전조가 끝났을 때 호출한다. 현재 HP를 0으로 만들고 즉시 폭발 및 사망 처리한다.
	UFUNCTION(BlueprintCallable, Category = "Self Destruct Drone")
	void TriggerSelfDestruct();

	UFUNCTION(BlueprintPure, Category = "Self Destruct Drone")
	UExplosionComponent* GetExplosionComponent() const { return ExplosionComponent; }

	UFUNCTION(BlueprintPure, Category = "Self Destruct Drone")
	AExplosiveProp* GetMountedExplosive() const
	{
		return MountedExplosives.IsEmpty() ? nullptr : MountedExplosives[0].Get();
	}

	// 전조 진입 순간의 방향을 저장한다. 이후 타겟 위치나 시야 변화는 돌진 방향을 바꾸지 않는다.
	bool BeginCommittedSelfDestruct(
		AActor* TargetActor,
		float TelegraphDuration,
		float ChargeSpeed,
		float MaxChargeDistance,
		float TargetStopDistance);
	bool BeginCommittedSelfDestructDirection(
		const FVector& ChargeDirection,
		float TelegraphDuration,
		float ChargeSpeed,
		float MaxChargeDistance);
	bool UpdateCommittedSelfDestructMovement(float DeltaTime, float ChargeSpeed);
	void CancelCommittedSelfDestruct();
	virtual bool IsPossessedActionCommitted() const override { return bHasCommittedSelfDestruct; }

	UFUNCTION(BlueprintPure, Category = "Self Destruct Drone")
	bool IsSelfDestructCommitted() const { return bHasCommittedSelfDestruct; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void HandleDeath() override;
	virtual bool TryApplyCommittedImpactVelocity(const FVector& ImpactVelocity) override;
	virtual void CancelCommittedAction() override;

	// 자폭 드론 BP에서 별도로 만든 부착 폭발물 BP 클래스를 지정한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Self Destruct Drone|Explosive")
	TSubclassOf<AExplosiveProp> MountedExplosiveClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Self Destruct Drone|Explosive")
	FName MountedExplosiveSocketPattern = TEXT("ExplosiveSocket");

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category = "Self Destruct Drone|Explosive")
	TArray<TObjectPtr<AExplosiveProp>> MountedExplosives;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Self Destruct Drone|Explosive")
	TObjectPtr<UExplosionComponent> ExplosionComponent;

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastSelfDestructTelegraphStarted(float TelegraphDuration);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastSelfDestructTelegraphCancelled();

	UFUNCTION(BlueprintImplementableEvent, Category = "Self Destruct Drone|Presentation")
	void OnSelfDestructTelegraphStarted(float TelegraphDuration);

	UFUNCTION(BlueprintImplementableEvent, Category = "Self Destruct Drone|Presentation")
	void OnSelfDestructTelegraphCancelled();

private:
	void SpawnAndAttachMountedExplosives();
	void DestroyMountedExplosives();
	bool BeginCommittedSelfDestructInternal(
		const FVector& ChargeDirection,
		float TelegraphDuration,
		float ChargeSpeed,
		float ChargeDistanceLimit);

	bool bDeathHandling = false;

	UPROPERTY(Replicated)
	bool bHasCommittedSelfDestruct = false;
	FVector CommittedChargeDirection = FVector::ForwardVector;
	FVector CommittedChargeTargetDirection = FVector::ForwardVector;
	FVector CommittedChargeStartLocation = FVector::ZeroVector;
	float CommittedChargeSpeed = 0.0f;
	float CommittedChargeDistanceLimit = 0.0f;
	float CommittedImpactElapsedTime = 0.0f;
};
