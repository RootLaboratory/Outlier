#include "Tutorial/Actors/Tuto_Pistol.h"

#include "FirstPerson/FirstPersonCharacter.h"
#include "OutlierPlayerState.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideTargetComponent.h"

ATuto_Pistol::ATuto_Pistol()
{
	WeaponType = EWeaponType::Pistol;
	EventTriggers.SetNum(1);
	WorldGuideTarget = CreateDefaultSubobject<UOutlierWorldGuideTargetComponent>(TEXT("WorldGuideTarget"));
	WorldGuideTarget->SetupAttachment(GetRootComponent());
}

bool ATuto_Pistol::Interact(AFirstPersonCharacter* Interactor)
{
	if (!Super::Interact(Interactor)) return false;
	HandleWeaponCollected(Interactor->GetPlayerState<AOutlierPlayerState>());
	return true;
}

void ATuto_Pistol::HandleWeaponCollected(AOutlierPlayerState* SourcePlayerState)
{
	UOutlierWorldGuideTargetComponent::CompleteOnActor(this);
	TriggerEvent(0, SourcePlayerState);
}
