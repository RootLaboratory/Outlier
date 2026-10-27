#pragma once

#include "CoreMinimal.h"
#include "Interaction/PanelMoverSwitch.h"
#include "Tutorial/Trigger/Tuto_ProgressSourceInterface.h"
#include "Tuto_PanelMoverSwitch.generated.h"

class UOutlierWorldGuideTargetComponent;

/** 실제 PanelMover의 도착 알림을 받아 자기 마커와 Observer 진행을 완료한다. */
UCLASS()
class OUTLIER_API ATuto_PanelMoverSwitch : public APanelMoverSwitch, public ITuto_ProgressSourceInterface
{
	GENERATED_BODY()

public:
	ATuto_PanelMoverSwitch();

	/** 이 상속 컴포넌트에서 WidgetID, WidgetClass, 마커 anchor를 설정한다. 3곳 모두 같은 그룹 WidgetID를 쓴다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tutorial|World Guide")
	TObjectPtr<UOutlierWorldGuideTargetComponent> WorldGuideTarget;

	virtual bool Interact(AFirstPersonCharacter* Interactor) override;
	virtual bool IsProgressCompleted() const override { return bEffectCompleted; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual bool ActivateTarget() override;

private:
	void HandlePanelCompleted(AActor* Source, AOutlierPlayerState* SourcePlayerState);
	TWeakObjectPtr<AOutlierPlayerState> ActivatingPlayer;
	TWeakObjectPtr<APanelMover> BoundPanel;
	bool bEffectCompleted = false;
};
