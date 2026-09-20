#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EnemyAdaptationDefinition.generated.h"

class UNiagaraSystem;

// 1P와 2P가 공유하는 총기 적응 Stack의 단계 기준과 단계별 효과를 설정한다.
UCLASS(BlueprintType)
class OUTLIER_API UEnemyAdaptationDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

	// 공유 Stack이 올라갈 수 있는 최대값. 모든 임계값은 이 값 이하여야 한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Adaptation|Stack", meta = (ClampMin = "1", UIMin = "1"))
	int32 MaxGunAdaptationStack = 10;

	// 내성 적용 대상 Enemy를 총기 또는 과부화로 처치했을 때 증가하는 Stack 수치.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Adaptation|Stack", meta = (ClampMin = "1", UIMin = "1"))
	int32 GunKillIncrement = 1;

	// 실제 내성 단계 진입 전, Non-Gun 처치 시 감소하는 Stack 수치.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Adaptation|Stack", meta = (ClampMin = "1", UIMin = "1"))
	int32 NonGunKillDecrement = 2;

	// 피해 감소 없이 방어막을 처음 표시해 내성 진입을 예고하는 Stack 기준.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Adaptation|Threshold", meta = (ClampMin = "1", UIMin = "1"))
	int32 ShieldPreviewThreshold = 7;

	// 내성 1단계의 총기 피해 감소와 약한 파열을 적용하기 시작하는 Stack 기준.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Adaptation|Threshold", meta = (ClampMin = "1", UIMin = "1"))
	int32 ResistanceLevel1Threshold = 8;

	// 내성 2단계의 총기 피해 감소와 중간 파열을 적용하기 시작하는 Stack 기준.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Adaptation|Threshold", meta = (ClampMin = "1", UIMin = "1"))
	int32 ResistanceLevel2Threshold = 9;

	// 내성 MAX 단계의 총기 피해 감소와 강한 파열을 적용하는 Stack 기준.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Adaptation|Threshold", meta = (ClampMin = "1", UIMin = "1"))
	int32 ResistanceMaxThreshold = 10;

	// 내성 1단계에서 최종 총기 피해에 곱하는 값. Non-Gun 피해에는 적용하지 않는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Adaptation|Damage", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ResistanceLevel1GunDamageMultiplier = 0.9f;

	// 내성 2단계에서 최종 총기 피해에 곱하는 값. Non-Gun 피해에는 적용하지 않는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Adaptation|Damage", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ResistanceLevel2GunDamageMultiplier = 0.8f;

	// 내성 MAX 단계에서 최종 총기 피해에 곱하는 값. Non-Gun 피해에는 적용하지 않는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Adaptation|Damage", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ResistanceMaxGunDamageMultiplier = 0.5f;

	// 내성 1단계를 Non-Gun 처치로 파괴했을 때 현재 전투 중 Enemy에게 적용하는 경직 시간.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Adaptation|Break", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float ResistanceLevel1BreakStunSeconds = 0.5f;

	// 내성 2단계를 Non-Gun 처치로 파괴했을 때 현재 전투 중 Enemy에게 적용하는 경직 시간.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Adaptation|Break", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float ResistanceLevel2BreakStunSeconds = 0.75f;

	// 내성 MAX 단계를 Non-Gun 처치로 파괴했을 때 현재 전투 중 Enemy에게 적용하는 경직 시간.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Adaptation|Break", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float ResistanceMaxBreakStunSeconds = 1.0f;

	// 내성 파괴 시 적용 대상 Enemy의 방어막 위치에서 재생할 공통 Niagara 에셋.
	// 단계별 표현 차이는 별도 강도 파라미터로 조절하며 에셋은 하나만 사용한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Adaptation|Break")
	TSoftObjectPtr<UNiagaraSystem> ShieldBreakVFX;
};
