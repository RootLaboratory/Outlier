#include "Damage/OutlierDamageReceiver.h"

#include "Damage/OutlierTaggedDamageEvent.h"
#include "GameFramework/Actor.h"

FOutlierDamageRequest FOutlierDamageRequest::FromDamageEvent(
	float DamageAmount,
	const FDamageEvent& DamageEvent,
	AController* EventInstigator,
	AActor* DamageCauser)
{
	FOutlierDamageRequest Request;
	Request.DamageAmount = DamageAmount;
	Request.EventInstigator = EventInstigator;
	Request.DamageCauser = DamageCauser;

	if (DamageEvent.IsOfType(FOutlierTaggedDamageEvent::ClassID))
	{
		const FOutlierTaggedDamageEvent& TaggedEvent =
			static_cast<const FOutlierTaggedDamageEvent&>(DamageEvent);
		Request.DamageTag = TaggedEvent.DamageTag;
		Request.HitResult = TaggedEvent.HitResult;
		Request.DamageOrigin = TaggedEvent.DamageOrigin;
		Request.bReflectedDamage = TaggedEvent.bReflectedDamage;
	}

	return Request;
}

float OutlierDamage::Apply(AActor* TargetActor, const FOutlierDamageRequest& Request)
{
	if (!IsValid(TargetActor) || Request.DamageAmount <= 0.0f)
	{
		return 0.0f;
	}

	if (IOutlierDamageReceiver* Receiver = Cast<IOutlierDamageReceiver>(TargetActor))
	{
		return Receiver->ReceiveOutlierDamage(Request);
	}

	// Non-Outlier Blueprint actors retain the engine damage contract through one
	// centralized compatibility boundary. GAS combatants never use this fallback.
	FOutlierTaggedDamageEvent DamageEvent;
	DamageEvent.DamageTag = Request.DamageTag;
	DamageEvent.HitResult = Request.HitResult;
	DamageEvent.DamageOrigin = Request.DamageOrigin;
	DamageEvent.bReflectedDamage = Request.bReflectedDamage;
	return TargetActor->TakeDamage(
		Request.DamageAmount,
		DamageEvent,
		Request.EventInstigator,
		Request.DamageCauser);
}
