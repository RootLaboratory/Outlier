#include "Tutorial/Actors/Tuto_LaserKillFloor.h"

ATuto_LaserKillFloor::ATuto_LaserKillFloor()
{
	// 기본 스위치는 Destroy로 지우지만 Tuto는 액터를 유지하므로, 꺼지면 레이저 표시도 숨긴다.
	bVisibleOnlyWhenEnabled = true;
}

void ATuto_LaserKillFloor::SetHazardEnabled(bool bEnabled)
{
	const bool bWasEnabled = IsHazardEnabled();
	Super::SetHazardEnabled(bEnabled);
	if (HasAuthority() && bWasEnabled && !IsHazardEnabled())
		CompleteProgress(GetProgressInstigator());
}
