#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "OutlierGameplayEffects.generated.h"

UCLASS()
class OUTLIER_API UOutlierDamageGameplayEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UOutlierDamageGameplayEffect();
};

UCLASS()
class OUTLIER_API UOutlierVitalityInitializationGameplayEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UOutlierVitalityInitializationGameplayEffect();
};

UCLASS()
class OUTLIER_API UOutlierHealthRecoveryGameplayEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UOutlierHealthRecoveryGameplayEffect();
};

UCLASS()
class OUTLIER_API UOutlierRebootGameplayEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UOutlierRebootGameplayEffect(const FObjectInitializer& ObjectInitializer);
};

UCLASS()
class OUTLIER_API UOutlierDamageImmuneGameplayEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UOutlierDamageImmuneGameplayEffect(const FObjectInitializer& ObjectInitializer);
};

UCLASS()
class OUTLIER_API UOutlierShieldRecoveryGameplayEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UOutlierShieldRecoveryGameplayEffect();
};

UCLASS()
class OUTLIER_API UOutlierPartnerShieldGameplayEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UOutlierPartnerShieldGameplayEffect();
};

UCLASS()
class OUTLIER_API UOutlierDeadGameplayEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UOutlierDeadGameplayEffect(const FObjectInitializer& ObjectInitializer);
};
