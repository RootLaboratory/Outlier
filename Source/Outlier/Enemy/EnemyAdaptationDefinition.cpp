#include "Enemy/EnemyAdaptationDefinition.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"

EDataValidationResult UEnemyAdaptationDefinition::IsDataValid(
	FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	auto AddError = [&Context, &Result](const FText& Error)
	{
		Context.AddError(Error);
		Result = EDataValidationResult::Invalid;
	};

	if (MaxGunAdaptationStack < 1 || GunKillIncrement < 1 || NonGunKillDecrement < 1)
	{
		AddError(NSLOCTEXT(
			"EnemyAdaptation",
			"InvalidStackSettings",
			"Max stack and kill stack changes must be greater than zero."));
	}

	const bool bThresholdsOrdered = ShieldPreviewThreshold < ResistanceLevel1Threshold
		&& ResistanceLevel1Threshold < ResistanceLevel2Threshold
		&& ResistanceLevel2Threshold < ResistanceMaxThreshold;
	if (!bThresholdsOrdered || ShieldPreviewThreshold < 1
		|| ResistanceMaxThreshold > MaxGunAdaptationStack)
	{
		AddError(NSLOCTEXT(
			"EnemyAdaptation",
			"InvalidThresholdOrder",
			"Thresholds must be strictly ordered and remain within the configured max stack."));
	}

	const bool bMultipliersValid = FMath::IsWithinInclusive(
		ResistanceLevel1GunDamageMultiplier, 0.0f, 1.0f)
		&& FMath::IsWithinInclusive(ResistanceLevel2GunDamageMultiplier, 0.0f, 1.0f)
		&& FMath::IsWithinInclusive(ResistanceMaxGunDamageMultiplier, 0.0f, 1.0f);
	if (!bMultipliersValid)
	{
		AddError(NSLOCTEXT(
			"EnemyAdaptation",
			"InvalidDamageMultiplier",
			"Gun damage multipliers must be between zero and one."));
	}

	if (ResistanceLevel1BreakStunSeconds < 0.0f
		|| ResistanceLevel2BreakStunSeconds < 0.0f
		|| ResistanceMaxBreakStunSeconds < 0.0f)
	{
		AddError(NSLOCTEXT(
			"EnemyAdaptation",
			"InvalidBreakStun",
			"Break stun durations cannot be negative."));
	}

	return Result == EDataValidationResult::NotValidated
		? EDataValidationResult::Valid
		: Result;
}
#endif

