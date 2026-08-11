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

	if (!EventTag.IsValid())
	{
		AddValidationError(TEXT("Audio Event Definition requires a valid EventTag."));
	}
	else if (!EventTag.ToString().StartsWith(TEXT("Audio.Event.")))
	{
		AddValidationError(FString::Printf(
			TEXT("EventTag '%s' must start with 'Audio.Event.'."),
			*EventTag.ToString()));
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
