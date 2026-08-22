// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Shooter/ShooterCharacterComponentBase.h"
#include "ShooterHealthComponent.generated.h"

class AController;
struct FGameplayTag;

UCLASS(ClassGroup=(Shooter), meta=(BlueprintSpawnableComponent))
class OUTLIER_API UShooterHealthComponent : public UShooterCharacterComponentBase
{
	GENERATED_BODY()

public:
	UShooterHealthComponent();

	bool ApplyDamage(
		float DamageAmount,
		AController* Instigator,
		AActor* DamageCauser,
		const FGameplayTag& DamageTag);
	void Die();
	void GetHit();
	void HitHistoryRefresh();
	void DelayShieldRecovery(float DelaySeconds);

private:
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

public:
	UPROPERTY(BlueprintReadOnly, Category =  "ShieldRecovery")
	float HitInterval = 3;

	UPROPERTY(BlueprintReadOnly, Category = "ShieldRecovery")
	float ShieldRecoveryInterval = 1;

	UPROPERTY(BlueprintReadOnly, Category = "ShieldRecovery")
	float ShieldRecoveryValue = 20;

private:
	uint8 bShieldRecoveryAbled : 1 = true;
	float ShieldRecoveryDelayRemaining = 0.f;
	float RecoveryAccumulated = 0.f;
};
