#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "OutlierShooterSuitAbilityDataRow.generated.h"

USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierShooterSuitAbilityDataRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0", Units = "Seconds"))
	float DurationSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0", Units = "Seconds"))
	float CooldownSeconds = 0.0f;

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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float ShieldDrainPerSecond = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float FireRateMultiplier = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float SpreadMultiplier = 0.0f;

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
