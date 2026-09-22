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

// 최종 처치 또는 권총 명중이 공용 Stack에 반영된 결과다. 방어막 표현과 전투 필드 효과,
// 후속 파열 연출은 이 결과를 공유해 같은 Stack 전이를 기준으로 처리한다.
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

	UPROPERTY(BlueprintReadOnly)
	float BreakDamage = 0.0f;
};
