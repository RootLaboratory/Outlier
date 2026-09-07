#include "GAS/Data/OutlierPartnerAbilityConfig.h"

bool FOutlierPartnerAbilityConfig::IsValid(FString& OutError) const
{
	const auto RequireNonNegative = [&OutError](float Value, const TCHAR* Field)
	{
		if (Value >= 0.0f)
		{
			return true;
		}

		OutError = FString::Printf(TEXT("%s must be non-negative"), Field);
		return false;
	};

	const bool bValid = RequireNonNegative(EMPCooldown, TEXT("EMP cooldown"))
		&& RequireNonNegative(ShieldCooldown, TEXT("Shield cooldown"))
		&& RequireNonNegative(HackCooldown, TEXT("Hack cooldown"))
		&& RequireNonNegative(ScanCooldown, TEXT("Scan cooldown"))
		&& RequireNonNegative(ScanDuration, TEXT("Scan duration"))
		&& RequireNonNegative(ScanRange, TEXT("Scan range"))
		&& RequireNonNegative(HackEffectiveRange, TEXT("Hack effective range"))
		&& RequireNonNegative(ShieldAmount, TEXT("Shield amount"))
		&& RequireNonNegative(MarkDuration, TEXT("EMP mark duration"))
		&& RequireNonNegative(StunDuration, TEXT("EMP stun duration"));
	if (bValid)
	{
		OutError.Reset();
	}
	return bValid;
}

bool FOutlierPartnerAbilityConfig::Equals(const FOutlierPartnerAbilityConfig& Other) const
{
	return FMath::IsNearlyEqual(EMPCooldown, Other.EMPCooldown)
		&& FMath::IsNearlyEqual(ShieldCooldown, Other.ShieldCooldown)
		&& FMath::IsNearlyEqual(HackCooldown, Other.HackCooldown)
		&& FMath::IsNearlyEqual(ScanCooldown, Other.ScanCooldown)
		&& FMath::IsNearlyEqual(ScanDuration, Other.ScanDuration)
		&& FMath::IsNearlyEqual(ScanRange, Other.ScanRange)
		&& FMath::IsNearlyEqual(HackEffectiveRange, Other.HackEffectiveRange)
		&& FMath::IsNearlyEqual(ShieldAmount, Other.ShieldAmount)
		&& FMath::IsNearlyEqual(MarkDuration, Other.MarkDuration)
		&& FMath::IsNearlyEqual(StunDuration, Other.StunDuration);
}
