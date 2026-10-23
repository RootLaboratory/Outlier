#include "GAS/Data/OutlierPartnerAbilityConfig.h"

bool FOutlierPartnerAbilityConfig::IsValid(FString& OutError) const
{
	OutError.Reset();
	if (!FMath::IsFinite(ShieldHealthRegenRate) || ShieldHealthRegenRate < 0.0f)
	{
		OutError = TEXT("Shield health regeneration rate must be finite and non-negative");
		return false;
	}
	if (!FMath::IsFinite(EMPAutoTargetPendingTime) || EMPAutoTargetPendingTime < 0.0f)
	{
		OutError = TEXT("EMP auto target pending time must be finite and non-negative");
		return false;
	}
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
		&& RequireNonNegative(StunDuration, TEXT("EMP stun duration"))
		&& FMath::IsFinite(EvasiveBurstMultiplier) && EvasiveBurstMultiplier >= 1.0f
		&& FMath::IsFinite(EvasiveBurstDuration)
		&& RequireNonNegative(EvasiveBurstDuration, TEXT("Evasive burst duration"));
	if (!bValid && OutError.IsEmpty())
	{
		OutError = TEXT("Evasive burst requires a finite multiplier >= 1 and a finite non-negative duration");
	}
	if (bValid)
	{
		if (!FMath::IsFinite(HackClickSuccessRangeMultiplier) || HackClickSuccessRangeMultiplier < 1.0f
			|| !FMath::IsFinite(HackSpinningGapMultiplier) || HackSpinningGapMultiplier < 1.0f)
		{
			OutError = TEXT("Hack click success range and spinning gap multipliers must be finite and >= 1");
			return false;
		}
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
		&& FMath::IsNearlyEqual(ShieldHealthRegenRate, Other.ShieldHealthRegenRate)
		&& FMath::IsNearlyEqual(MarkDuration, Other.MarkDuration)
		&& FMath::IsNearlyEqual(StunDuration, Other.StunDuration)
		&& FMath::IsNearlyEqual(EMPAutoTargetPendingTime, Other.EMPAutoTargetPendingTime)
		&& FMath::IsNearlyEqual(EvasiveBurstMultiplier, Other.EvasiveBurstMultiplier)
		&& FMath::IsNearlyEqual(EvasiveBurstDuration, Other.EvasiveBurstDuration)
		&& FMath::IsNearlyEqual(HackClickSuccessRangeMultiplier, Other.HackClickSuccessRangeMultiplier)
		&& FMath::IsNearlyEqual(HackSpinningGapMultiplier, Other.HackSpinningGapMultiplier);
}
