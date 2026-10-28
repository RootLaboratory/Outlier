#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "TaggedAudioBatchSettings.generated.h"

class UAnimSequenceBase;

UENUM()
enum class ETaggedAudioBatchMode : uint8
{
	Add UMETA(DisplayName = "설정으로 추가"),
	Copy UMETA(DisplayName = "기준 애니메이션에서 복사")
};

/** 에디터 패널의 입력값이다. 런타임 설정이나 별도 에셋으로 저장하지 않는다. */
UCLASS()
class UTaggedAudioBatchSettings : public UObject
{
	GENERATED_BODY()
public:
	UTaggedAudioBatchSettings();

	UPROPERTY(EditAnywhere, Category = "배치")
	ETaggedAudioBatchMode Mode = ETaggedAudioBatchMode::Add;
	UPROPERTY(EditAnywhere, Category = "배치", meta = (EditCondition = "Mode == ETaggedAudioBatchMode::Copy"))
	TObjectPtr<UAnimSequenceBase> SourceAnimation;
	/** 체크하면 입력 시간은 0~1 비율이다. 복사에서는 기준/대상 길이에 비례해 시작과 종료를 변환한다. */
	UPROPERTY(EditAnywhere, Category = "배치")
	bool bNormalizedTime = true;
	UPROPERTY(EditAnywhere, Category = "배치", meta = (EditCondition = "Mode == ETaggedAudioBatchMode::Add"))
	TArray<float> Times = { 0.25f, 0.75f };
	UPROPERTY(EditAnywhere, Category = "배치")
	FName TrackName = TEXT("TaggedAudio");
	UPROPERTY(EditAnywhere, Category = "추가 설정", meta = (EditCondition = "Mode == ETaggedAudioBatchMode::Add"))
	bool bLoop = false;
	/** 단발은 무시한다. 정규화 모드에서는 애니메이션 길이에 대한 비율이다. */
	UPROPERTY(EditAnywhere, Category = "추가 설정", meta = (ClampMin = "0.0001", EditCondition = "bLoop && Mode == ETaggedAudioBatchMode::Add"))
	float Duration = 0.25f;
	UPROPERTY(EditAnywhere, Category = "추가 설정", meta = (Categories = "Audio.Type"))
	FGameplayTag AudioTypeTag;
	UPROPERTY(EditAnywhere, Category = "추가 설정", meta = (Categories = "Audio.Context"))
	FGameplayTag AudioContextTag;
	UPROPERTY(EditAnywhere, Category = "추가 설정", meta = (EditCondition = "!bLoop"))
	FName SocketName = NAME_None;
	UPROPERTY(EditAnywhere, Category = "추가 설정", meta = (EditCondition = "!bLoop"))
	bool bQuerySurface = true;
	UPROPERTY(EditAnywhere, Category = "추가 설정", meta = (ClampMin = "1.0", EditCondition = "!bLoop && bQuerySurface"))
	float TraceDistance = 80.0f;
	/** 발걸음/표면 조회는 항상 월드 메시를 사용한다. 일반 행동은 꺼두면 로컬 1P/원격 3P를 사용한다. */
	UPROPERTY(EditAnywhere, Category = "추가 설정")
	bool bWorldMeshOnly = true;
	UPROPERTY(EditAnywhere, Category = "추가 설정", meta = (EditCondition = "bLoop"))
	bool bTwoD = false;
};
