#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "OutlierDamageReceiver.generated.h"

class AActor;
class AController;

// Enemy 내성 시스템이 피해 감소와 최종 처치 결과를 같은 기준으로 판정할 수 있도록
// 공격을 만든 지점에서 확정해 전달한다. 기본값은 시스템/환경 피해가 Stack에 관여하지 않는 Ignore다.
enum class EOutlierAdaptationDamageCategory : uint8
{
	Ignore,
	Gun,
	NonGun,
	Pistol
};

struct OUTLIER_API FOutlierDamageRequest
{
	float DamageAmount = 0.0f;
	FGameplayTag DamageTag;
	EOutlierAdaptationDamageCategory AdaptationDamageCategory =
		EOutlierAdaptationDamageCategory::Ignore;
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
	OUTLIER_API bool IsFromEnemy(const FOutlierDamageRequest& Request);
	OUTLIER_API float Apply(AActor* TargetActor, const FOutlierDamageRequest& Request);
}
