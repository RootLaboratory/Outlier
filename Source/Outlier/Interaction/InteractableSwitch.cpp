#include "Interaction/InteractableSwitch.h"

#include "Interaction/InteractableDoor.h"

bool AInteractableSwitch::ActivateTarget()
{
	if (!TargetDoor)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Switch] TargetDoor is null Actor=%s"), *GetName());
		return false;
	}

	if (bCanToggleDoor)
	{
		TargetDoor->ToggleDoor();
	}
	else
	{
		TargetDoor->SetDoorOpen(true);
	}
	return true;
}
