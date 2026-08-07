#include "Weapon/OutlierWeaponCameraShake.h"

void UOutlierWeaponShakePattern::GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const
{
	OutInfo.Duration = FCameraShakeDuration::Infinite();
	OutInfo.BlendIn = 0.0f;
	OutInfo.BlendOut = 0.025f;
}

void UOutlierWeaponShakePattern::StartShakePatternImpl(const FCameraShakePatternStartParams& Params)
{
	(void)Params;
	ElapsedTime = 0.0f;
}

void UOutlierWeaponShakePattern::UpdateShakePatternImpl(
	const FCameraShakePatternUpdateParams& Params,
	FCameraShakePatternUpdateResult& OutResult)
{
	// 재시작 직후 첫 카메라 프레임은 시간 0의 최대 충격을 사용한다.
	EvaluateShake(ElapsedTime, OutResult);
	ElapsedTime += FMath::Max(Params.DeltaTime, 0.0f);
}

void UOutlierWeaponShakePattern::ScrubShakePatternImpl(
	const FCameraShakePatternScrubParams& Params,
	FCameraShakePatternUpdateResult& OutResult)
{
	ElapsedTime = FMath::Max(Params.AbsoluteTime, 0.0f);
	EvaluateShake(ElapsedTime, OutResult);
}

bool UOutlierWeaponShakePattern::IsFinishedImpl() const
{
	return false;
}

void UOutlierWeaponShakePattern::EvaluateShake(
	float TimeSeconds,
	FCameraShakePatternUpdateResult& OutResult) const
{
	// 매 발 최대 충격으로 시작해 빠르게 감쇠한다. 연사 재시작마다 위상을 바꿔 같은 움직임이 반복되지 않게 한다.
	const float ImpulseEnvelope = FMath::Exp(-28.0f * FMath::Max(TimeSeconds, 0.0f));
	const float PhaseOffset = static_cast<float>(ImpactSequence % 1024u) * 11.3f;
	const float PitchNoise = FMath::PerlinNoise1D((TimeSeconds * 35.0f) + 3.1f + PhaseOffset);
	// Gameplay 반동과 같은 위쪽 방향을 유지하면서 강도만 조금 흔들어 첫 충격이 아래로 꺾이지 않게 한다.
	OutResult.Rotation.Pitch =
		(0.75f + (PitchNoise * 0.25f)) * 0.32f * ImpulseEnvelope;
	OutResult.Location.X =
		FMath::PerlinNoise1D((TimeSeconds * 55.0f) + 17.7f + PhaseOffset) * 0.45f * ImpulseEnvelope;
	OutResult.Location.Y =
		FMath::PerlinNoise1D((TimeSeconds * 60.0f) + 31.3f + PhaseOffset) * 0.40f * ImpulseEnvelope;
	OutResult.FOV =
		FMath::PerlinNoise1D((TimeSeconds * 35.0f) + 47.9f + PhaseOffset) * 0.12f * ImpulseEnvelope;
}

UOutlierWeaponCameraShake::UOutlierWeaponCameraShake(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UOutlierWeaponShakePattern>(TEXT("RootShakePattern")))
{
	bSingleInstance = true;
}

void UOutlierWeaponCameraShake::SetImpactSequence(uint32 InImpactSequence)
{
	if (UOutlierWeaponShakePattern* WeaponPattern =
		Cast<UOutlierWeaponShakePattern>(GetRootShakePattern()))
	{
		WeaponPattern->SetImpactSequence(InImpactSequence);
	}
}
