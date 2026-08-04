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
	float MinDamage = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosion|Damage", meta = (ClampMin = "0.0"))
	float InnerRadiusCm = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosion|Damage", meta = (ClampMin = "0.0"))
	float OuterRadiusCm = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosion|Damage", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float OccludedMultiplier = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosion|Reaction", meta = (ClampMin = "0.0"))
	float EnemyImpulseScale = 500.0f;

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
