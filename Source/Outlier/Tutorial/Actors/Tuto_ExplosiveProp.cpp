#include "Tutorial/Actors/Tuto_ExplosiveProp.h"

#include "GameFramework/Controller.h"
#include "OutlierPlayerState.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideTargetComponent.h"

ATuto_ExplosiveProp::ATuto_ExplosiveProp()
{
	WorldGuideTarget = CreateDefaultSubobject<UOutlierWorldGuideTargetComponent>(TEXT("WorldGuideTarget"));
	WorldGuideTarget->SetupAttachment(GetRootComponent());
}

float ATuto_ExplosiveProp::ReceiveOutlierDamage(const FOutlierDamageRequest& Request)
{
	const TWeakObjectPtr<AController> PreviousInstigator = ProgressInstigator;
	if (HasAuthority() && !IsExploded() && !IsMountedOnSelfDestructDrone())
		ProgressInstigator = Request.EventInstigator;
	const float AppliedDamage = Super::ReceiveOutlierDamage(Request);
	if (AppliedDamage <= 0.0f) ProgressInstigator = PreviousInstigator;
	return AppliedDamage;
}

void ATuto_ExplosiveProp::HandleExplosionProcessed()
{
	const bool bAlreadyCompleted = IsExploded();
	Super::HandleExplosionProcessed();
	if (!HasAuthority() || IsMountedOnSelfDestructDrone() || bAlreadyCompleted || !IsExploded()) return;
	AController* Controller = ProgressInstigator.IsValid() ? ProgressInstigator.Get() : GetInstigatorController();
	UOutlierWorldGuideTargetComponent::CompleteOnActor(this);
	TriggerEvent(0, Controller ? Controller->GetPlayerState<AOutlierPlayerState>() : nullptr);
	ProgressInstigator.Reset();
}
