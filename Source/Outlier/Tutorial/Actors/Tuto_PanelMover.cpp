#include "Tutorial/Actors/Tuto_PanelMover.h"

void ATuto_PanelMover::FinishMove()
{
	Super::FinishMove();
	if (!HasAuthority() || bMoveCompleted) return;
	bMoveCompleted = true;
	CompleteProgress(GetProgressInstigator());
}
