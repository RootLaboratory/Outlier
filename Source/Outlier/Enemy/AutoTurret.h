#pragma once

#include "CoreMinimal.h"
#include "Enemy/EnemyBase.h"
#include "Interface/WeaponMuzzleProvider.h"
#include "AutoTurret.generated.h"

class USceneComponent;
class UBoxComponent;
class UAnimMontage;
class USkeletalMeshComponent;
struct FEnemyRecoverTurretImpactOffsetTask;

UENUM(BlueprintType)
enum class EAutoTurretImpactReactionMode : uint8
{
	ConcurrentOffsetRecovery UMETA(DisplayName = "Concurrent Offset Recovery"),
	StateTreeInterrupt UMETA(DisplayName = "StateTree Impact Reaction")
};

USTRUCT(BlueprintType)
struct OUTLIER_API FAutoTurretBehaviorRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deploy", meta = (ClampMin = "0.0"))
	float DeployFallbackDuration = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim", meta = (ClampMin = "0.0"))
	float DefaultRotationSpeedDegrees = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim", meta = (ClampMin = "0.0"))
	float AttackRotationSpeedDegrees = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float MaxYawDegrees = 160.0f;

	// 공격 중에는 정후방 표적까지 조준할 수 있도록 별도의 Yaw 범위를 사용한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float AttackMaxYawDegrees = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float MaxPitchDegrees = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim", meta = (ClampMin = "0.0"))
	float AimToleranceDegrees = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack", meta = (ClampMin = "0.0"))
	float TelegraphDuration = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack", meta = (ClampMin = "0.0"))
	float BeamPresentationDuration = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float SearchYawDegrees = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search", meta = (ClampMin = "-89.0", ClampMax = "89.0"))
	float SearchMinPitchDegrees = -10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search", meta = (ClampMin = "-89.0", ClampMax = "89.0"))
	float SearchMaxPitchDegrees = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search", meta = (ClampMin = "-89.0", ClampMax = "89.0"))
	float SearchNeutralPitchDegrees = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search", meta = (ClampMin = "0.0"))
	float SearchHalfCycleDuration = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float MaxImpactYawDegrees = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float MaxImpactPitchDegrees = 22.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0.0"))
	float ImpactHoldDuration = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0.0"))
	float ImpactRecoverySpeedDegrees = 12.0f;
};

UCLASS()
class OUTLIER_API AAutoTurret : public AEnemyBase, public IWeaponMuzzleProvider
{
	GENERATED_BODY()

public:
	AAutoTurret();
	static bool IsTurretDiagnosticsEnabled();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;
	virtual float ReceiveOutlierDamage(const FOutlierDamageRequest& Request) override;
	virtual FGenericTeamId GetGenericTeamId() const override;
	virtual FVector GetPawnViewLocation() const override;
	virtual FVector GetCombatAimPoint(const AActor* TargetActor) const override;
	virtual FRotator GetViewRotation() const override;
	virtual bool CanUseEnemyPerception() const override;
	virtual bool UsesHearingPerception() const override { return false; }
	virtual bool CanUseRoomTargetSharing() const override;
	virtual USkeletalMeshComponent* GetWeaponMuzzleComponent(bool bFirstPerson) const override;
	virtual FName GetWeaponMuzzleSocketName(bool bFirstPerson) const override;
	virtual void GetWeaponMuzzleSocketNames(bool bFirstPerson, TArray<FName>& OutSocketNames) const override;
	virtual bool UsesIndependentMuzzleShots() const override;
	virtual void ResetWeaponMuzzleSequence() override;
	virtual void AdvanceWeaponMuzzleSequence() override;
	virtual void ApplyExplosionReaction(const FVector& ExplosionOrigin, float EnemyImpulseScale,
		float TurretReactionScale, float EffectRatio) override;

	UFUNCTION(BlueprintPure, Category = "Enemy|Turret")
	bool IsDeployed() const { return bDeployed; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Turret")
	bool IsDeploying() const { return bDeploymentStarted && !bDeployed; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Turret")
	bool IsHackedToPlayerTeam() const { return bHackedToPlayerTeam; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Turret")
	const FAutoTurretBehaviorRow& GetTurretBehavior() const { return RuntimeTurretBehavior; }

	bool BeginTurretDeployment();
	void PlayFireMontage();
	void StopFireMontage();

	UFUNCTION(BlueprintCallable, Category = "Enemy|Turret|Deploy")
	void NotifyDeploySequenceFinished();

	bool UpdateTurretAimAtActor(AActor* TargetActor, float DeltaTime, bool bAttackRotation);
	bool UpdateTurretAimAtLocation(const FVector& TargetLocation, float DeltaTime,
		bool bAttackRotation, bool bUseSearchPitch = false);
	virtual bool UpdateAttackLocation(const FVector& TargetLocation) override;
	FVector GetCurrentTurretAimLocation() const { return CurrentAimLocation; }

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void ApplyClassStatOverrides() override;
	virtual void ApplyMovementFromRuntimeStat() override;
	virtual void PrepareForStateTreeStart() override;
	virtual void HandleDeath() override;
	virtual float GetDeathDestroyDelay() const override;
	virtual void HandleHackEffect(FGameplayTag EffectTag, const FHackResultContext& Context) override;
	virtual void HandleHackStarted(const FHackQueryContext& Context) override;
	virtual void HandleHackCompleted(const FHackResultContext& Context) override;
	virtual void HandleEMPStarted(FGameplayTag EffectTag) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Turret")
	TObjectPtr<USceneComponent> TurretHeadPivot;

	// Yaw 피벗과 분리하여 총구의 상하 조준이 메시의 Roll 축으로 섞이지 않게 한다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Turret")
	TObjectPtr<USceneComponent> TurretHeadPitchPivot;

	// Base와 다른 Skeleton을 사용하는 Head 애니메이션은 이 Mesh에서 별도로 재생한다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Turret")
	TObjectPtr<USkeletalMeshComponent> TurretHeadMesh;

	// 고정형 터렛은 Character Capsule 대신 전개된 몸체에 맞춘 Box로 플레이어 이동을 차단한다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Turret|Collision")
	TObjectPtr<UBoxComponent> TurretBlockingBox;

	// Base/Body Mesh에 만든 Head 기준 소켓을 지정하면 Pivot을 해당 위치와 회전에 자동 정렬한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Turret|Head")
	FName HeadPivotSocketName = NAME_None;

	// FBX의 총구 전방축이 Unreal +X와 반대인 경우 상하 조준 방향만 뒤집는다. 논리 Hitscan 방향에는 영향을 주지 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Turret|Head",
		meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float HeadVisualPitchDirection = 1.0f;

	// 배열 순서가 교대 발사 순서이며, 각 이름과 동일하거나 해당 이름으로 시작하는 소켓을 한 그룹으로 수집한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Turret|Weapon")
	TArray<FName> MuzzleGroupPrefixes;

	// 이 본과 하위 본의 Physics Body는 총알을 막되 터렛 HP에는 피해를 전달하지 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Turret|Collision")
	TArray<FName> NoDamageBoneRoots = { TEXT("HatchBase") };

	// Hidden 상태에서 감춰지는 실제 터렛 본이다. Deploy 시작 시 다시 Query Collision을 켠다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Turret|Collision")
	TArray<FName> HiddenCollisionBoneRoots = { TEXT("TurretBase") };

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Turret|Data")
	FDataTableRowHandle TurretBehaviorRow;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Turret|Animation")
	TObjectPtr<UAnimMontage> DeployMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Turret|Animation")
	TObjectPtr<UAnimMontage> FireMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Turret|Animation")
	TObjectPtr<UAnimMontage> DeathMontage;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Enemy|Turret|Data")
	FAutoTurretBehaviorRow RuntimeTurretBehavior;

	// 공격을 유지하는 병행 복구와 공격을 중단하는 StateTree 반응을 BP별로 비교한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Turret|Impact")
	EAutoTurretImpactReactionMode ImpactReactionMode =
		EAutoTurretImpactReactionMode::ConcurrentOffsetRecovery;

	UPROPERTY(ReplicatedUsing = OnRep_DeploymentState, VisibleInstanceOnly, BlueprintReadOnly, Category = "Enemy|Turret")
	uint8 bDeploymentStarted : 1 = false;

	UPROPERTY(ReplicatedUsing = OnRep_DeploymentState, VisibleInstanceOnly, BlueprintReadOnly, Category = "Enemy|Turret")
	uint8 bDeployed : 1 = false;

	UPROPERTY(ReplicatedUsing = OnRep_HackedTeam, VisibleInstanceOnly, BlueprintReadOnly, Category = "Enemy|Turret")
	uint8 bHackedToPlayerTeam : 1 = false;

	UFUNCTION()
	void OnRep_DeploymentState();

	UFUNCTION()
	void OnRep_HackedTeam();

	UFUNCTION(BlueprintImplementableEvent, Category = "Enemy|Turret|Deploy")
	void OnTurretDeploymentStarted();

	UFUNCTION(BlueprintImplementableEvent, Category = "Enemy|Turret|Deploy")
	void OnTurretDeploymentCompleted();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastBeginTurretDeployment();

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayFireMontage();

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastStopFireMontage();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayDeathMontage();

private:
	friend struct FEnemyRecoverTurretImpactOffsetTask;

	void StartImpactRecovery();
	void TickImpactRecovery();
	void StopImpactRecovery();
	bool UpdateTurretImpactRecovery(float DeltaTime);
	void ResetTurretImpactRecovery();
	void ApplyHeadRotation();
	void CacheAimOriginMuzzleSockets();
	void ConfigureHeadPivotAttachment();
	void ApplyTurretCollisionState();
	bool IsNoDamageBone(FName BoneName) const;
	void ConfigureTurretHackPolicy();
	void ApplyDeploymentRuntimeState();
	void CompleteTurretDeployment();
	void ApplyHackedTeamState();
	static void PlayMontageOnMesh(USkeletalMeshComponent* TargetMesh, UAnimMontage* Montage);
	static void StopMontageOnMesh(USkeletalMeshComponent* TargetMesh, UAnimMontage* Montage = nullptr);

	FRotator CurrentAimOffset = FRotator::ZeroRotator;
	FRotator ImpactRotationOffset = FRotator::ZeroRotator;
	FQuat HeadMountBasisRotation = FQuat::Identity;
	FVector CurrentAimLocation = FVector::ZeroVector;
	TArray<FName> AimOriginMuzzleSockets;
	FTimerHandle DeployFallbackTimerHandle;
	FTimerHandle ImpactRecoveryTimerHandle;
	double LastImpactRecoveryUpdateTimeSeconds = 0.0;
	float ImpactRecoveryHoldRemaining = 0.0f;
	int32 CurrentMuzzleGroupIndex = 0;
};
