#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "OutlierDamageReceiver.generated.h"

class AActor;
class AController;

struct OUTLIER_API FOutlierDamageRequest
{
	float DamageAmount = 0.0f;
	FGameplayTag DamageTag;
	float StunDurationSeconds = 0.0f;
	FHitResult HitResult;
	FVector DamageOrigin = FVector::ZeroVector;
	bool bReflectedDamage = false;
	AController* EventInstigator = nullptr;
	AActor* DamageCauser = nullptr;
};

UINTERFACE(MinimalAPI)
class UOutlierDamageReceiver : public UInterface
{
	GENERATED_BODY()
};

class OUTLIER_API IOutlierDamageReceiver
{
	GENERATED_BODY()

public:
	virtual float ReceiveOutlierDamage(const FOutlierDamageRequest& Request) = 0;
};

namespace OutlierDamage
{
	OUTLIER_API float Apply(AActor* TargetActor, const FOutlierDamageRequest& Request);
}
