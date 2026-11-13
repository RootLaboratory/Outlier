// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Shooter/ShooterCharacterComponentBase.h"
#include "Shooter/ShooterCharacter.h"
#include "ShooterCombatComponent.generated.h"

class UAnimMontage;

UCLASS(ClassGroup=(Shooter), meta=(BlueprintSpawnableComponent))
class OUTLIER_API UShooterCombatComponent : public UShooterCharacterComponentBase
{
	GENERATED_BODY()
	friend class FOutlierShooterPresentationAnimationTest;
	friend class FShooterGroundedMovementTest;
	friend class FShooterActionIntentTest;
	friend class FShooterSprintInputModeTest;
	friend class FShooterSprintReloadTest;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	uint8 bWantsToAim : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	uint8 bWantsToFire : 1 = false;
	// 발사 대기/연사 간격에도 유지한다. 실제 무기 발사 상태와 별개로 Hold Sprint를 차단한다.
	bool bFireInputHeld = false;
	void UpdateFireInput(bool bHeld);

	UPROPERTY(ReplicatedUsing = OnRep_IsAiming, VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	uint8 bIsAiming : 1 = false;

	UPROPERTY(ReplicatedUsing = OnRep_IsReloading, VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	uint8 bIsReloading : 1 = false;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	uint8 bIsMeleeAttacking : 1 = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Fire", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SprintFireAllowedAlpha = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Fire", meta = (ClampMin = "0.0"))
	float SprintExitFireRetryInterval = 0.02f;

	FTimerHandle PendingSprintExitFireTimerHandle;
	FTimerHandle OverchargeFireRecoveryTimerHandle;
	FDelegateHandle WeaponReuseCooldownTagChangedHandle;
	TWeakObjectPtr<UAnimInstance> BoundFirstPersonReloadInstance;
	TWeakObjectPtr<UAnimInstance> BoundThirdPersonReloadInstance;
	TWeakObjectPtr<UAnimMontage> ActiveFirstPersonReloadMontage;
	TWeakObjectPtr<UAnimMontage> ActiveThirdPersonReloadMontage;

	uint8 bPendingSprintExitFire : 1 = false;
	TWeakObjectPtr<AWeaponBase> PendingSprintExitWeapon;
	uint32 PendingSprintExitRequest = 0;
	uint32 PendingSprintExitEpoch = 0;
	uint32 LastSprintExitRequest = 0;
	uint32 LastSprintExitEpoch = 0;
	double SprintExitFireDeadline = 0.0;

	// 기본 복귀 보간보다 넉넉한 상한이다. 멎은 애니메이션을 무한히 재시도하지 않는다.
	static constexpr float SprintExitFireTimeout = 2.0f;

	bool IsActionLockBlockingAimFire(const AShooterCharacter& ShooterCharacter) const;
	bool ShouldDelayFireForSprintExit(const AShooterCharacter& ShooterCharacter) const;
	void QueueSprintExitFire(AShooterCharacter& ShooterCharacter);
	void RetryPendingSprintExitFire();
	void ClearPendingSprintExitFire();
	void StartAttackInternal();
	void BindReloadMontageEndedDelegates();
	void UnbindReloadMontageEndedDelegates();
	void BindWeaponReuseCooldownObserver();
	void UnbindWeaponReuseCooldownObserver();
	void HandleWeaponReuseCooldownTagChanged(const FGameplayTag CooldownTag, int32 NewCount);

	UFUNCTION()
	void HandleReloadMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	UFUNCTION()
	void OnRep_IsReloading();

	UFUNCTION()
	void OnRep_IsAiming();

	void NotifyAimPresentation();

	void StartAimInternal();

public:
	UShooterCombatComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime,ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	void TryReload();
	void HandleAimPressed();
	void HandleAimReleased();
	// 입력 수집/RPC와 분리해 승인된 의도를 적용한다. 서버와 보정 재생에서 입력을 새로 발급하지 않는다.
	void ApplyAimInput(bool bNewWantsToAim);
	void TryStartAttack();
	void TryStopAttack();
	// 소유자는 복귀 준비만 알린다. 요청 수락/취소와 실제 발사는 서버가 검증한다.
	bool BeginSprintExitFire(AWeaponBase* Weapon, uint32 Request, uint32 Epoch);
	void ResolveSprintExitFire(uint32 Request, uint32 Epoch, bool bFire);
	void CancelPendingSprintExitFire();
	void CancelMeleeAttack();
	void HandleWeaponAttackStopped();
	void HandleAutoReloadRequested();
	void ResumeHeldFireAfterWeaponOvercharge();

	void RefreshCombatState();
	void RefreshWeaponMode();
	void ResolveStateConflicts();

	void StopAimInternal();
	void SuspendAimInternal();
	void RestoreAimIfRequested();
	void BeginReloadInternal();
	void CancelReloadInternal();
	void FinishReloadInternal();
	void HandleReloadCommitNotify();
	void HandleMeleeHitNotify();
	void HandleMeleeTraceBeginNotify();
	void HandleMeleeTraceTickNotify();
	void HandleMeleeTraceEndNotify();
	void HandleMeleeRecoveryEndNotify();

	bool CanEnterCombatState(EWeaponMode InWeaponMode, ECombatState NextState) const;
	bool CanAimInCurrentState() const;
	bool CanReloadInCurrentState() const;
	bool CanFireInCurrentState() const;

	void ClearInputIntent();
	bool IsSprintBlockedByAttack() const;
	bool IsFireInputHeld() const { return bFireInputHeld; }
	void ApplyFireInput(bool bHeld) { bFireInputHeld = bHeld; }

	bool WantsToAim() const { return bWantsToAim; }
	bool WantsToFire() const { return bWantsToFire; }
	bool IsAiming() const { return bIsAiming; }
	bool IsReloading() const { return bIsReloading; }
	bool IsSecondaryOnCooldown() const;
	bool IsMeleeAttacking() const { return bIsMeleeAttacking; }
};
