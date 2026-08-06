#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ExplosionTypes.generated.h"

USTRUCT(BlueprintType)
struct OUTLIER_API FExplosionProfileRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosion")
	FName ExplosionId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosion|Damage", meta = (ClampMin = "0.0"))
	float MaxDamage = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosion|Damage", meta = (ClampMin = "0.0"))
	float OuterRadiusCm = 1000.0f;

	// 1보다 크면 폭발 중심에서 멀어질수록 피해가 더 빠르게 감소한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosion|Damage", meta = (ClampMin = "0.01"))
	float DamageFalloffExponent = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosion|Damage", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float OccludedMultiplier = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosion|Reaction", meta = (ClampMin = "0.0"))
	float EnemyImpulseScale = 500.0f;

	// 피해 감쇠와 별도로 충격 속도의 거리별 감쇠 모양을 조절한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosion|Reaction", meta = (ClampMin = "0.01"))
	float ImpulseFalloffExponent = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosion|Reaction", meta = (ClampMin = "0.0"))
	float TurretReactionScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosion|Presentation", meta = (ClampMin = "0.0"))
	float CameraShakeScale = 1.0f;

};

USTRUCT(BlueprintType)
struct OUTLIER_API FExplosivePropRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Prop")
	FName ExplosivePropId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Prop", meta = (ClampMin = "0.0"))
	float MaxHP = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive Prop|Feedback", meta = (ClampMin = "0.0"))
	float HitFlashDuration = 0.08f;
};
