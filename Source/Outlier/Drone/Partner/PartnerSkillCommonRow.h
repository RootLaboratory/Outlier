#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "PartnerSkillCommonRow.generated.h"

USTRUCT(BlueprintType)
struct OUTLIER_API FPartnerSkillCommonRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float CoolDown = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Duration = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float CastTime = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	uint8 bRequireLineOfSight : 1;
};
