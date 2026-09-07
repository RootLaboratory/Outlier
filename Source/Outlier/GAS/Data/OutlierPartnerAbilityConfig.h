#pragma once

#include "CoreMinimal.h"
#include "OutlierPartnerAbilityConfig.generated.h"

USTRUCT()
struct OUTLIER_API FOutlierPartnerAbilityConfig
{
	GENERATED_BODY()

	UPROPERTY()
	float EMPCooldown = 0.0f;

	UPROPERTY()
	float ShieldCooldown = 0.0f;

	UPROPERTY()
	float HackCooldown = 0.0f;

	UPROPERTY()
	float ScanCooldown = 0.0f;

	UPROPERTY()
	float ScanDuration = 0.0f;

	// DT_Partner_Skill 기준값. Upgrade AbilityConfig 투영(ConfigField)이 FindFProperty로
	// 이름을 찾으므로 멤버명은 DT_PartnerUpgradeEffect.csv의 ConfigField와 일치해야 한다.
	UPROPERTY()
	float ScanRange = 0.0f;

	UPROPERTY()
	float HackEffectiveRange = 0.0f;

	UPROPERTY()
	float ShieldAmount = 0.0f;

	UPROPERTY()
	float MarkDuration = 0.0f;

	UPROPERTY()
	float StunDuration = 0.0f;

	bool IsValid(FString& OutError) const;
	bool Equals(const FOutlierPartnerAbilityConfig& Other) const;
};
