#include "DeathTransitionSequence.h"

#include "FPostProcessStructures.h"

namespace DeathTransitionSequence
{
	uint8 PassBit(EDeathTransitionPass Pass)
	{
		return static_cast<uint8>(1u << static_cast<uint8>(Pass));
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

	Parameters.DeathNoise.Time = 0.0f;
	Parameters.DeathFade.Progress = 0.0f;
	Parameters.DeathBlack.Progress = 0.0f;

	ApplyPassStates(Parameters, UIParameters);
}

bool FDeathTransitionSequence::Tick(
	float DeltaTime,
	FPostProcessStrcture& Parameters,
	FPostProcessStrctureUI& UIParameters,
	bool& bOutBlackoutStarted)
{
	bOutBlackoutStarted = false;

	if (DeltaTime <= 0.0f
		|| Phase == EDeathTransitionPhase::Idle
		|| Phase == EDeathTransitionPhase::Black)
	{
		return false;
	}

	PhaseElapsedTime += DeltaTime;

	switch (Phase)
	{
	case EDeathTransitionPhase::Fading:
	{
		FDeathFadeParameters& Fade = Parameters.DeathFade;
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
		if (PhaseElapsedTime < FMath::Max(Parameters.DeathFade.Delay, 0.0f))
		{
			return false;
		}

		EnterPhase(EDeathTransitionPhase::Blackout);
		Parameters.DeathBlack.Progress = 0.0f;
		ApplyPassStates(Parameters, UIParameters);
		bOutBlackoutStarted = true;
		return true;
	}

	case EDeathTransitionPhase::Blackout:
	{
		FDeathBlackParameters& Black = Parameters.DeathBlack;
		const float Duration = FMath::Max(Black.Duration, KINDA_SMALL_NUMBER);
		Black.Progress = FMath::Clamp(PhaseElapsedTime / Duration, 0.0f, 1.0f);
		if (Black.Progress >= 1.0f)
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

	// 완전 검정 상태에서는 Black만 유지한다. Noise/Fade를 계속 등록하면
	// 리스폰 전까지 매 프레임 불필요한 RDG 패스가 추가된다.
	const bool bBlackOnly = Phase == EDeathTransitionPhase::Black;
	Parameters.DeathNoise.bEnabled = bActive && !bBlackOnly && IsPassEnabled(EDeathTransitionPass::Noise) ? 1 : 0;
	Parameters.DeathFade.bEnabled = bActive && !bBlackOnly && IsPassEnabled(EDeathTransitionPass::Fade) ? 1 : 0;
	Parameters.DeathBlack.bEnabled = bBlackActive && IsPassEnabled(EDeathTransitionPass::Black) ? 1 : 0;
	UIParameters.DeathChromaticAberration.bEnabled =
		bActive && IsPassEnabled(EDeathTransitionPass::ChromaticAberration) ? 1 : 0;
}
