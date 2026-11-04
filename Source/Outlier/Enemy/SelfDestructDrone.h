#pragma once

#include "CoreMinimal.h"
#include "Enemy/VECDrone.h"
#include "Drone/FlightMovementComponentBase.h"
#include "Audio/OutlierAudioTypes.h"
#include "SelfDestructDrone.generated.h"

class UExplosionComponent;
class AExplosiveProp;
class AGameStateBase;
class UOutlierAudioSubsystem;

USTRUCT()
struct FSelfDestructChargeState
{
	GENERATED_BODY()
	UPROPERTY()
	uint32 Revision = 0;
	UPROPERTY()
	bool bActive = false;
	UPROPERTY()
	FOutlierAudioPlaybackWindow Playback;
};

UCLASS()
class OUTLIER_API ASelfDestructDrone : public AVECDrone
{
	GENERATED_BODY()

public:
	ASelfDestructDrone();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual float GetWeakPointDamageMultiplier(const UPrimitiveComponent* HitComponent) const override;
	virtual void NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp,
		bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit) override;

	// StateTree 전조가 끝났을 때 호출한다. GAS로 치명 피해를 적용해 폭발 및 사망 처리에 진입한다.
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
		float ChargeSpeed);
	// 직접 빙의 돌진은 시선 방향과 최대 이동 거리를 고정한다.
	bool BeginCommittedSelfDestructDirection(
		const FVector& ChargeDirection,
		float TelegraphDuration,
		float ChargeSpeed,
		float MaxChargeDistance);
	bool UpdateCommittedSelfDestructMovement(float DeltaTime, float ChargeSpeed);
	void CancelCommittedSelfDestruct();
	virtual bool IsPossessedActionCommitted() const override { return ChargeState.bActive; }

	UFUNCTION(BlueprintPure, Category = "Self Destruct Drone")
	bool IsSelfDestructCommitted() const { return ChargeState.bActive; }

	// 빙의 Update Task의 실제 종료 시간을 commit과 같은 시간축에 확정한다.
	void SetCommittedChargeDuration(float Duration);
	float GetCommittedChargeElapsedTime() const;
	void RefreshChargePresentation();

protected:
	// AI 돌진은 가속만 적용한다. 목적지 감속이나 타겟 재추적은 하지 않는다.
	UPROPERTY(EditDefaultsOnly, Category = "Self Destruct Drone|Movement")
	FFlightSpeedProfile AIChargeSpeedProfile;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void HandleDeath() override;
	virtual FGameplayTag GetDeathAudioContext() const override { return bTriggeredSelfDestruct ? FGameplayTag() : DeathAudioContext; }
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Audio", meta = (Categories = "Audio.Context"))
	FGameplayTag TelegraphAudioContext;
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Audio", meta = (Categories = "Audio.Context"))
	FGameplayTag SelfDestructAudioContext;
	virtual void ResetPoolRuntimeState() override;
	virtual void PrepareForPoolIdle() override;
	virtual void OnPoolStateApplied(EEnemyPoolState PreviousState) override;
	virtual bool TryApplyCommittedImpactVelocity(const FVector& ImpactVelocity) override;
	virtual void CancelCommittedAction() override;

	// 자폭 드론 BP에서 별도로 만든 부착 폭발물 BP 클래스를 지정한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Self Destruct Drone|Explosive")
	TSubclassOf<AExplosiveProp> MountedExplosiveClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Self Destruct Drone|Explosive")
	FName MountedExplosiveSocketPattern = TEXT("ExplosiveSocket");
	// 지정하지 않으면 기존 패턴에서 유일한 소켓만 허용한다. 복수 후보를 임의 선택하지 않는다.
	UPROPERTY(EditDefaultsOnly, Category = "Self Destruct Drone|Explosive")
	FName MountedExplosiveSocketName;

	UPROPERTY(EditDefaultsOnly, Category = "Self Destruct Drone|Presentation", meta = (ClampMin = "0.0", Units = "s"))
	float ChargeSoundDuration = 4.029208f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_MountedExplosives, Category = "Self Destruct Drone|Explosive")
	TArray<TObjectPtr<AExplosiveProp>> MountedExplosives;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Self Destruct Drone|Explosive")
	TObjectPtr<UExplosionComponent> ExplosionComponent;

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
		bool bLimitChargeDistance,
		float ChargeDistanceLimit);

	bool bDeathHandling = false;
	bool bTriggeredSelfDestruct = false;
	FFlightSpeedProfileState ChargeSpeedState;
	friend class FEnemyChargeSpeedProfileTest;
	friend class FEnemyChargeCollisionTest;
	friend class FEnemyChargePresentationStateTest;

	UPROPERTY(ReplicatedUsing = OnRep_ChargeState)
	FSelfDestructChargeState ChargeState;
	UFUNCTION()
	void OnRep_ChargeState();
	UFUNCTION()
	void OnRep_MountedExplosives();
	void PublishChargeState(bool bActive, float Duration = 0.0f);
	void StopChargePresentation();
	FTimerHandle ChargePresentationTimer;
	TWeakObjectPtr<AGameStateBase> ChargeClock;
	TWeakObjectPtr<UOutlierAudioSubsystem> ChargeAudio;
	TWeakObjectPtr<AExplosiveProp> PresentationExplosive;
	uint32 PresentedChargeRevision = 0;
	bool bChargePresentationStarted = false;
	FVector CommittedChargeDirection = FVector::ForwardVector;
	FVector CommittedChargeTargetDirection = FVector::ForwardVector;
	FVector CommittedChargeStartLocation = FVector::ZeroVector;
	float CommittedChargeSpeed = 0.0f;
	bool bCommittedChargeDistanceLimited = false;
	float CommittedChargeDistanceLimit = 0.0f;
	float CommittedImpactElapsedTime = 0.0f;
};
