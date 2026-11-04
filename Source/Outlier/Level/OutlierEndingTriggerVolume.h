#pragma once

#include "CoreMinimal.h"
#include "Tutorial/Actors/Tuto_EventTriggerVolume.h"
#include "OutlierEndingTriggerVolume.generated.h"

class UOutlierEndingWidget;

/** 로컬 겹침 감지, 서버 진입 검증, 재시도 및 일회성 복제를 재사용한다. */
UCLASS(HideCategories = ("Tutorial|Event", "Tutorial|World Guide"))
class OUTLIER_API AOutlierEndingTriggerVolume : public ATuto_EventTriggerVolume
{
	GENERATED_BODY()

protected:
	virtual bool HandlePawnEntered(AOutlierPlayerState* SourcePlayerState) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ending")
	TSubclassOf<UOutlierEndingWidget> EndingWidgetClass;
};
