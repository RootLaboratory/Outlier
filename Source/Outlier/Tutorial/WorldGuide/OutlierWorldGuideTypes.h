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

	/** 같은 WidgetID 타깃 중 완료해야 하는 서로 다른 타깃 수. 1이면 타깃 하나의 완료가 곧 안내 완료이다.
	 *  2 이상이면 그룹 안내로, 완료된 타깃의 마커만 먼저 숨기고 수가 모이면 WidgetID를 완료해 Chain을 진행한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Guide", meta = (ClampMin = "1"))
	int32 RequiredTargetCount = 1;

	bool IsValid() const
	{
		return WidgetID.IsValid() && RequiredTargetCount >= 1 && (EndCondition == EOutlierEventEndCondition::Manual
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
