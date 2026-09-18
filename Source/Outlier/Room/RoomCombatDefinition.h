#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "RoomCombatDefinition.generated.h"

class AEnemyBase;

UENUM(BlueprintType)
enum class ERoomCombatPhaseStartPolicy : uint8
{
	InitialDetection UMETA(DisplayName = "Initial Detection"),
	HackTrigger UMETA(DisplayName = "Hack Trigger"),
	Automatic UMETA(DisplayName = "Automatic")
};

UENUM(BlueprintType)
enum class ERoomCombatWaveSpawnMode : uint8
{
	Preplaced UMETA(DisplayName = "Preplaced"),
	SpawnFromObjects UMETA(DisplayName = "Spawn From Objects")
};

USTRUCT(BlueprintType)
struct OUTLIER_API FRoomCombatEnemyEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room Combat")
	TSoftClassPtr<AEnemyBase> EnemyClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room Combat", meta = (ClampMin = "1", UIMin = "1"))
	int32 Count = 1;
};

USTRUCT(BlueprintType)
struct OUTLIER_API FRoomCombatWaveDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room Combat")
	ERoomCombatWaveSpawnMode SpawnMode = ERoomCombatWaveSpawnMode::Preplaced;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room Combat", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float NextWaveRemainingRatio = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room Combat")
	FGameplayTagQuery SpawnPointQuery;

	// Preplaced Wave는 비워둘 수 있고, SpawnFromObjects는 여기의 고정 구성을 나눠서 소환한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room Combat", meta = (TitleProperty = "EnemyClass"))
	TArray<FRoomCombatEnemyEntry> Enemies;
};

USTRUCT(BlueprintType)
struct OUTLIER_API FRoomCombatPhaseDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room Combat")
	ERoomCombatPhaseStartPolicy StartPolicy = ERoomCombatPhaseStartPolicy::InitialDetection;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room Combat", meta = (TitleProperty = "SpawnMode"))
	TArray<FRoomCombatWaveDefinition> Waves;
};

UCLASS(BlueprintType)
class OUTLIER_API URoomCombatDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	// 에디터 검증과 서버 시작 검증에서 같은 차수 순서 규칙을 사용한다.
	bool HasValidPhaseOrder() const;
	bool CanStartTriggeredSequence(int32 PhaseIndex) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

	// 전투 차수와 Wave 번호는 별도 ID 없이 각 배열의 순서를 그대로 사용한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room Combat", meta = (TitleProperty = "StartPolicy"))
	TArray<FRoomCombatPhaseDefinition> CombatPhases;
};
