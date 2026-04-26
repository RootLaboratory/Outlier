// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Shooter/ShooterCharacterComponentBase.h"
#include "Shooter/ShooterCharacter.h"
#include "ShooterCombatComponent.generated.h"

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

	FTimerHandle SecondaryCooldownStateTimerHandle;
	FTimerHandle ReloadCommitFallbackTimerHandle;

public:
	UShooterCombatComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

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
	void HandleReloadCommitFallback();

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
