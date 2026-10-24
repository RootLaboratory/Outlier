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

	// 실드가 남아 있는 동안 대상 최대 체력 대비 초당 회복 비율. Effect에서 주입한다.
	UPROPERTY()
	float ShieldHealthRegenRate = 0.0f;

	UPROPERTY()
	float MarkDuration = 0.0f;

	UPROPERTY()
	float StunDuration = 0.0f;

	// AutoTarget FunctionOverride의 자동 선택 연출 시간. Upgrade Effect가 주입한다.
	UPROPERTY()
	float EMPAutoTargetPendingTime = 0.0f;

	// EvasiveBurst 태그가 있을 때만 사용한다. 속도 Attribute 대신 최종 부스트 속도에 곱한다.
	UPROPERTY()
	float EvasiveBurstMultiplier = 1.0f;

	UPROPERTY()
	float EvasiveBurstDuration = 0.0f;

	UPROPERTY()
	float HackClickSuccessRangeMultiplier = 1.0f;

	UPROPERTY()
	float HackSpinningGapMultiplier = 1.0f;

	bool IsValid(FString& OutError) const;
	bool Equals(const FOutlierPartnerAbilityConfig& Other) const;
};
