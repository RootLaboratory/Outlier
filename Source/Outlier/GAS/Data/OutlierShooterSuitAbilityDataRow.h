#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "OutlierShooterSuitAbilityDataRow.generated.h"

USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierShooterSuitAbilityDataRow : public FTableRowBase
{
	GENERATED_BODY()

	// QuantumLeap 행에서는 무적 도약 획득 후 이동 성공 시 부여할 무적 시간이다. 시전 시간과는 별개다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0", Units = "Seconds"))
	float DurationSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0", Units = "Seconds"))
	float CooldownSeconds = 0.0f;

	// Stealth 행: 30이면 기본 이동속도에 30%를 더한다. 은신 중/해제 후에 같은 값을 쓴다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0", Units = "Percent"))
	float MovementSpeedBonusPercent = 30.0f;

	// Stealth 행: 실제 은신 해제 후 이동속도 보너스를 유지할 시간. 0이면 종료 버프를 생략한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0", Units = "Seconds"))
	float ExitSpeedBuffDurationSeconds = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0", Units = "Seconds"))
	float CastTimeSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0", Units = "Centimeters"))
	float MaxPartnerDistance = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0", Units = "Centimeters"))
	float PartnerOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0", Units = "Centimeters"))
	float ReflectionRadius = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float ReflectDamageMult = 1.0f;

	// 반사 중 기존 자연 회복에 더할 초당 회복량이다. 0이면 추가 회복이 없다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float ShieldRecoveryBonusPerSecond = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float ShieldDrainPerSecond = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float FireRateMultiplier = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float SpreadMultiplier = 0.0f;

	// WeaponOvercharge 행: 조준 원 지름 / 화면 가로폭. 표시와 서버 후보 판정에 같은 비율을 쓴다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.001", ClampMax = "1.0"))
	float AutoAimDiameterWidthRatio = 1.0f / 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0", Units = "Seconds"))
	float ShieldRecoveryDelay = 0.0f;
};

USTRUCT()
struct OUTLIER_API FOutlierShooterSuitConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	float MaxPartnerDistance = 0.0f;

	// 아래 4개는 이름이 UOutlierUpgradeProjectionSettings::SuitRoleMappings 의
	// SuitConfigFieldName 과 리플렉션으로 매칭된다 ( UOutlierUpgradeComponent::ResolveSuitRow ).
	// 필드명을 바꾸면 ini 쪽 SuitConfigFieldName 도 같이 바꿔야 한다.
	// ( FOutlierShooterSuitConfig 는 BlueprintType 이 아니라서 BlueprintReadOnly 는 못 붙임 —
	//   FindFProperty 리플렉션은 UPROPERTY 만 있으면 동작하므로 EditAnywhere 로 충분하다. )
	UPROPERTY(EditAnywhere)
	FOutlierShooterSuitAbilityDataRow QuantumLeap;

	UPROPERTY(EditAnywhere)
	FOutlierShooterSuitAbilityDataRow BulletReflection;

	UPROPERTY(EditAnywhere)
	FOutlierShooterSuitAbilityDataRow Stealth;

	UPROPERTY(EditAnywhere)
	FOutlierShooterSuitAbilityDataRow WeaponOvercharge;

	bool IsValid(FString& OutError) const;
	bool Equals(const FOutlierShooterSuitConfig& Other) const;
};

namespace OutlierShooterSuitData
{
	OUTLIER_API bool TryResolveConfiguration(
		const UDataTable* DataTable,
		FOutlierShooterSuitConfig& OutConfig,
		FString& OutError);
}
