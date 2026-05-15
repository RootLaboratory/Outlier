#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "PartnerSkillDataRow.generated.h"

USTRUCT(BlueprintType)
struct OUTLIER_API FPartnerSkillDataRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float ScanRange = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float ScanDuration = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float ScanCooldown = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float HackRange = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float HackDuration = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float HackCooldown = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float AreaOfEffectRange = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float AreaOfEffectDuration = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float AreaOfEffectCooldown = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float ShieldRange = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float ShieldDuration = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float ShieldCooldown = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float ShieldAmount = 200.0f;
};
