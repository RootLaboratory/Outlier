#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataTable.h"
#include "GameplayEffectTypes.h"
#include "PartnerVitalityComponent.generated.h"

class UOutlierAbilitySystemComponent;
struct FOnAttributeChangeData;
struct FActiveGameplayEffect;

UCLASS(ClassGroup = Partner)
class OUTLIER_API UPartnerVitalityComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPartnerVitalityComponent();

	bool InitializeFromDataTables(
		const FDataTableRowHandle& VitalityRow,
		const FDataTableRowHandle& SurvivalRow);
	void BindObservers();
	void UnbindObservers();
	void BeginOwnerTeardown();
	bool IsRebooting() const;
	bool RemoveRebootEffect();
	void SetEnemyPossessionProtection(bool bEnabled);

	float GetConfiguredRebootTime() const { return ConfiguredRebootTime; }
	bool IsInitialized() const { return bInitialized; }

	static bool ValidateDataTableRows(
		const FDataTableRowHandle& VitalityRow,
		const FDataTableRowHandle& SurvivalRow,
		float& OutMaxHealth,
		float& OutRebootTime,
		FString& OutError);

private:
	bool FailInitialization(const FString& Error) const;
	void HandleHealthChanged(const FOnAttributeChangeData& ChangeData);
	void HandleMaxHealthChanged(const FOnAttributeChangeData& ChangeData);
	void HandleGameplayEffectRemoved(const FActiveGameplayEffect& RemovedEffect);
	void RefreshDamagePresentation() const;
	void EnterReboot();
	void CompleteReboot();
	UOutlierAbilitySystemComponent* ResolveAbilitySystem() const;

	float ConfiguredRebootTime = 0.0f;
	bool bInitialized = false;
	bool bObserversBound = false;
	bool bOwnerTearingDown = false;
	FDelegateHandle HealthChangedDelegateHandle;
	FDelegateHandle MaxHealthChangedDelegateHandle;
	FDelegateHandle GameplayEffectRemovedDelegateHandle;
	FActiveGameplayEffectHandle RebootEffectHandle;
	FActiveGameplayEffectHandle PossessionProtectionEffectHandle;
};
