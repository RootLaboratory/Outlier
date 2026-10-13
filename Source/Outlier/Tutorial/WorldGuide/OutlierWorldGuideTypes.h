#pragma once

#include "CoreMinimal.h"
#include "Event/OutlierEventTypes.h"
#include "OutlierWorldGuideTypes.generated.h"

/** Row Name equals WidgetID. Content and rendering are configured on the target component's WBP. */
USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierWorldGuideDataRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Guide", meta = (Categories = "UI.Info.World"))
	FGameplayTag WidgetID;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Guide")
	EOutlierEventEndCondition EndCondition = EOutlierEventEndCondition::Manual;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Guide")
	EOutlierEventTriggerType TriggerType = EOutlierEventTriggerType::CHAIN;

	bool IsValid() const
	{
		return WidgetID.IsValid() && (EndCondition == EOutlierEventEndCondition::Manual
			|| EndCondition == EOutlierEventEndCondition::Overlap);
	}
};

/** Queue context, not a DT row. A branch actor can override NextWidgetID when completing. */
USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierWorldGuideContext
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Guide", meta = (Categories = "UI.Info.World"))
	FGameplayTag WidgetID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Guide", meta = (Categories = "Event"))
	FGameplayTag OriginEventTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Guide", meta = (Categories = "UI.Info.World"))
	FGameplayTag NextWidgetID;

	UPROPERTY(BlueprintReadOnly, Category = "World Guide")
	EOutlierEventEndCondition EndCondition = EOutlierEventEndCondition::Manual;

	/** Assigned by the authority. Zero is the idle state. */
	UPROPERTY(BlueprintReadOnly, Category = "World Guide")
	int32 RequestID = 0;

	/** Runtime chain history prevents a configured successor from looping forever. */
	UPROPERTY(BlueprintReadOnly, Category = "World Guide")
	TArray<FGameplayTag> VisitedWidgetIDs;
};
