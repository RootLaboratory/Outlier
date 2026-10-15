#pragma once

#include "CoreMinimal.h"
#include "Event/OutlierEventTypes.h"
#include "OutlierWorldGuideTypes.generated.h"

/** Row Name은 WidgetID와 같다. 내용과 렌더링은 대상 Component의 WBP에서 설정한다. */
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

/** DT Row가 아닌 Queue 컨텍스트. 분기 액터는 완료 시 NextWidgetID를 재지정할 수 있다. */
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

	/** Authority가 할당한다. 0은 대기 상태이다. */
	UPROPERTY(BlueprintReadOnly, Category = "World Guide")
	int32 RequestID = 0;

	/** 런타임 Chain 이력으로 설정된 후속 목표의 무한 루프를 막는다. */
	UPROPERTY(BlueprintReadOnly, Category = "World Guide")
	TArray<FGameplayTag> VisitedWidgetIDs;
};
