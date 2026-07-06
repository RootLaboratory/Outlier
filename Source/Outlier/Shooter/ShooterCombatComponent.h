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

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	uint8 bWantsToAim : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	uint8 bWantsToFire : 1 = false;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	uint8 bIsAiming : 1 = false;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	uint8 bIsReloading : 1 = false;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	uint8 bSecondaryOnCooldown : 1 = false;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	uint8 bIsMeleeAttacking : 1 = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Fire", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SprintFireAllowedAlpha = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Fire", meta = (ClampMin = "0.0"))
	float SprintExitFireRetryInterval = 0.02f;

	FTimerHandle SecondaryCooldownStateTimerHandle;
	FTimerHandle PendingSprintExitFireTimerHandle;

	uint8 bPendingSprintExitFire : 1 = false;

	bool IsActionLockBlockingAimFire(const AShooterCharacter& ShooterCharacter) const;
	bool ShouldDelayFireForSprintExit(const AShooterCharacter& ShooterCharacter) const;
	void QueueSprintExitFire(AShooterCharacter& ShooterCharacter);
	void RetryPendingSprintExitFire();
	void ClearPendingSprintExitFire();
	void BindReloadMontageEndedDelegates();
	void UnbindReloadMontageEndedDelegates();

	UFUNCTION()
	void HandleReloadMontageEnded(UAnimMontage* Montage, bool bInterrupted);

public:
	UShooterCombatComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime,ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	void TryReload();
	void HandleAimPressed();
	void HandleAimReleased();
	void TryStartAttack();
	void TryStopAttack();
	void HandleWeaponAttackStopped();
	void HandleAutoReloadRequested();

	void RefreshCombatState();
	void RefreshWeaponMode();
	void ResolveStateConflicts();

	void StopAimInternal();
	void BeginReloadInternal();
	void CancelReloadInternal();
	void FinishReloadInternal();
	void BeginSecondaryCooldownInternal(float CooldownDuration);
	void FinishSecondaryCooldownInternal();
	void ResetSecondaryCooldown();
	void HandleReloadCommitNotify();

	bool CanEnterCombatState(EWeaponMode InWeaponMode, ECombatState NextState) const;
	bool CanAimInCurrentState() const;
	bool CanReloadInCurrentState() const;
	bool CanFireInCurrentState() const;

	void ClearInputIntent();

	bool WantsToAim() const { return bWantsToAim; }
	bool WantsToFire() const { return bWantsToFire; }
	bool IsAiming() const { return bIsAiming; }
	bool IsReloading() const { return bIsReloading; }
	bool IsSecondaryOnCooldown() const { return bSecondaryOnCooldown; }
	bool IsMeleeAttacking() const { return bIsMeleeAttacking; }
};
