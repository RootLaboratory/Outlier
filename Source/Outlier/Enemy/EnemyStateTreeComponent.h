#pragma once

#include "Components/StateTreeComponent.h"
#include "EnemyStateTreeComponent.generated.h"

// Enemy 전용 Schema를 제공해 공용/Linked StateTree 에셋 선택과 런타임 검증 기준을 일치시킨다.
UCLASS(ClassGroup = AI)
class OUTLIER_API UEnemyStateTreeComponent : public UStateTreeComponent
{
	GENERATED_BODY()

public:
	virtual TSubclassOf<UStateTreeSchema> GetSchema() const override;
};
