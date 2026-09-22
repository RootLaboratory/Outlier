#include "GAS/Cues/OutlierCueAudio.h"

#include "Audio/OutlierAudioSubsystem.h"
#include "Audio/OutlierAudioTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayEffectTypes.h"

bool OutlierCueAudio::Play(
	const AActor* Target,
	const FGameplayCueParameters& Parameters,
	const FGameplayTag& AudioTypeTag,
	const FGameplayTag& AudioContextTag)
{
	if (!AudioTypeTag.IsValid() || !AudioContextTag.IsValid())
	{
		return false;
	}

	// Static 계열 notify 는 CDO 에서 실행된다 — 호출자의 GetWorld() 는 null 이다.
	// 월드는 반드시 Target 에서 가져온다.
	const UWorld* World = Target ? Target->GetWorld() : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UOutlierAudioSubsystem* AudioSubsystem =
		GameInstance ? GameInstance->GetSubsystem<UOutlierAudioSubsystem>() : nullptr;
	if (!AudioSubsystem)
	{
		return false;
	}

	FOutlierAudioPlayRequest Request;
	Request.EventTag = AudioTypeTag;
	Request.ContextTags.AddTag(AudioContextTag);
	Request.EmitterActor = const_cast<AActor*>(Target);

	if (const FHitResult* HitResult = Parameters.EffectContext.GetHitResult();
		HitResult && HitResult->bBlockingHit)
	{
		Request.Location = HitResult->ImpactPoint;
		Request.bHasLocation = true;
	}
	else if (!Parameters.Location.IsZero())
	{
		Request.Location = Parameters.Location;
		Request.bHasLocation = true;
	}
	// 둘 다 없으면 BuildResolvedPlay 가 EmitterActor 위치로 폴백한다.

	// 반드시 Local 진입점을 쓴다. 이 코드는 이미 각 클라이언트에서 돌고 있으므로
	// Relevant* 계열을 부르면 네트워크를 한 번 더 타 중복 재생된다.
	return AudioSubsystem->PlayLocalAtLocation(Request);
}
