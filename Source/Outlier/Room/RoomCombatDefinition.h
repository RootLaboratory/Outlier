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

	// 비어 있으면 활성 SpawnPoint 전체를 사용하고, 지정하면 해당 Tag 계층에 속한 지점만 사용한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room Combat", meta = (Categories = "RoomCombat.Spawn"))
	FGameplayTag RequiredSpawnPointTag;

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

USTRUCT(BlueprintType)
struct OUTLIER_API FRoomCombatRoomDefinition
{
	GENERATED_BODY()

	bool HasValidPhaseOrder() const;
	bool CanStartTriggeredSequence(int32 PhaseIndex) const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room Combat", meta = (Categories = "Room"))
	FGameplayTag RoomTag;

	// 전투 차수와 Wave 번호는 별도 ID 없이 각 배열의 순서를 그대로 사용한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room Combat", meta = (TitleProperty = "StartPolicy"))
	TArray<FRoomCombatPhaseDefinition> CombatPhases;
};

UCLASS(BlueprintType)
class OUTLIER_API URoomCombatDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	const FRoomCombatRoomDefinition* FindRoomDefinition(FGameplayTag RoomTag) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

	// 모든 Room의 전투 구성을 한 Asset에서 편집하고, 월드 Actor는 RoomTag로 항목을 선택한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room Combat", meta = (TitleProperty = "RoomTag"))
	TArray<FRoomCombatRoomDefinition> RoomDefinitions;
};
