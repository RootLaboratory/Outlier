#include "GAS/Data/OutlierShooterSuitAbilityDataRow.h"

namespace
{
const FName CommonRowName(TEXT("Common"));
const FName QuantumLeapRowName(TEXT("QuantumLeap"));
const FName BulletReflectionRowName(TEXT("BulletReflection"));
const FName StealthRowName(TEXT("Stealth"));
const FName WeaponOverchargeRowName(TEXT("WeaponOvercharge"));

// 0 은 허용한다 ( 업그레이드 델타가 과해서 쿨다운/사거리 등이 바닥까지 깎이는 경우,
// ApplyConfigDelta 가 0 으로 클램프해두므로 여기서도 0을 정상값으로 받아줘야 한다 ).
// 음수만 실제로 잘못된 config 로 취급한다.
bool RequireNonNegative(float Value, const TCHAR* Field, FString& OutError)
{
	if (Value >= 0.0f)
	{
		return true;
	}

	OutError = FString::Printf(TEXT("%s must be non-negative"), Field);
	return false;
}

bool NearlyEqualRow(
	const FOutlierShooterSuitAbilityDataRow& A,
	const FOutlierShooterSuitAbilityDataRow& B)
{
	return FMath::IsNearlyEqual(A.DurationSeconds, B.DurationSeconds)
		&& FMath::IsNearlyEqual(A.CooldownSeconds, B.CooldownSeconds)
		&& FMath::IsNearlyEqual(A.CastTimeSeconds, B.CastTimeSeconds)
		&& FMath::IsNearlyEqual(A.MaxPartnerDistance, B.MaxPartnerDistance)
		&& FMath::IsNearlyEqual(A.PartnerOffset, B.PartnerOffset)
		&& FMath::IsNearlyEqual(A.ReflectionRadius, B.ReflectionRadius)
		&& FMath::IsNearlyEqual(A.ReflectDamageMult, B.ReflectDamageMult)
		&& FMath::IsNearlyEqual(A.ShieldDrainPerSecond, B.ShieldDrainPerSecond)
		&& FMath::IsNearlyEqual(A.FireRateMultiplier, B.FireRateMultiplier)
		&& FMath::IsNearlyEqual(A.SpreadMultiplier, B.SpreadMultiplier)
		&& FMath::IsNearlyEqual(A.ShieldRecoveryDelay, B.ShieldRecoveryDelay);
}
}

bool FOutlierShooterSuitConfig::IsValid(FString& OutError) const
{
	return RequireNonNegative(MaxPartnerDistance, TEXT("Common.MaxPartnerDistance"), OutError)
		&& RequireNonNegative(QuantumLeap.CastTimeSeconds, TEXT("QuantumLeap.CastTimeSeconds"), OutError)
		&& RequireNonNegative(QuantumLeap.CooldownSeconds, TEXT("QuantumLeap.CooldownSeconds"), OutError)
		&& RequireNonNegative(QuantumLeap.PartnerOffset, TEXT("QuantumLeap.PartnerOffset"), OutError)
		&& RequireNonNegative(BulletReflection.DurationSeconds, TEXT("BulletReflection.DurationSeconds"), OutError)
		&& RequireNonNegative(BulletReflection.CooldownSeconds, TEXT("BulletReflection.CooldownSeconds"), OutError)
		&& RequireNonNegative(BulletReflection.ReflectionRadius, TEXT("BulletReflection.ReflectionRadius"), OutError)
		&& RequireNonNegative(BulletReflection.ReflectDamageMult, TEXT("BulletReflection.ReflectDamageMult"), OutError)
		&& RequireNonNegative(Stealth.DurationSeconds, TEXT("Stealth.DurationSeconds"), OutError)
		&& RequireNonNegative(Stealth.CooldownSeconds, TEXT("Stealth.CooldownSeconds"), OutError)
		&& RequireNonNegative(WeaponOvercharge.DurationSeconds, TEXT("WeaponOvercharge.DurationSeconds"), OutError)
		&& RequireNonNegative(WeaponOvercharge.CooldownSeconds, TEXT("WeaponOvercharge.CooldownSeconds"), OutError)
		&& RequireNonNegative(WeaponOvercharge.ShieldDrainPerSecond, TEXT("WeaponOvercharge.ShieldDrainPerSecond"), OutError)
		&& RequireNonNegative(WeaponOvercharge.FireRateMultiplier, TEXT("WeaponOvercharge.FireRateMultiplier"), OutError)
		&& RequireNonNegative(WeaponOvercharge.SpreadMultiplier, TEXT("WeaponOvercharge.SpreadMultiplier"), OutError)
		&& RequireNonNegative(WeaponOvercharge.ShieldRecoveryDelay, TEXT("WeaponOvercharge.ShieldRecoveryDelay"), OutError);
}

bool FOutlierShooterSuitConfig::Equals(const FOutlierShooterSuitConfig& Other) const
{
	return FMath::IsNearlyEqual(MaxPartnerDistance, Other.MaxPartnerDistance)
		&& NearlyEqualRow(QuantumLeap, Other.QuantumLeap)
		&& NearlyEqualRow(BulletReflection, Other.BulletReflection)
		&& NearlyEqualRow(Stealth, Other.Stealth)
		&& NearlyEqualRow(WeaponOvercharge, Other.WeaponOvercharge);
}

bool OutlierShooterSuitData::TryResolveConfiguration(
	const UDataTable* DataTable,
	FOutlierShooterSuitConfig& OutConfig,
	FString& OutError)
{
	if (!DataTable)
	{
		OutError = TEXT("Shooter Suit DataTable is null");
		return false;
	}
	if (DataTable->GetRowStruct() != FOutlierShooterSuitAbilityDataRow::StaticStruct())
	{
		OutError = FString::Printf(
			TEXT("Shooter Suit DataTable row struct must be %s"),
			*FOutlierShooterSuitAbilityDataRow::StaticStruct()->GetName());
		return false;
	}

	const TSet<FName> ExpectedRows =
	{
		CommonRowName,
		QuantumLeapRowName,
		BulletReflectionRowName,
		StealthRowName,
		WeaponOverchargeRowName
	};
	TSet<FName> ActualRows;
	for (const FName& RowName : DataTable->GetRowNames())
	{
		ActualRows.Add(RowName);
	}
	bool bHasExactRows = ActualRows.Num() == ExpectedRows.Num();
	for (const FName& ExpectedRow : ExpectedRows)
	{
		bHasExactRows &= ActualRows.Contains(ExpectedRow);
	}
	if (!bHasExactRows)
	{
		OutError = TEXT("Shooter Suit DataTable must contain exactly Common, QuantumLeap, BulletReflection, Stealth, and WeaponOvercharge rows");
		return false;
	}

	auto FindRequiredRow = [DataTable, &OutError](FName RowName)
		-> const FOutlierShooterSuitAbilityDataRow*
	{
		const FOutlierShooterSuitAbilityDataRow* Row =
			DataTable->FindRow<FOutlierShooterSuitAbilityDataRow>(RowName, TEXT("ShooterSuitConfig"), false);
		if (!Row)
		{
			OutError = FString::Printf(TEXT("Missing Shooter Suit row %s"), *RowName.ToString());
		}
		return Row;
	};

	const FOutlierShooterSuitAbilityDataRow* Common = FindRequiredRow(CommonRowName);
	const FOutlierShooterSuitAbilityDataRow* QuantumLeap = FindRequiredRow(QuantumLeapRowName);
	const FOutlierShooterSuitAbilityDataRow* BulletReflection = FindRequiredRow(BulletReflectionRowName);
	const FOutlierShooterSuitAbilityDataRow* Stealth = FindRequiredRow(StealthRowName);
	const FOutlierShooterSuitAbilityDataRow* WeaponOvercharge = FindRequiredRow(WeaponOverchargeRowName);
	if (!Common || !QuantumLeap || !BulletReflection || !Stealth || !WeaponOvercharge)
	{
		return false;
	}

	OutConfig.MaxPartnerDistance = Common->MaxPartnerDistance;
	OutConfig.QuantumLeap = *QuantumLeap;
	OutConfig.BulletReflection = *BulletReflection;
	OutConfig.Stealth = *Stealth;
	OutConfig.WeaponOvercharge = *WeaponOvercharge;
	return OutConfig.IsValid(OutError);
}
