#pragma once

#include "CoreMinimal.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"

// 기존 TakeDamage 경로를 유지하면서 피해 출처 Tag와 실제 피격 정보를 함께 전달한다.
// 폭발물은 이 Tag를 기준으로 허용된 피해만 HP에 반영한다.
struct OUTLIER_API FOutlierTaggedDamageEvent : public FDamageEvent
{
	// 기본 FDamageEvent와 구분하기 위한 고유 번호다.
	static const int32 ClassID = 31001;

	FGameplayTag DamageTag;
	FHitResult HitResult;
	FVector DamageOrigin = FVector::ZeroVector;

	// 전달받은 FDamageEvent가 이 사용자 정의 타입인지 안전하게 확인할 때 사용한다.
	virtual int32 GetTypeID() const override
	{
		return FOutlierTaggedDamageEvent::ClassID;
	}

	virtual bool IsOfType(int32 InID) const override
	{
		return InID == FOutlierTaggedDamageEvent::ClassID || FDamageEvent::IsOfType(InID);
	}

	// TakeDamage 내부에서 사용할 피격 위치와 피해가 들어온 방향을 반환한다.
	virtual void GetBestHitInfo(
		AActor const* HitActor,
		AActor const* HitInstigator,
		FHitResult& OutHitInfo,
		FVector& OutImpulseDir) const override
	{
		OutHitInfo = HitResult;
		if (!OutHitInfo.GetActor() && HitActor)
		{
			OutHitInfo.HitObjectHandle = FActorInstanceHandle(const_cast<AActor*>(HitActor));
			OutHitInfo.bBlockingHit = true;
			OutHitInfo.Location = HitActor->GetActorLocation();
			OutHitInfo.ImpactPoint = OutHitInfo.Location;
		}

		OutImpulseDir = HitActor
			? (HitActor->GetActorLocation() - DamageOrigin).GetSafeNormal()
			: FVector::ZeroVector;
	}
};
