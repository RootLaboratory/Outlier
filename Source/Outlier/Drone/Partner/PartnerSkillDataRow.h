#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "PartnerSkillDataRow.generated.h"

USTRUCT(BlueprintType)
struct OUTLIER_API FPartnerSkillDataRow : public FTableRowBase
{
	GENERATED_BODY()

	// Scan
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float ScanRange = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float ScanDuration = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float ScanCooldown = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float ScanExpandSpeed = 5.0f;

	// Hack
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float HackEffectiveRange = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float HackMiniGameTime = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float HackCooldown = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float HackFailPenaltyTime = 3.0f;

	// EMP (AreaOfEffect)
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float AreaOfEffectRange = 1500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float EMPMarkingTime = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float EMPStunDuration = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float AreaOfEffectCooldown = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 EMPMaxTargets = 99;

	// Shield
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float ShieldRange = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float ShieldDuration = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float ShieldCooldown = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float ShieldAmount = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float ShieldDecayRate = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float ShieldDecayDelay = 1.0f;

	// Interaction
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float InteractionRange = 200.0f;
};
