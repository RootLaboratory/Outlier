#include "Enemy/EnemyAdaptationDefinition.h"

int32 UEnemyAdaptationDefinition::ClampStack(int32 Stack) const
{
	return FMath::Clamp(Stack, 0, MaxGunAdaptationStack);
}

EEnemyAdaptationState UEnemyAdaptationDefinition::ResolveState(int32 Stack) const
{
	const int32 ClampedStack = ClampStack(Stack);
	if (ClampedStack >= ResistanceMaxThreshold)
	{
		return EEnemyAdaptationState::ResistanceMax;
	}
	if (ClampedStack >= ResistanceLevel2Threshold)
	{
		return EEnemyAdaptationState::ResistanceLevel2;
	}
	if (ClampedStack >= ResistanceLevel1Threshold)
	{
		return EEnemyAdaptationState::ResistanceLevel1;
	}
	if (ClampedStack >= ShieldPreviewThreshold)
	{
		return EEnemyAdaptationState::ShieldPreview;
	}
	return EEnemyAdaptationState::Normal;
}

float UEnemyAdaptationDefinition::ResolveGunDamageMultiplier(int32 Stack) const
{
	switch (ResolveState(Stack))
	{
	case EEnemyAdaptationState::ResistanceLevel1:
		return ResistanceLevel1GunDamageMultiplier;
	case EEnemyAdaptationState::ResistanceLevel2:
		return ResistanceLevel2GunDamageMultiplier;
	case EEnemyAdaptationState::ResistanceMax:
		return ResistanceMaxGunDamageMultiplier;
	case EEnemyAdaptationState::Normal:
	case EEnemyAdaptationState::ShieldPreview:
	default:
		return 1.0f;
	}
}

float UEnemyAdaptationDefinition::ResolveBreakStunSeconds(int32 Stack) const
{
	switch (ResolveState(Stack))
	{
	case EEnemyAdaptationState::ResistanceLevel1:
		return ResistanceLevel1BreakStunSeconds;
	case EEnemyAdaptationState::ResistanceLevel2:
		return ResistanceLevel2BreakStunSeconds;
	case EEnemyAdaptationState::ResistanceMax:
		return ResistanceMaxBreakStunSeconds;
	case EEnemyAdaptationState::Normal:
	case EEnemyAdaptationState::ShieldPreview:
	default:
		return 0.0f;
	}
}

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
		|| ResistanceMaxBreakStunSeconds < 0.0f
		|| ShieldBreakDamage < 0.0f)
	{
		AddError(NSLOCTEXT(
			"EnemyAdaptation",
			"InvalidBreakStun",
			"Break stun durations and break damage cannot be negative."));
	}

	return Result == EDataValidationResult::NotValidated
		? EDataValidationResult::Valid
		: Result;
}
#endif
