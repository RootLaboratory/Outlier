#pragma once

#include "CoreMinimal.h"

struct FPostProcessStrcture;
struct FPostProcessStrctureUI;

enum class EDeathTransitionPhase : uint8
{
	Idle,
	// Fade가 0 → MaxStrength로 보간 중.
	Fading,
	// Fade 완료 후 Delay 대기.
	Holding,
	// Black이 0 → 1로 보간 중. 이 단계에 들어가는 순간 PreSetLoadWidget이 뜬다.
	Blackout,
	// 완전 검정 유지.
	Black
};

enum class EDeathTransitionPass : uint8
{
	Noise,
	Fade,
	Black,
	ChromaticAberration
};

// 사망 연출 타임라인. 서브시스템이 들고 Tick만 돌려주며, 파라미터 구조체의 사망 연출 필드만 건드린다.
// Noise / Fade / Black / 사망 CA를 한 세트로 켜고 끈다.
class RDG_API FDeathTransitionSequence
{
public:
	// 진행 중이었다면 처음부터 다시 시작한다.
	void Start(FPostProcessStrcture& Parameters, FPostProcessStrctureUI& UIParameters);

	// 사망 연출을 전부 끄고 Idle로 되돌린다. 기존 렌즈 CA를 다시 켜는 건 여기서 하지 않는다.
	void Reset(FPostProcessStrcture& Parameters, FPostProcessStrctureUI& UIParameters);

	// 파라미터가 바뀌었으면 true. bOutBlackoutStarted는 이번 틱에 Black 단계가 시작됐을 때 true.
	bool Tick(
		float DeltaTime,
		FPostProcessStrcture& Parameters,
		FPostProcessStrctureUI& UIParameters,
		bool& bOutBlackoutStarted);

	// 패스별 on/off. 꺼진 패스는 그리기만 빠지고 타임라인(위젯이 뜨는 시점 포함)은 그대로 흐른다.
	void SetPassEnabled(
		EDeathTransitionPass Pass,
		bool bEnabled,
		FPostProcessStrcture& Parameters,
		FPostProcessStrctureUI& UIParameters);
	bool IsPassEnabled(EDeathTransitionPass Pass) const;

	bool IsActive() const { return Phase != EDeathTransitionPhase::Idle; }
	EDeathTransitionPhase GetPhase() const { return Phase; }

private:
	void EnterPhase(EDeathTransitionPhase NewPhase);

	// 현재 단계와 패스 플래그로 각 패스의 bEnabled를 다시 계산한다.
	void ApplyPassStates(FPostProcessStrcture& Parameters, FPostProcessStrctureUI& UIParameters) const;

	EDeathTransitionPhase Phase = EDeathTransitionPhase::Idle;
	float PhaseElapsedTime = 0.0f;

	// EDeathTransitionPass 순서의 비트. 기본은 전부 켜짐.
	uint8 EnabledPassMask = 0b1111;
};
