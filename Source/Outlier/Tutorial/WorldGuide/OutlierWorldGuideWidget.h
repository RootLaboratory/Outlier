#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideTypes.h"
#include "OutlierWorldGuideWidget.generated.h"

/** Optional native base for a target marker WBP. The component also accepts existing UserWidget BPs. */
UCLASS(Blueprintable, meta = (DisableNativeTick))
class OUTLIER_API UOutlierWorldGuideWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Transient, Category = "World Guide")
	FOutlierWorldGuideContext GuideContext;

	void SetGuideContext(const FOutlierWorldGuideContext& Context) { GuideContext = Context; }
};
