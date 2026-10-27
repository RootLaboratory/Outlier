#pragma once

#include "CoreMinimal.h"
#include "Tutorial/Actors/Tuto_InteractionDoorSwitch.h"
#include "Tuto_HackDoorSwitch.generated.h"

/**
 * 해킹 잠금이 걸린 Tuto 문 스위치. Slot 0(첫 상호작용 성공)은 Tuto_InteractionDoorSwitch와 같고,
 * Slot 1에 해킹 성공(잠금 해제)을 추가한다. 사운드 등 연출은 이벤트 DT 쪽에서 처리한다.
 * 기본값으로 State.Locked / Hack.Target.NonPossessable / Hack.Effect.Unblock을 넣고, 미니게임 태그는 BP에서 정한다.
 */
UCLASS()
class OUTLIER_API ATuto_HackDoorSwitch : public ATuto_InteractionDoorSwitch
{
	GENERATED_BODY()

public:
	ATuto_HackDoorSwitch();

	virtual void HandleHackEffect(FGameplayTag EffectTag, const FHackResultContext& Context) override;
};
