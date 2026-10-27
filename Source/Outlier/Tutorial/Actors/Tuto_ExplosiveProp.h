#pragma once

#include "CoreMinimal.h"
#include "Explosion/ExplosiveProp.h"
#include "Tutorial/Trigger/Tuto_EventTriggerInterface.h"
#include "Tuto_ExplosiveProp.generated.h"

class AController;
class UOutlierWorldGuideTargetComponent;

/** 실제 폭발이 완료되면 자신의 EventTag를 제출한다. Observer는 사용하지 않는다. */
UCLASS()
class OUTLIER_API ATuto_ExplosiveProp : public AExplosiveProp, public ITuto_EventTriggerInterface
{
	GENERATED_BODY()

public:
	ATuto_ExplosiveProp();
	virtual float ReceiveOutlierDamage(const FOutlierDamageRequest& Request) override;
	virtual FGameplayTag GetEventTag(int32 EventIndex) const override { return EventIndex == 0 ? EventTag : FGameplayTag(); }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial", meta = (Categories = "Event"))
	FGameplayTag EventTag;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tutorial|World Guide")
	TObjectPtr<UOutlierWorldGuideTargetComponent> WorldGuideTarget;

protected:
	virtual void HandleExplosionProcessed() override;

private:
	// 폭발 큐가 다음 프레임에 처리되더라도 기폭한 플레이어의 Pair를 보존한다.
	TWeakObjectPtr<AController> ProgressInstigator;
};
