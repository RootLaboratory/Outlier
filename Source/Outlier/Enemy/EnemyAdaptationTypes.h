#pragma once

#include "CoreMinimal.h"
#include "EnemyAdaptationTypes.generated.h"

UENUM(BlueprintType)
enum class EEnemyAdaptationState : uint8
{
	Normal,
	ShieldPreview,
	ResistanceLevel1,
	ResistanceLevel2,
	ResistanceMax
};

UENUM(BlueprintType)
enum class EEnemyFinalKillCategory : uint8
{
	Gun,
	NonGun,
	Ignore
};

// 한 건의 최종 처치가 공용 Stack에 반영된 결과다. 후속 Slice의 방어막 갱신과
// 내성 파괴 VFX/전투 필드 경직은 이 결과만 받아 같은 판정을 공유한다.
USTRUCT(BlueprintType)
struct OUTLIER_API FEnemyAdaptationUpdateResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	EEnemyFinalKillCategory KillCategory = EEnemyFinalKillCategory::Ignore;

	UPROPERTY(BlueprintReadOnly)
	int32 PreviousStack = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 CurrentStack = 0;

	UPROPERTY(BlueprintReadOnly)
	EEnemyAdaptationState PreviousState = EEnemyAdaptationState::Normal;

	UPROPERTY(BlueprintReadOnly)
	EEnemyAdaptationState CurrentState = EEnemyAdaptationState::Normal;

	UPROPERTY(BlueprintReadOnly)
	bool bAdaptationBroken = false;

	UPROPERTY(BlueprintReadOnly)
	float BreakStunSeconds = 0.0f;
};
