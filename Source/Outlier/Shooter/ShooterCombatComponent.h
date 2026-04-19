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

public:
	UShooterCombatComponent();

	void TryReload();
	void TryStartAim();
	void TryStopAim();
	void TryStartAttack();
	void TryStopAttack();

	void RefreshCombatState();
	void RefreshWeaponMode();
	void ResolveStateConflicts();

	void StopAimInternal();
	void BeginReloadInternal();
	void CancelReloadInternal();
	void FinishReloadInternal();
	void BeginSecondaryCooldownInternal(float CooldownDuration);
	void FinishSecondaryCooldownInternal();
	void HandleReloadCommitNotify();

	bool CanEnterCombatState(EWeaponMode InWeaponMode, ECombatState NextState) const;
	bool CanAimInCurrentState() const;
	bool CanReloadInCurrentState() const;
	bool CanFireInCurrentState() const;
};
