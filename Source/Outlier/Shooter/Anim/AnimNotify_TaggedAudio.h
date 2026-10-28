#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "GameplayTagContainer.h"
#include "AnimNotify_TaggedAudio.generated.h"

/** 애니메이션 타이밍만 전달한다. 재생 메시 선택, 표면 조회, 사운드 재생은 오디오 서브시스템이 처리한다. */
UCLASS(meta = (DisplayName = "Tagged Audio"))
class OUTLIER_API UAnimNotify_TaggedAudio : public UAnimNotify
{
	GENERATED_BODY()

public:
	UAnimNotify_TaggedAudio();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (Categories = "Audio.Type"))
	FGameplayTag AudioTypeTag;

	/** 재장전 등 애니메이션 세부음에 사용한다. 발사·착지처럼 게임플레이가 직접 요청하는 Context는 무시한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (Categories = "Audio.Context"))
	FGameplayTag AudioContextTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	FName SocketName = NAME_None;

	/** 발 접촉 시 지상 이동 여부를 확인하고, 월드 메시의 소켓 아래에서 표면을 조회한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Surface")
	bool bQuerySurface = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Surface", meta = (ClampMin = "1.0", EditCondition = "bQuerySurface"))
	float TraceDistance = 80.0f;

	/** 비활성화하면 로컬은 1인칭 메시, 원격은 월드 메시를 사용한다. 표면 조회는 항상 월드 메시를 사용한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (EditCondition = "!bQuerySurface"))
	bool bWorldMeshOnly = false;

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	/** 루프 Notify와 공유하여 실제 게임플레이 이벤트만 해당 사운드를 요청하도록 한다. */
	static bool IsGameplayOwnedContext(FGameplayTag Context);
};
