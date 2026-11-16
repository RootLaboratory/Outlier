#pragma once

#include "CoreMinimal.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/StaticMeshComponent.h"

// 몸체 기하/표시 판정만 공유한다. 후보 수집, 명시적 몸체 설정과 차폐 정책은 호출부가 맡는다.
namespace ActorBodyTargeting
{
	struct FBodyHit
	{
		FVector Location = FVector::ZeroVector;
		float Time = 0.0f;
		bool bPrecise = false;
	};

	inline bool IsVisibleBody(const UPrimitiveComponent* Body)
	{
		static const FName IgnoreTag(TEXT("HackCursorIgnore"));
		static const FName StencilExcludeTag(TEXT("Outlier.StencilExclude"));
		return IsValid(Body) && Body->IsRegistered() && Body->IsVisible() && !Body->bHiddenInGame
			&& !Body->bOnlyOwnerSee && Body->bRenderInMainPass
			&& !Body->ComponentHasTag(IgnoreTag) && !Body->ComponentHasTag(StencilExcludeTag);
	}

	inline bool IsVisibleMesh(const UPrimitiveComponent* Body)
	{
		return IsVisibleBody(Body)
			&& (Body->IsA<UStaticMeshComponent>() || Body->IsA<USkinnedMeshComponent>());
	}

	inline bool IntersectBody(UPrimitiveComponent& Body, const FVector& Start, const FVector& End,
		const FCollisionQueryParams& Params, FBodyHit& OutHit)
	{
		FHitResult Hit;
		OutHit.bPrecise = Body.LineTraceComponent(Hit, Start, End, Params);
		if (OutHit.bPrecise)
		{
			OutHit.Location = Hit.Location;
			OutHit.Time = Hit.Time;
			return true;
		}
		// 채널/충돌 활성화 설정을 바꾸지 않는다. 직접 충돌 판정이 실패하면 월드 Bounds로 보조한다.
		FVector Normal;
		return FMath::LineExtentBoxIntersection(Body.Bounds.GetBox(), Start, End, FVector::ZeroVector,
			OutHit.Location, Normal, OutHit.Time);
	}

	inline bool IsBetterCandidate(bool bPrecise, double Distance,
		bool bBestPrecise, double BestDistance, int32 TieBreak = 0)
	{
		// 정밀 Hit가 항상 우선한다. 같은 종류에서는 거리, 동률에서는 호출부의 정책을 따른다.
		if (bPrecise != bBestPrecise)
		{
			return bPrecise;
		}
		return Distance < BestDistance || (Distance == BestDistance && TieBreak < 0);
	}
}
