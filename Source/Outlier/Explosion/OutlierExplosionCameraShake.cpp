#include "Explosion/OutlierExplosionCameraShake.h"

void UOutlierExplosionShakePattern::GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const
{
	OutInfo.Duration = FCameraShakeDuration(DurationSeconds);
	OutInfo.BlendIn = 0.02f;
	OutInfo.BlendOut = 0.20f;
}

void UOutlierExplosionShakePattern::StartShakePatternImpl(const FCameraShakePatternStartParams& Params)
{
	(void)Params;
	ElapsedTime = 0.0f;
}

void UOutlierExplosionShakePattern::UpdateShakePatternImpl(
	const FCameraShakePatternUpdateParams& Params,
	FCameraShakePatternUpdateResult& OutResult)
{
	ElapsedTime += FMath::Max(Params.DeltaTime, 0.0f);
	EvaluateShake(ElapsedTime, OutResult);
}

void UOutlierExplosionShakePattern::ScrubShakePatternImpl(
	const FCameraShakePatternScrubParams& Params,
	FCameraShakePatternUpdateResult& OutResult)
{
	ElapsedTime = FMath::Max(Params.AbsoluteTime, 0.0f);
	EvaluateShake(ElapsedTime, OutResult);
}

bool UOutlierExplosionShakePattern::IsFinishedImpl() const
{
	return ElapsedTime >= DurationSeconds;
}

void UOutlierExplosionShakePattern::EvaluateShake(
	float TimeSeconds,
	FCameraShakePatternUpdateResult& OutResult) const
{
	const float NormalizedTime = FMath::Clamp(TimeSeconds / DurationSeconds, 0.0f, 1.0f);
	const float Envelope = FMath::Square(1.0f - NormalizedTime);
	const float TwoPiTime = 2.0f * UE_PI * TimeSeconds;

	OutResult.Rotation.Pitch = FMath::Sin(TwoPiTime * 18.0f) * 1.1f * Envelope;
	OutResult.Rotation.Yaw = FMath::Sin((TwoPiTime * 15.0f) + 1.3f) * 0.75f * Envelope;
	OutResult.Rotation.Roll = FMath::Sin((TwoPiTime * 11.0f) + 2.1f) * 0.12f * Envelope;
}

UOutlierExplosionCameraShake::UOutlierExplosionCameraShake(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UOutlierExplosionShakePattern>(TEXT("RootShakePattern")))
{
	bSingleInstance = true;
}
