#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "EnemyStat.generated.h"

UENUM(BlueprintType)
enum class EEnemyType : uint8
{
	Gun,
	Melee,
	Mortar,
	Sniper,
	Turret
};

USTRUCT(BlueprintType)
struct OUTLIER_API FEnemyStat : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 ID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EEnemyType Type = EEnemyType::Gun;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Health = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MoveSpeed = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float SightRadius = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float LoseSightRadius = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float PeripheralVisionAngle = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float BattlePeripheralVisionAngle = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float HearingRange = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float BattleHearingRange = 0.0f;

	// 부착 폭발물 약점이 없는 Enemy는 0을 사용한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosion", meta = (ClampMin = "0.0"))
	float ExplosiveWeakPointMultiplier = 0.0f;
};

// 이동형 Enemy, 자폭 드론, 고정형 터렛이 공유하는 외부 충격 반응 설정이다.
// EnemyStat과 수명 주기가 다르므로 별도 DataTable Row로 관리한다.
USTRUCT(BlueprintType)
struct OUTLIER_API FEnemyImpactReactionProfileRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0.0"))
	float MinReactionStrength = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0.0"))
	float MaxAccumulatedStrength = 1600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ResistanceRatio = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0.0"))
	float InitialInertiaHoldDuration = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0.0"))
	float KnockbackDamping = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0.0"))
	float MinPhysicalKnockbackDuration = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0.0"))
	float MaxPhysicalKnockbackDuration = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0.0"))
	float MinControlRecoveryDuration = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0.0"))
	float MaxControlRecoveryDuration = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0.0"))
	float RecoverySpeedThreshold = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0.0"))
	float RecoveryFacingInterpSpeed = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0.0"))
	float MaxMeshImpactTiltDegrees = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0.0"))
	float MeshImpactTiltInterpSpeed = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float MaxChargeImpactTurnAngleDegrees = 75.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float MaxChargePitchDegrees = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0.0"))
	float ChargeImpactTurnInterpSpeed = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0.0"))
	float CameraShakeScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0.0"))
	float InputLockDuration = 0.25f;
};
