#pragma once

#include "Camera/CameraShakeBase.h"
#include "OutlierWeaponCameraShake.generated.h"

/** 총기 한 발의 짧고 고주파인 화면 충격을 생성한다. 지속시간은 CameraManager가 관리한다. */
UCLASS(EditInlineNew)
class OUTLIER_API UOutlierWeaponShakePattern : public UCameraShakePattern
{
	GENERATED_BODY()

public:
	void SetImpactSequence(uint32 InImpactSequence) { ImpactSequence = InImpactSequence; }

private:
	virtual void GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const override;
	virtual void StartShakePatternImpl(const FCameraShakePatternStartParams& Params) override;
	virtual void UpdateShakePatternImpl(
		const FCameraShakePatternUpdateParams& Params,
		FCameraShakePatternUpdateResult& OutResult) override;
	virtual void ScrubShakePatternImpl(
		const FCameraShakePatternScrubParams& Params,
		FCameraShakePatternUpdateResult& OutResult) override;
	virtual bool IsFinishedImpl() const override;

	void EvaluateShake(float TimeSeconds, FCameraShakePatternUpdateResult& OutResult) const;

	float ElapsedTime = 0.0f;
	uint32 ImpactSequence = 0;
};

UCLASS(BlueprintType)
class OUTLIER_API UOutlierWeaponCameraShake : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UOutlierWeaponCameraShake(const FObjectInitializer& ObjectInitializer);
	void SetImpactSequence(uint32 InImpactSequence);
};
