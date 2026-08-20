#pragma once

#include "CoreMinimal.h"
#include "Ability/OutlierAbilityComponent.h"
#include "ShooterTestingAbilityComponent.generated.h"

UCLASS(ClassGroup = (Shooter), meta = (BlueprintSpawnableComponent))
class OUTLIER_API UShooterTestingAbilityComponent : public UOutlierAbilityComponent
{
	GENERATED_BODY()

public:
	UShooterTestingAbilityComponent();

	virtual void BeginPlay() override;

	UFUNCTION(BlueprintCallable, Category = "Ability|Shooter")
	void RefreshCachedShooterAbilityData();

protected:
	virtual void InitializeAbilityHandlers() override;

	EOutlierAbilityResult ExecuteStealth(const FOutlierAbilityRow& AbilityRow);

	void CacheShooterAbilityData(float StealthCooldownSeconds);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability|Shooter", meta = (ClampMin = "0.0"))
	float DefaultStealthCooldownSeconds = 5.0f;
};
