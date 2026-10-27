#pragma once

#include "CoreMinimal.h"
#include "Interaction/LaserControlSwitch.h"
#include "Tutorial/Trigger/Tuto_ProgressSourceInterface.h"
#include "UObject/ObjectKey.h"
#include "Tuto_LaserControlSwitch.generated.h"

class UOutlierWorldGuideTargetComponent;

/** 연결된 모든 레이저의 완료 알림을 모아 자기 WorldGuide와 Observer 진행 하나를 완료한다. */
UCLASS()
class OUTLIER_API ATuto_LaserControlSwitch : public ALaserControlSwitch, public ITuto_ProgressSourceInterface
{
	GENERATED_BODY()

public:
	ATuto_LaserControlSwitch();

	/** 이 상속 컴포넌트에서 WidgetID, WidgetClass, 마커 anchor를 설정한다. 4곳 모두 같은 그룹 WidgetID를 쓴다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tutorial|World Guide")
	TObjectPtr<UOutlierWorldGuideTargetComponent> WorldGuideTarget;

	virtual bool Interact(AFirstPersonCharacter* Interactor) override;
	virtual bool IsProgressCompleted() const override { return bEffectCompleted; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual bool ActivateTarget() override;
	virtual bool DeactivateLaserFloor(ALaserKillFloor* LaserFloor) override;

private:
	void HandleLaserCompleted(AActor* Source, AOutlierPlayerState* SourcePlayerState);
	void TryCompleteEffects(AOutlierPlayerState* SourcePlayerState);
	TArray<TWeakObjectPtr<ALaserKillFloor>> BoundLasers;
	TSet<FObjectKey> RequiredLasers;
	TSet<FObjectKey> CompletedLasers;
	TWeakObjectPtr<AOutlierPlayerState> ActivatingPlayer;
	bool bValidTargets = false;
	bool bEffectCompleted = false;
};
