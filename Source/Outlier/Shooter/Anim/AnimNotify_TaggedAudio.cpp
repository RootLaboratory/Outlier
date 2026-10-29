#include "Shooter/Anim/AnimNotify_TaggedAudio.h"

#include "Audio/OutlierAudioSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

UAnimNotify_TaggedAudio::UAnimNotify_TaggedAudio()
{
	AudioTypeTag = FGameplayTag::RequestGameplayTag(TEXT("Audio.Type.Player"), false);
}

bool UAnimNotify_TaggedAudio::IsGameplayOwnedContext(FGameplayTag Context)
{
	// 실제 성공/상태 이벤트가 이미 요청하는 소리는 애니메이션에서 다시 요청하지 않는다.
	static const FGameplayTagContainer GameplayOwnedContexts = []
	{
		FGameplayTagContainer Tags;
		for (const TCHAR* Name : {
			TEXT("Audio.Context.Player.01.Type.Pistol.Shoot"),
			TEXT("Audio.Context.Player.01.Type.Rifle.Shoot"),
			TEXT("Audio.Context.Player.02.Type.2P.Shoot"),
			TEXT("Audio.Context.Player.01.Type.Pistol.Change"),
			TEXT("Audio.Context.Player.01.Type.Rifle.Change"),
			TEXT("Audio.Context.Player.01.Type.Melee.change"),
			TEXT("Audio.Context.Player.01.Type.Melee.Swing"),
			TEXT("Audio.Context.Player.01.Type.Rifle.Empty"),
			TEXT("Audio.Context.Player.01.Type.Pistol.Hit"),
			TEXT("Audio.Context.Player.01.Type.Rifle.Hit"),
			TEXT("Audio.Context.Player.01.Movement.Jump"),
			TEXT("Audio.Context.Player.01.Movement.Land"),
			TEXT("Audio.Context.Player.01.Movement.Sliding") })
		{
			const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(Name), false);
			if (Tag.IsValid()) Tags.AddTag(Tag);
		}
		// 옛 Melee.Hit는 Plastic으로 Redirect된다. 부모를 얻어 나머지 표면 타격음도 함께 차단한다.
		const FGameplayTag MeleePlastic = FGameplayTag::RequestGameplayTag(
			TEXT("Audio.Context.Player.01.Type.Melee.Hit.Plastic"), false);
		if (MeleePlastic.IsValid()) Tags.AddTag(MeleePlastic.RequestDirectParent());
		return Tags;
	}();
	return Context.MatchesAny(GameplayOwnedContexts);
}

void UAnimNotify_TaggedAudio::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);
	if (!IsValid(MeshComp) || !AudioTypeTag.IsValid() || !AudioContextTag.IsValid()
		|| IsGameplayOwnedContext(AudioContextTag)) return;

	UWorld* World = MeshComp->GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	if (UOutlierAudioSubsystem* Audio = GameInstance ? GameInstance->GetSubsystem<UOutlierAudioSubsystem>() : nullptr)
	{
		// Notify 객체는 여러 캐릭터가 공유하므로 소유자 캐시 없이 이번 메시의 Subsystem에 전달한다.
		Audio->PlayAnimationAudio(MeshComp, AudioTypeTag, AudioContextTag, SocketName,
			bQuerySurface, bQuerySurface || bWorldMeshOnly, TraceDistance);
	}
}
