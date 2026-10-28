#include "Shooter/Anim/AnimNotifyState_TaggedAudio.h"

#include "Audio/OutlierAudioSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Shooter/Anim/AnimNotify_TaggedAudio.h"

UAnimNotifyState_TaggedAudio::UAnimNotifyState_TaggedAudio()
{
	AudioTypeTag = FGameplayTag::RequestGameplayTag(TEXT("Audio.Type.Player"), false);
}

void UAnimNotifyState_TaggedAudio::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	SetLoopEnabled(MeshComp, true);
}

void UAnimNotifyState_TaggedAudio::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
	SetLoopEnabled(MeshComp, false);
}

void UAnimNotifyState_TaggedAudio::SetLoopEnabled(USkeletalMeshComponent* MeshComp, bool bEnabled) const
{
	if (!IsValid(MeshComp) || !AudioTypeTag.IsValid() || !AudioContextTag.IsValid()
		|| UAnimNotify_TaggedAudio::IsGameplayOwnedContext(AudioContextTag)) return;
	// 발 접촉은 매번 바닥을 확인하는 단발 Notify만 사용한다. State로 지속 요청하지 않는다.
	static const FGameplayTag FootstepContext = FGameplayTag::RequestGameplayTag(TEXT("Audio.Context.Player.01.Movement.Footstep"), false);
	if (FootstepContext.IsValid() && AudioContextTag.MatchesTag(FootstepContext)) return;

	UWorld* World = MeshComp->GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UOutlierAudioSubsystem* Audio = GameInstance ? GameInstance->GetSubsystem<UOutlierAudioSubsystem>() : nullptr;
	if (!Audio) return;

	// Subsystem이 시작을 허용한 메시를 기록하므로, 시점이 바뀐 뒤에도 해당 메시의 종료는 처리된다.
	// NotifyEnd 누락 시에는 실제 행동 취소/해제 경로가 StopAnimationAudioLoops로 정리한다.
	Audio->SetAnimationAudioLoop(MeshComp, AudioTypeTag, AudioContextTag, bEnabled, bWorldMeshOnly, bTwoD);
}
