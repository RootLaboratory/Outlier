#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "OutlierAbilitySystemComponent.generated.h"

class APawn;

UCLASS(ClassGroup = GAS)
class OUTLIER_API UOutlierAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	UOutlierAbilitySystemComponent();

	void InitializeForPawn(APawn* Pawn);
	void ClearForPawn(const APawn* Pawn);
	EGameplayEffectReplicationMode GetConfiguredReplicationMode() const { return ReplicationMode; }
};
