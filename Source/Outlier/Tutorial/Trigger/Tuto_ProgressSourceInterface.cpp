#include "Tutorial/Trigger/Tuto_ProgressSourceInterface.h"

#include "GameFramework/Actor.h"
#include "OutlierPlayerState.h"

void ITuto_ProgressSourceInterface::CompleteProgress(AOutlierPlayerState* SourcePlayerState)
{
	AActor* Actor = Cast<AActor>(_getUObject());
	if (!IsValid(Actor) || !Actor->HasAuthority()) return;
	OnProgressCompleted.Broadcast(Actor, SourcePlayerState);
}

AOutlierPlayerState* ITuto_ProgressSourceInterface::GetProgressInstigator() const
{
	return ProgressInstigator.Get();
}

void ITuto_ProgressSourceInterface::SetProgressInstigator(AOutlierPlayerState* PlayerState)
{
	ProgressInstigator = PlayerState;
}
