#include "DeathTransitionSequence.h"

#include "FPostProcessStructures.h"

namespace DeathTransitionSequence
{
	uint8 PassBit(EDeathTransitionPass Pass)
	{
		return static_cast<uint8>(1u << static_cast<uint8>(Pass));
	}

	// Background와 Noise 페이드가 모두 끝난 시점.
	// 페이드 시간이 레이어마다 달라서 마지막 레이어가 가장 늦게 끝난다는 보장은 없다.
	float GetBlackoutEndTime(const FDeathBlackParameters& Black)
	{
		float EndTime = GetDeathBlackLayerEndTime(Black, EDeathBlackLayer::Background);
		return FMath::Max(EndTime, GetDeathBlackLayerEndTime(Black, EDeathBlackLayer::Noise));
	}
}

void FDeathTransitionSequence::Start(FPostProcessStrcture& Parameters, FPostProcessStrctureUI& UIParameters)
{
	Reset(Parameters, UIParameters);
	EnterPhase(EDeathTransitionPhase::Fading);

	// 기존 렌즈 CA를 끄고 사망 CA를 켠다. 같은 갱신 안에서 바꿔야 둘이 겹치거나 둘 다 꺼진 프레임이 없다.
	// 사망 CA가 디버그로 꺼져 있어도 기존 CA는 사망 순간 꺼지는 게 정책이다.
	UIParameters.ChromaticAberration.bEnabled = 0;

	ApplyPassStates(Parameters, UIParameters);
}

void FDeathTransitionSequence::Reset(FPostProcessStrcture& Parameters, FPostProcessStrctureUI& UIParameters)
{
	EnterPhase(EDeathTransitionPhase::Idle);
	bNoiseStartEventSent = false;

	Parameters.DeathNoise.Time = 0.0f;
	Parameters.DeathFade.Progress = 0.0f;
	Parameters.DeathFade.ElapsedTime = 0.0f;
	Parameters.DeathBlack.ElapsedTime = 0.0f;

	ApplyPassStates(Parameters, UIParameters);
}

bool FDeathTransitionSequence::Tick(
	float DeltaTime,
	FPostProcessStrcture& Parameters,
	FPostProcessStrctureUI& UIParameters,
	bool& bOutBlackNoiseStarted)
{
	bOutBlackNoiseStarted = false;

	if (DeltaTime <= 0.0f || Phase == EDeathTransitionPhase::Idle)
	{
		return false;
	}

	// 비네트 타이밍. Fade 시작부터 흐르고, 비네트는 리스폰까지 남으므로 다 나오기 전이면 Black 단계에서도 흐른다.
	FDeathFadeParameters& Fade = Parameters.DeathFade;
	const bool bVignetteRising = Fade.ElapsedTime < GetDeathFadeVignetteEndTime(Fade);
	Fade.ElapsedTime += DeltaTime;

	if (Phase == EDeathTransitionPhase::Black)
	{
		return bVignetteRising;
	}

	PhaseElapsedTime += DeltaTime;

	switch (Phase)
	{
	case EDeathTransitionPhase::Fading:
	{
		const float Duration = FMath::Max(Fade.Duration, KINDA_SMALL_NUMBER);
		Fade.Progress = FMath::Clamp(PhaseElapsedTime / Duration, 0.0f, 1.0f);
		if (Fade.Progress >= 1.0f)
		{
			EnterPhase(EDeathTransitionPhase::Holding);
		}
		return true;
	}

	case EDeathTransitionPhase::Holding:
	{
		// 대기 중에는 비네트가 움직일 때만 갱신한다.
		if (PhaseElapsedTime < FMath::Max(Fade.Delay, 0.0f))
		{
			return bVignetteRising;
		}

		EnterPhase(EDeathTransitionPhase::Blackout);
		Parameters.DeathBlack.ElapsedTime = 0.0f;
		ApplyPassStates(Parameters, UIParameters);
		// 페이드 시간이 0이고 Noise 지연도 0이면 첫 Blackout 틱에 바로 보인다.
		if (!bNoiseStartEventSent && GetDeathBlackLayerAmount(Parameters.DeathBlack, EDeathBlackLayer::Noise) > 0.0f)
		{
			bNoiseStartEventSent = true;
			bOutBlackNoiseStarted = true;
		}
		return true;
	}

	case EDeathTransitionPhase::Blackout:
	{
		// 레이어별 세기는 Black 패스가 ElapsedTime과 시작 시점으로 계산한다.
		FDeathBlackParameters& Black = Parameters.DeathBlack;
		Black.ElapsedTime = PhaseElapsedTime;
		if (!bNoiseStartEventSent && GetDeathBlackLayerAmount(Black, EDeathBlackLayer::Noise) > 0.0f)
		{
			bNoiseStartEventSent = true;
			bOutBlackNoiseStarted = true;
		}
		if (Black.ElapsedTime >= DeathTransitionSequence::GetBlackoutEndTime(Black))
		{
			EnterPhase(EDeathTransitionPhase::Black);
			ApplyPassStates(Parameters, UIParameters);
		}
		return true;
	}

	default:
		return false;
	}
}

void FDeathTransitionSequence::SetPassEnabled(
	EDeathTransitionPass Pass,
	bool bEnabled,
	FPostProcessStrcture& Parameters,
	FPostProcessStrctureUI& UIParameters)
{
	const uint8 Bit = DeathTransitionSequence::PassBit(Pass);
	EnabledPassMask = bEnabled ? (EnabledPassMask | Bit) : (EnabledPassMask & ~Bit);
	ApplyPassStates(Parameters, UIParameters);
}

bool FDeathTransitionSequence::IsPassEnabled(EDeathTransitionPass Pass) const
{
	return (EnabledPassMask & DeathTransitionSequence::PassBit(Pass)) != 0;
}

void FDeathTransitionSequence::EnterPhase(EDeathTransitionPhase NewPhase)
{
	Phase = NewPhase;
	PhaseElapsedTime = 0.0f;
}

void FDeathTransitionSequence::ApplyPassStates(FPostProcessStrcture& Parameters, FPostProcessStrctureUI& UIParameters) const
{
	const bool bActive = Phase != EDeathTransitionPhase::Idle;
	const bool bBlackActive = Phase == EDeathTransitionPhase::Blackout || Phase == EDeathTransitionPhase::Black;

	// 최종 단계에서 Fade는 Black에 완전히 덮이므로 뺀다.
	// 비네트와 Noise는 Black 위에 그려지므로 리스폰 전까지 계속 유지한다. 비네트는 Fade 패스 플래그를 따른다.
	const bool bFinalBlack = Phase == EDeathTransitionPhase::Black;
	Parameters.DeathNoise.bEnabled = bActive && IsPassEnabled(EDeathTransitionPass::Noise) ? 1 : 0;
	Parameters.DeathFade.bEnabled = bActive && !bFinalBlack && IsPassEnabled(EDeathTransitionPass::Fade) ? 1 : 0;
	Parameters.DeathFade.bVignetteEnabled = bActive && IsPassEnabled(EDeathTransitionPass::Fade) ? 1 : 0;
	Parameters.DeathBlack.bEnabled = bBlackActive && IsPassEnabled(EDeathTransitionPass::Black) ? 1 : 0;
	UIParameters.DeathChromaticAberration.bEnabled =
		bActive && IsPassEnabled(EDeathTransitionPass::ChromaticAberration) ? 1 : 0;
}
