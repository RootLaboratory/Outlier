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
struct OUTLIER_API FEnemyDamageInstance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Amount = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Count = 1;
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
	TArray<FEnemyDamageInstance> DamagePattern;

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
};
