#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "OutlierAbilitySystemComponent.generated.h"

class APawn;
class AController;

UCLASS(ClassGroup = GAS)
class OUTLIER_API UOutlierAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	UOutlierAbilitySystemComponent();

	void InitializeForPawn(APawn* Pawn);
	void ClearForPawn(const APawn* Pawn);
	bool ApplyDamageToSelf(
		float DamageAmount,
		AController* Instigator,
		AActor* DamageCauser,
		const FGameplayTag& DamageTag);
	bool ApplyShieldRecoveryToSelf(float Amount);
	bool ApplyPartnerShieldDeltaToSelf(float PartnerShieldDelta, float MaxPartnerShieldDelta);
	bool ApplyDeadStateToSelf();
	bool InitializeVitalityToSelf(float MaxHealth);
	bool RestoreHealthToMax();
	FActiveGameplayEffectHandle ApplyRebootStateToSelf(float DurationSeconds);
	FActiveGameplayEffectHandle ApplyDamageImmuneStateToSelf();
	bool RemoveActiveEffectFromSelf(FActiveGameplayEffectHandle Handle);
	EGameplayEffectReplicationMode GetConfiguredReplicationMode() const { return ReplicationMode; }
};
