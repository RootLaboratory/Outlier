#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Tuto_EventTriggerData.generated.h"

/** Presentation regions controlled by completion/cancellation. Voice and subtitles keep their own lifetime. */
UENUM(BlueprintType)
enum class ETuto_EventRegion : uint8
{
	Upper,
	Gif,
	Both,
	WorldGuide,
	All
};

UENUM()
enum class ETuto_EventOperation : uint8
{
	Trigger,
	Complete,
	Cancel,
	Overlap
};

/** One event configuration. The caller chooses when to send this entry. */
USTRUCT(BlueprintType)
struct OUTLIER_API FTuto_EventTriggerData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event", meta = (Categories = "Event"))
	FGameplayTag EventTag;
};
