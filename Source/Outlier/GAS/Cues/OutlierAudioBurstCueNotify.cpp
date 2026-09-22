#include "GAS/Cues/OutlierAudioBurstCueNotify.h"

#include "GAS/Cues/OutlierCueAudio.h"

bool UOutlierAudioBurstCueNotify::OnExecute_Implementation(
	AActor* Target,
	const FGameplayCueParameters& Parameters) const
{
	// 파티클 / 카메라 셰이크 / 데칼 등 Burst 배열 연출을 먼저 그대로 돌린다.
	const bool bResult = Super::OnExecute_Implementation(Target, Parameters);

	OutlierCueAudio::Play(Target, Parameters, AudioTypeTag, AudioContextTag);

	return bResult;
}
