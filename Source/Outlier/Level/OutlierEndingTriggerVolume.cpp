#include "Level/OutlierEndingTriggerVolume.h"

#include "Engine/World.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "OutlierPlayerState.h"
#include "UI/OutlierEndingWidget.h"

bool AOutlierEndingTriggerVolume::HandlePawnEntered(AOutlierPlayerState* SourcePlayerState)
{
	if (!HasAuthority() || !IsValid(SourcePlayerState) || !EndingWidgetClass)
	{
		return false;
	}

	TArray<AFirstPersonPlayerController*> Controllers;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		AFirstPersonPlayerController* Controller = Cast<AFirstPersonPlayerController>(It->Get());
		const AOutlierPlayerState* State = Controller ? Controller->GetPlayerState<AOutlierPlayerState>() : nullptr;
		if (State && State->GetPairId() == SourcePlayerState->GetPairId())
		{
			Controllers.Add(Controller);
		}
	}
	// 상대 플레이어가 참가 중이면 진입을 소모하지 않는다. Standalone에서는 단독 미리보기를 지원한다.
	if (Controllers.Num() < (GetNetMode() == NM_Standalone ? 1 : 2))
	{
		return false;
	}

	for (AFirstPersonPlayerController* Controller : Controllers)
	{
		if (Controller->IsLocalController()) Controller->ClientShowEnding_Implementation(EndingWidgetClass);
		else Controller->ClientShowEnding(EndingWidgetClass);
	}
	return true;
}
