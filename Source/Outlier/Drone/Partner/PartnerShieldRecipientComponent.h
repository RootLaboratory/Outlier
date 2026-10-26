#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PartnerShieldRecipientComponent.generated.h"

class APartnerCharacter;
class APartnerShieldSphere;
class UOutlierAbilitySystemComponent;

// Shooter와 아군 터렛이 같은 PartnerShield 수치/감소 규칙을 사용한다.
UCLASS()
class OUTLIER_API UPartnerShieldRecipientComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UPartnerShieldRecipientComponent();
	bool ApplyShield(float Amount, float Duration, float HealthRegenRate = 0.0f);
	void ShowShield(TSubclassOf<APartnerShieldSphere> VisualClass, APartnerCharacter* Source);
	void DestroyVisual();
	void ClearShield();
	bool HasShield() const;
	APartnerShieldSphere* GetVisual() const { return ShieldVisual; }

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// Applied in the shield's local space after its default target-relative placement.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shield|Transform")
	FTransform ShieldLocalTransform = FTransform::Identity;

private:
	UOutlierAbilitySystemComponent* GetASC() const;
	void UpdateDecay();
	FTimerHandle DecayTimer;
	float ShieldDuration = 0.0f;
	float ShieldHealthRegenRate = 0.0f;
	UPROPERTY(Transient)
	TObjectPtr<APartnerShieldSphere> ShieldVisual;
};
