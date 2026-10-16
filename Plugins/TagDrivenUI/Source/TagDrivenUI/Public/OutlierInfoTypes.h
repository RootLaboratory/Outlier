#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "OutlierInfoTypes.generated.h"

UENUM(BlueprintType)
enum class EOutlierInfoRegion : uint8
{
	Upper,
	Right,
	BottomTTS
};

UENUM(BlueprintType)
enum class EOutlierInfoEndCondition : uint8
{
	Manual,
	Time,
	Overlap,
	/** 소스가 완료나 취소를 알릴 때만 종료한다. */
	Source
};

/** 표시 요청일 뿐이다. 콘텐츠와 활성 상태는 받는 위젯이 소유한다. */
USTRUCT(BlueprintType)
struct TAGDRIVENUI_API FOutlierInfoWidgetRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info", meta = (Categories = "Event"))
	FGameplayTag EventTag;

	/** 이 영역 체인을 시작한 원본 이벤트. 완료/취소는 원본 전체를 대상으로 할 수 있다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info", meta = (Categories = "Event"))
	FGameplayTag OriginEventTag;

	/** 선택적 TTS 직접 참조. Event DT 행이 없는 음성도 포함한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info", meta = (Categories = "Audio.Context"))
	FGameplayTag TTSTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info", meta = (Categories = "UI.Info.Upper"))
	FGameplayTag UpperTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info")
	FGameplayTag GifTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info")
	EOutlierInfoEndCondition EndCondition = EOutlierInfoEndCondition::Source;

	/** 표시 시점부터 센다. 0이면 Time 위젯이 DT 기본값을 읽고, Source는 duration을 무시한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info", meta = (ClampMin = "0.0"))
	float Duration = 0.0f;
};
