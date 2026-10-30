#pragma once

#include "CoreMinimal.h"
#include "Weapon/WeaponFeedbackDefinition.h"
#include "LaserWeaponFeedbackDefinition.generated.h"

// 시간과 피해는 Task/무기가 소유하고 이 DA는 연출 설정만 공유한다.
UCLASS(BlueprintType)
class OUTLIER_API ULaserWeaponFeedbackDefinition : public UWeaponFeedbackDefinition
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Laser|Telegraph")
	TObjectPtr<UTrailEffectDefinition> ChargeEffect;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Laser|Fire")
	TObjectPtr<UTrailEffectDefinition> ImpactEffect;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Laser|Parameters")
	FName WidthParameterName = TEXT("User.Width");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Laser|Parameters")
	FName ProgressParameterName = TEXT("User.ChargeProgress");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Laser|Fire", meta = (ClampMin = "0.0"))
	float FireWidth = 5.0f;
};
