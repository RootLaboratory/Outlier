#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideTypes.h"
#include "OutlierWorldGuideWidget.generated.h"

/** 대상 마커 WBP용 선택적 네이티브 베이스. Component는 기존 UserWidget BP도 허용한다. */
UCLASS(Blueprintable, meta = (DisableNativeTick))
class OUTLIER_API UOutlierWorldGuideWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Transient, Category = "World Guide")
	FOutlierWorldGuideContext GuideContext;

	void SetGuideContext(const FOutlierWorldGuideContext& Context) { GuideContext = Context; }
};
