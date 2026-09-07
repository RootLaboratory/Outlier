#pragma once

#include "Camera/CameraShakeBase.h"
#include "OutlierExplosionCameraShake.generated.h"

/** 짧고 비방향성인 폭발 회전 흔들림을 생성한다. */
UCLASS(EditInlineNew)
class OUTLIER_API UOutlierExplosionShakePattern : public UCameraShakePattern
{
	GENERATED_BODY()

private:
	virtual void GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const override;
	virtual void StartShakePatternImpl(const FCameraShakePatternStartParams& Params) override;
	virtual void UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params, FCameraShakePatternUpdateResult& OutResult) override;
	virtual void ScrubShakePatternImpl(const FCameraShakePatternScrubParams& Params, FCameraShakePatternUpdateResult& OutResult) override;
	virtual bool IsFinishedImpl() const override;

	void EvaluateShake(float TimeSeconds, FCameraShakePatternUpdateResult& OutResult) const;

	float ElapsedTime = 0.0f;
	static constexpr float DurationSeconds = 0.35f;
};

UCLASS(BlueprintType)
class OUTLIER_API UOutlierExplosionCameraShake : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UOutlierExplosionCameraShake(const FObjectInitializer& ObjectInitializer);
};
