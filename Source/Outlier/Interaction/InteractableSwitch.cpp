#include "Interaction/InteractableSwitch.h"

#include "Interaction/InteractableDoor.h"

bool AInteractableSwitch::ActivateTarget()
{
	if (!TargetDoor)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Switch] TargetDoor is null Actor=%s"), *GetName());
		return false;
	}

	const bool bRequestedOpen = bCanToggleDoor ? !TargetDoor->IsDoorOpen() : true;
	if (!TargetDoor->TrySetDoorOpen(bRequestedOpen))
	{
		// 문이 거절한 요청은 스위치 활성화/저장/성공 연출로 이어지지 않는다.
		// 호출자의 기존 실패 경로가 Hold 상태를 정리하므로 별도 안내는 추가하지 않는다.
		return false;
	}
	return true;
}
