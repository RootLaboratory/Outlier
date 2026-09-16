#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MeleeTargetInterface.generated.h"

UENUM(BlueprintType)
enum class EMeleeHitResultType : uint8
{
	EnemyDamage,
	EnemyInstantKill,
	PartnerDamage
};

USTRUCT(BlueprintType)
struct OUTLIER_API FMeleeHitContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Melee")
	TObjectPtr<AActor> TargetActor = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Melee")
	FVector HitLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Melee")
	FVector HitNormal = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Melee")
	int32 AttackSequence = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Melee")
	EMeleeHitResultType ResultType = EMeleeHitResultType::EnemyDamage;
};

UINTERFACE(BlueprintType, Blueprintable)
class OUTLIER_API UMeleeTargetInterface : public UInterface
{
	GENERATED_BODY()
};

class OUTLIER_API IMeleeTargetInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Melee|Target")
	bool CanShowMeleeTargetIndicator(const AActor* InstigatorActor) const;
	virtual bool CanShowMeleeTargetIndicator_Implementation(const AActor* /*InstigatorActor*/) const
	{
		return false;
	}

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Melee|Target")
	void SetMeleeTargeted(AActor* InstigatorActor, bool bTargeted);
	virtual void SetMeleeTargeted_Implementation(AActor* /*InstigatorActor*/, bool /*bTargeted*/) {}

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Melee|Result")
	void HandleMeleeHitConfirmed(const FMeleeHitContext& Context);
	virtual void HandleMeleeHitConfirmed_Implementation(const FMeleeHitContext& /*Context*/) {}

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Melee|Result")
	void HandleMeleeHitFeedback(const FMeleeHitContext& Context);
	virtual void HandleMeleeHitFeedback_Implementation(const FMeleeHitContext& /*Context*/) {}
};
