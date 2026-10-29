#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "TaggedAudioBatchSettings.generated.h"

class UAnimSequenceBase;

UENUM()
enum class ETaggedAudioBatchMode : uint8
{
	Add UMETA(DisplayName = "설정으로 추가"),
	Copy UMETA(DisplayName = "기준 애니메이션에서 복사"),
	SyncMarkers UMETA(DisplayName = "L/R 마커 기준 추가·정렬"),
	AlignExisting UMETA(DisplayName = "기존 발걸음 위치만 L/R에 정렬"),
	AlignNamedNotify UMETA(DisplayName = "이름으로 기존 Notify만 L/R에 정렬")
};

/** 에디터 패널의 입력값이다. 런타임 설정이나 별도 에셋으로 저장하지 않는다. */
UCLASS()
class UTaggedAudioBatchSettings : public UObject
{
	GENERATED_BODY()
public:
	UTaggedAudioBatchSettings();

	UPROPERTY(EditAnywhere, Category = "배치")
	ETaggedAudioBatchMode Mode = ETaggedAudioBatchMode::SyncMarkers;
	UPROPERTY(EditAnywhere, Category = "배치", meta = (EditCondition = "Mode == ETaggedAudioBatchMode::Copy", EditConditionHides))
	TObjectPtr<UAnimSequenceBase> SourceAnimation;
	/** 체크하면 입력 시간은 0~1 비율이다. 복사에서는 기준/대상 길이에 비례해 시작과 종료를 변환한다. */
	UPROPERTY(EditAnywhere, Category = "배치", meta = (EditCondition = "Mode == ETaggedAudioBatchMode::Add || Mode == ETaggedAudioBatchMode::Copy", EditConditionHides))
	bool bNormalizedTime = true;
	UPROPERTY(EditAnywhere, Category = "배치", meta = (EditCondition = "Mode == ETaggedAudioBatchMode::Add", EditConditionHides))
	TArray<float> Times = { 0.25f, 0.75f };
	UPROPERTY(EditAnywhere, Category = "배치", meta = (EditCondition = "Mode != ETaggedAudioBatchMode::AlignNamedNotify", EditConditionHides))
	FName TrackName = TEXT("TaggedAudio");
	/** 이벤트에 저장된 Notify 이름과 정확히 일치하는 단발 이벤트만 이동한다. */
	UPROPERTY(EditAnywhere, Category = "정렬 대상", meta = (DisplayName = "대상 Notify 이름", EditCondition = "Mode == ETaggedAudioBatchMode::AlignNamedNotify", EditConditionHides))
	FName NotifyNameFilter = TEXT("MakeNoise");
	/** None이면 모든 트랙에서 찾는다. 입력하면 해당 트랙만 정렬한다. */
	UPROPERTY(EditAnywhere, Category = "정렬 대상", meta = (DisplayName = "대상 트랙 (None: 전체)", EditCondition = "Mode == ETaggedAudioBatchMode::AlignNamedNotify", EditConditionHides))
	FName NotifyTrackFilter = NAME_None;
	/** 마커 모드는 각 대상 Sequence 자체의 시간을 사용한다. L/R 마커는 이동하지 않는다. */
	UPROPERTY(EditAnywhere, Category = "마커", meta = (EditCondition = "Mode == ETaggedAudioBatchMode::SyncMarkers || Mode == ETaggedAudioBatchMode::AlignExisting || Mode == ETaggedAudioBatchMode::AlignNamedNotify", EditConditionHides))
	FName LeftMarkerName = TEXT("L");
	UPROPERTY(EditAnywhere, Category = "마커", meta = (EditCondition = "Mode == ETaggedAudioBatchMode::SyncMarkers || Mode == ETaggedAudioBatchMode::AlignExisting || Mode == ETaggedAudioBatchMode::AlignNamedNotify", EditConditionHides))
	FName RightMarkerName = TEXT("R");
	/** 추가 시 소켓을 지정하고, 정렬 시 알려진 좌/우 소켓의 대응을 확인한다. 기존 소켓은 변경하지 않는다. */
	UPROPERTY(EditAnywhere, Category = "마커", meta = (EditCondition = "Mode == ETaggedAudioBatchMode::SyncMarkers || Mode == ETaggedAudioBatchMode::AlignExisting", EditConditionHides))
	FName LeftSocketName = TEXT("foot_l");
	UPROPERTY(EditAnywhere, Category = "마커", meta = (EditCondition = "Mode == ETaggedAudioBatchMode::SyncMarkers || Mode == ETaggedAudioBatchMode::AlignExisting", EditConditionHides))
	FName RightSocketName = TEXT("foot_r");
	UPROPERTY(EditAnywhere, Category = "추가 설정", meta = (EditCondition = "Mode == ETaggedAudioBatchMode::Add", EditConditionHides))
	bool bLoop = false;
	/** 단발은 무시한다. 정규화 모드에서는 애니메이션 길이에 대한 비율이다. */
	UPROPERTY(EditAnywhere, Category = "추가 설정", meta = (ClampMin = "0.0001", EditCondition = "bLoop && Mode == ETaggedAudioBatchMode::Add", EditConditionHides))
	float Duration = 0.25f;
	UPROPERTY(EditAnywhere, Category = "추가 설정", meta = (Categories = "Audio.Type", EditCondition = "Mode != ETaggedAudioBatchMode::AlignNamedNotify", EditConditionHides))
	FGameplayTag AudioTypeTag;
	UPROPERTY(EditAnywhere, Category = "추가 설정", meta = (Categories = "Audio.Context", EditCondition = "Mode != ETaggedAudioBatchMode::AlignNamedNotify", EditConditionHides))
	FGameplayTag AudioContextTag;
	UPROPERTY(EditAnywhere, Category = "추가 설정", meta = (EditCondition = "!bLoop && Mode == ETaggedAudioBatchMode::Add", EditConditionHides))
	FName SocketName = NAME_None;
	UPROPERTY(EditAnywhere, Category = "추가 설정", meta = (EditCondition = "!bLoop && Mode == ETaggedAudioBatchMode::Add", EditConditionHides))
	bool bQuerySurface = true;
	UPROPERTY(EditAnywhere, Category = "추가 설정", meta = (ClampMin = "1.0", EditCondition = "Mode == ETaggedAudioBatchMode::SyncMarkers || (Mode == ETaggedAudioBatchMode::Add && !bLoop && bQuerySurface)", EditConditionHides))
	float TraceDistance = 80.0f;
	/** 발걸음/표면 조회는 항상 월드 메시를 사용한다. 일반 행동은 꺼두면 로컬 1P/원격 3P를 사용한다. */
	UPROPERTY(EditAnywhere, Category = "추가 설정", meta = (EditCondition = "Mode == ETaggedAudioBatchMode::Add", EditConditionHides))
	bool bWorldMeshOnly = true;
	UPROPERTY(EditAnywhere, Category = "추가 설정", meta = (EditCondition = "bLoop && Mode == ETaggedAudioBatchMode::Add", EditConditionHides))
	bool bTwoD = false;
};
