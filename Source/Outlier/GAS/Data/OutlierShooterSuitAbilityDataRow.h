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

	float MaxPartnerDistance = 0.0f;
	FOutlierShooterSuitAbilityDataRow QuantumLeap;
	FOutlierShooterSuitAbilityDataRow BulletReflection;
	FOutlierShooterSuitAbilityDataRow Stealth;
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
