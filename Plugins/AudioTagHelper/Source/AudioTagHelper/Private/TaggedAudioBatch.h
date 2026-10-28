#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

class UAnimSequenceBase;
class UTaggedAudioBatchSettings;

/** 미리보기와 적용이 같은 판정/설정 값을 사용하도록 배치 계획을 값으로 보관한다. */
struct FTaggedAudioPlacement
{
	bool bLoop = false;
	FGameplayTag Type;
	FGameplayTag Context;
	FName Socket;
	bool bQuerySurface = false;
	float TraceDistance = 80.0f;
	bool bWorldMeshOnly = false;
	bool bTwoD = false;
	float Time = 0.0f;
	float Duration = 0.0f;
	TWeakObjectPtr<UObject> SourceNotify;
};

struct FTaggedAudioBatchRow
{
	TWeakObjectPtr<UAnimSequenceBase> Target;
	FTaggedAudioPlacement Placement;
	/** 기존 이벤트는 GUID로 다시 찾는다. 정렬/Undo로 배열 순서가 바뀌어도 같은 Notify만 이동한다. */
	FGuid ExistingGuid;
	float PreviousTime = 0.0f;
	FName MarkerName;
	/** 이름 정렬은 오디오 설정을 읽거나 변경하지 않고 기존 이벤트 자체의 시간만 옮긴다. */
	bool bNamedNotify = false;
	FName ExistingNotifyName;
	int32 PreviousTrackIndex = INDEX_NONE;
	FString SkipReason;
};

namespace TaggedAudioBatch
{
	TArray<FTaggedAudioBatchRow> Preview(const UTaggedAudioBatchSettings& Settings,
		const TArray<UAnimSequenceBase*>& Targets);
	/** 단일 Undo 묶음으로 추가/이동한다. 기존 Notify 삭제/교체 및 패키지 저장은 수행하지 않는다. */
	int32 Apply(const UTaggedAudioBatchSettings& Settings, const TArray<FTaggedAudioBatchRow>& Rows);
	FString Describe(const TArray<FTaggedAudioBatchRow>& Rows);
}
