#pragma once

#include "CoreMinimal.h"
#include "Weapon/MeleeWeaponBase.h"
#include "TimerManager.h"
#include "MeleeWeaponTestActor.generated.h"

// Concrete editor-only fixture for the abstract gameplay weapon lifecycle tests.
UCLASS(Transient, NotBlueprintable)
class AMeleeWeaponTestActor : public AMeleeWeaponBase
{
	GENERATED_BODY()

public:
	void SetTestOwner(ACharacter* InOwner)
	{
		WeaponOwner = InOwner;
		bIsEquipped = InOwner != nullptr;
	}

	bool HasPendingAttackTimers() const
	{
		return GetWorldTimerManager().TimerExists(AttackTimerHandle)
			|| GetWorldTimerManager().TimerExists(RecoveryTimerHandle);
	}
};
