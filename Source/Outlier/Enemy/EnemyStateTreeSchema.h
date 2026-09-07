#pragma once

#include "Components/StateTreeComponentSchema.h"
#include "EnemyStateTreeSchema.generated.h"

UCLASS(
	BlueprintType,
	EditInlineNew,
	CollapseCategories,
	meta = (DisplayName = "Enemy StateTree", CommonSchema)
)
class OUTLIER_API UEnemyStateTreeSchema : public UStateTreeComponentSchema
{
	GENERATED_BODY()

public:
	UEnemyStateTreeSchema();

	virtual bool IsStructAllowed(const UScriptStruct* InScriptStruct) const override;
};
