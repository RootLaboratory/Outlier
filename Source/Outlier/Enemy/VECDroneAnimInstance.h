#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Enemy/EnemyBase.h"
#include "VECDroneAnimInstance.generated.h"

class AVECDrone;

UCLASS(Blueprintable, Transient)
class OUTLIER_API UVECDroneAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUninitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual void NativePostEvaluateAnimation() override;

protected:
	// Set this in the AnimBP class defaults so FP/TP graphs can share this parent class.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VECDrone|Presentation")
	bool bFirstPersonPresentation = false;

	// Bind this to the Firing state's Sequence Evaluator explicit time.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VECDrone|Attack", meta = (ClampMin = "0.0"))
	float GatherPoseTimeSeconds = 0.5f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "VECDrone|Owner")
	TObjectPtr<AVECDrone> CachedVECDrone = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VECDrone|Attack")
	EEnemyAttackPhase AttackPhase = EEnemyAttackPhase::Idle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VECDrone|Attack")
	bool bIsAttackIdle = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VECDrone|Attack")
	bool bIsGathering = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VECDrone|Attack")
	bool bIsFiring = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VECDrone|Attack")
	bool bIsRecovering = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VECDrone|Attack")
	bool bIsPossessed = false;

private:
	void CacheOwningVECDrone();
	void RefreshAnimationState();
	void ResetAnimationState();
	void LogAnimationConfiguration() const;
	const TCHAR* GetPresentationName() const;

	EEnemyAttackPhase LastLoggedAttackPhase = EEnemyAttackPhase::Idle;
	FName LastLoggedGraphState = NAME_None;
	int32 ConsecutiveMismatchFrames = 0;
	bool bHasLoggedAttackPhase = false;
	bool bHasLoggedGraphState = false;
	bool bHasLoggedMismatch = false;
};
