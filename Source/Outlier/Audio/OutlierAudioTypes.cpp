#include "Audio/OutlierAudioTypes.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

const FPrimaryAssetType UOutlierAudioEventDefinition::PrimaryAssetType(TEXT("AudioEvent"));

FPrimaryAssetId UOutlierAudioEventDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(PrimaryAssetType, GetFName());
}

#if WITH_EDITOR
EDataValidationResult UOutlierAudioEventDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	auto AddValidationError = [&Context, &Result](const FString& Message)
	{
		Context.AddError(FText::FromString(Message));
		Result = EDataValidationResult::Invalid;
	};

	if (VolumeMultiplier < 0.0f)
	{
		AddValidationError(TEXT("VolumeMultiplier cannot be negative."));
	}

	if (PitchMultiplier < 0.0f)
	{
		AddValidationError(TEXT("PitchMultiplier cannot be negative."));
	}

	if (Variants.IsEmpty())
	{
		AddValidationError(TEXT("Audio Event Definition requires at least one Variant."));
	}

	for (int32 VariantIndex = 0; VariantIndex < Variants.Num(); ++VariantIndex)
	{
		const FOutlierAudioVariant& Variant = Variants[VariantIndex];
		if (Variant.Sound.IsNull())
		{
			AddValidationError(FString::Printf(
				TEXT("Variant %d has no Sound asset."),
				VariantIndex));
		}

		if (Variant.Weight <= 0.0f)
		{
			AddValidationError(FString::Printf(
				TEXT("Variant %d has a non-positive Weight."),
				VariantIndex));
		}
	}

	return Result == EDataValidationResult::NotValidated
		? EDataValidationResult::Valid
		: Result;
}
#endif

const FPrimaryAssetType UOutlierAudioBank::PrimaryAssetType(TEXT("AudioBank"));

FPrimaryAssetId UOutlierAudioBank::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(PrimaryAssetType, GetFName());
}

#if WITH_EDITOR
EDataValidationResult UOutlierAudioBank::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	auto AddValidationError = [&Context, &Result](const FString& Message)
	{
		Context.AddError(FText::FromString(Message));
		Result = EDataValidationResult::Invalid;
	};

	if (!TypeTag.IsValid())
	{
		AddValidationError(TEXT("Audio Bank requires a valid TypeTag."));
	}
	else if (!TypeTag.ToString().StartsWith(TEXT("Audio.Type.")))
	{
		AddValidationError(FString::Printf(
			TEXT("TypeTag '%s' must start with 'Audio.Type.'."),
			*TypeTag.ToString()));
	}

	if (Definitions.IsEmpty())
	{
		AddValidationError(TEXT("Audio Bank requires at least one Definition."));
	}

	for (int32 Index = 0; Index < Definitions.Num(); ++Index)
	{
		if (Definitions[Index].IsNull())
		{
			AddValidationError(FString::Printf(
				TEXT("Definitions[%d] is empty."),
				Index));
		}
	}

	return Result == EDataValidationResult::NotValidated
		? EDataValidationResult::Valid
		: Result;
}
#endif
