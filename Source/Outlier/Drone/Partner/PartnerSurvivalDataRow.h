#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "PartnerSurvivalDataRow.generated.h"

USTRUCT(BlueprintType)
struct OUTLIER_API FPartnerSurvivalDataRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 MaxHitCount = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float RebootTime = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float InvincibleAfterRebootTime = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float HitInvincibleTime = 0.25f;
};
