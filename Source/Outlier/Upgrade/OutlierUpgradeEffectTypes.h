#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "OutlierUpgradeEffectTypes.generated.h"

UENUM(BlueprintType)
enum class EOutlierUpgradeEffectType : uint8
{
	Attribute = 0,
	AbilityConfig = 1,
	GrantAbility = 2,
	FunctionOverride = 3
};

UENUM(BlueprintType)
enum class EOutlierUpgradeModOp : uint8
{
	Additive,       
	Multiplicative, 
	Override        
};

USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierUpgradeEffectRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade|Effect")
	FName NodeRowName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade|Effect")
	EOutlierUpgradeEffectType EffectType = EOutlierUpgradeEffectType::Attribute;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade|Effect")
	FGameplayTag TargetTag;

	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade|Effect")
	FName ConfigField = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade|Effect")
	EOutlierUpgradeModOp Op = EOutlierUpgradeModOp::Additive;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade|Effect")
	float Magnitude = 0.0f;
};
