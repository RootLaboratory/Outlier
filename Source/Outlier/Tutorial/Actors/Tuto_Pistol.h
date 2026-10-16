#pragma once

#include "CoreMinimal.h"
#include "Tutorial/Trigger/Tuto_EventTriggerInterface.h"
#include "Weapon/RangedWeaponBase.h"
#include "Tuto_Pistol.generated.h"

class UOutlierWorldGuideTargetComponent;

UCLASS()
class OUTLIER_API ATuto_Pistol : public ARangedWeaponBase, public ITuto_EventTriggerInterface
{
	GENERATED_BODY()

public:
	ATuto_Pistol();

	/** 이 상속 컴포넌트에서 WidgetID, WidgetClass, 마커 anchor를 설정한다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tutorial|World Guide")
	TObjectPtr<UOutlierWorldGuideTargetComponent> WorldGuideTarget;
	virtual bool Interact(AFirstPersonCharacter* Interactor) override;
	virtual TSubclassOf<AWeaponBase> GetGrantedWeaponClass() const override;
	virtual FGameplayTag GetEventTag(int32 EventIndex) const override
	{
		return EventTriggers.IsValidIndex(EventIndex) ? EventTriggers[EventIndex].EventTag : FGameplayTag();
	}

protected:
	/** 서버: 현재 로드아웃이 이미 GrantedWeaponClass를 소유하면 배치된 픽업은 스스로 제거된다. */
	virtual void BeginPlay() override;

	/** Slot 0: 픽업 성공. 추가 상황은 C++ 서브클래스에서 다른 slot을 선택할 수 있다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Event", meta = (TitleProperty = "EventTag"))
	TArray<FTuto_EventTriggerData> EventTriggers;

	/** 실제로 지급되어 로드아웃에 기록되는 권총(예: BP_Pistol). 소유 무기가 튜토리얼 트리거를 갖지 않도록
	 *  Tuto 클래스는 거부한다. 미설정 시 픽업은 실패한다. */
	UPROPERTY(EditDefaultsOnly, Category = "Tutorial|Weapon")
	TSubclassOf<ARangedWeaponBase> GrantedWeaponClass;

	virtual void HandleWeaponCollected(AOutlierPlayerState* SourcePlayerState);
};
