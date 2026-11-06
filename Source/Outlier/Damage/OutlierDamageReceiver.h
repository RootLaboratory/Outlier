#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "OutlierDamageReceiver.generated.h"

class AActor;
class AController;
class AShooterCharacter;

// 한 은신에서 시작한 공격들이 공유하는 서버 전용 자격이다.
// 공격 때문에 은신이 먼저 끝나도 막타까지 살아 있고, 첫 처치 보상은 한 번만 소비된다.
struct FOutlierStealthSession
{
	TWeakObjectPtr<AShooterCharacter> Shooter;
	uint32 Generation = 0;
	bool bAssassinsStrikeConsumed = false;
};

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
	// 서버가 생성한 폭발 요청에서만 지정한다. 대상의 무적/피해 수신 검사는 그대로 거친다.
	bool bAllowFriendlyFire = false;
	AController* EventInstigator = nullptr;
	AActor* DamageCauser = nullptr;
	TSharedPtr<FOutlierStealthSession> StealthSession;
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
	// 고정 피해량이 아니라 현재 생명력 전체를 소진하는 환경 피해다. 기존 면역 검사는 유지한다.
	OUTLIER_API FGameplayTag InstantKillTag();
	// Tester와 임시 F1 디버그가 공유하는 서버 페어 무적이다. Shipping에서도 사용한다.
	OUTLIER_API bool IsEditorInvulnerable(const AActor* TargetActor);
	OUTLIER_API bool IsFromEnemy(const FOutlierDamageRequest& Request);
	OUTLIER_API float Apply(AActor* TargetActor, const FOutlierDamageRequest& Request);
}
