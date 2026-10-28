#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "GameplayTagContainer.h"
#include "AnimNotifyState_TaggedAudio.generated.h"

/** 애니메이션 구간 동안 루프를 재생하는 Notify. 액션을 취소하는 경로에서도 해당 루프를 중단해야 한다. */
UCLASS(meta = (DisplayName = "Tagged Audio Loop"))
class OUTLIER_API UAnimNotifyState_TaggedAudio : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	UAnimNotifyState_TaggedAudio();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (Categories = "Audio.Type"))
	FGameplayTag AudioTypeTag;

	/** 루프 정책은 Audio Bank에서 설정한다. 발소리나 게임플레이가 직접 요청하는 사운드에는 사용하지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (Categories = "Audio.Context"))
	FGameplayTag AudioContextTag;

	/** 비활성화하면 로컬 캐릭터는 1인칭 메시, 원격 캐릭터는 월드 메시를 사용한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	bool bWorldMeshOnly = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	bool bTwoD = false;

	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

private:
	void SetLoopEnabled(USkeletalMeshComponent* MeshComp, bool bEnabled) const;
};
