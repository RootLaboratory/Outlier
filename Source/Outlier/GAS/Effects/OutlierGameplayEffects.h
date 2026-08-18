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

UCLASS()
class OUTLIER_API UOutlierWeaponReuseCooldownGameplayEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UOutlierWeaponReuseCooldownGameplayEffect(const FObjectInitializer& ObjectInitializer);
};

UCLASS(Abstract)
class OUTLIER_API UOutlierPartnerCooldownGameplayEffect : public UGameplayEffect
{
	GENERATED_BODY()

protected:
	UOutlierPartnerCooldownGameplayEffect(const FObjectInitializer& ObjectInitializer);
	void ConfigureCooldownTag(
		const FObjectInitializer& ObjectInitializer,
		const FGameplayTag& CooldownTag);
};

UCLASS()
class OUTLIER_API UOutlierPartnerEMPCooldownGameplayEffect : public UOutlierPartnerCooldownGameplayEffect
{
	GENERATED_BODY()

public:
	UOutlierPartnerEMPCooldownGameplayEffect(const FObjectInitializer& ObjectInitializer);
};

UCLASS()
class OUTLIER_API UOutlierPartnerShieldCooldownGameplayEffect : public UOutlierPartnerCooldownGameplayEffect
{
	GENERATED_BODY()

public:
	UOutlierPartnerShieldCooldownGameplayEffect(const FObjectInitializer& ObjectInitializer);
};

UCLASS()
class OUTLIER_API UOutlierPartnerHackCooldownGameplayEffect : public UOutlierPartnerCooldownGameplayEffect
{
	GENERATED_BODY()

public:
	UOutlierPartnerHackCooldownGameplayEffect(const FObjectInitializer& ObjectInitializer);
};

UCLASS()
class OUTLIER_API UOutlierPartnerScanCooldownGameplayEffect : public UOutlierPartnerCooldownGameplayEffect
{
	GENERATED_BODY()

public:
	UOutlierPartnerScanCooldownGameplayEffect(const FObjectInitializer& ObjectInitializer);
};
