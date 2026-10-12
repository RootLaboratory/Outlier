#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "OutlierInfoTypes.generated.h"

UENUM(BlueprintType)
enum class EOutlierInfoRegion : uint8
{
	Upper,
	Right,
	BottomTTS
};

UENUM(BlueprintType)
enum class EOutlierInfoEndCondition : uint8
{
	Manual,
	Time,
	Overlap,
	/** Ends only when the source reports completion or cancellation. */
	Source
};

/** A presentation request only. Content and live state belong to the receiving widget. */
USTRUCT(BlueprintType)
struct TAGDRIVENUI_API FOutlierInfoWidgetRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info", meta = (Categories = "Event"))
	FGameplayTag EventTag;

	/** Optional direct TTS reference, including voices without an Event DT row. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info", meta = (Categories = "Audio.Context"))
	FGameplayTag TTSTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info", meta = (Categories = "UI.Info.Upper"))
	FGameplayTag UpperTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info")
	FGameplayTag GifTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info")
	EOutlierInfoEndCondition EndCondition = EOutlierInfoEndCondition::Source;

	/** Starts when displayed. Zero lets a Time widget read its DT default; Source ignores duration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info", meta = (ClampMin = "0.0"))
	float Duration = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info", meta = (Categories = "Event"))
	FGameplayTag NextEventTag;
};
