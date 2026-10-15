#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Tuto_EventTriggerData.generated.h"

/** 완료/취소로 제어되는 연출 영역. 보이스와 자막은 자체 수명을 유지한다. */
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

/** 이벤트 설정 하나. 이 항목을 보낼 시점은 호출자가 정한다. */
USTRUCT(BlueprintType)
struct OUTLIER_API FTuto_EventTriggerData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event", meta = (Categories = "Event"))
	FGameplayTag EventTag;
};
