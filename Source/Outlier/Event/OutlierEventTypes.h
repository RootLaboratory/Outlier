#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "UI/InputDisplayTypes.h"
#include "OutlierEventTypes.generated.h"

class UTexture2D;

UENUM(BlueprintType)
enum class EOutlierTTSPriority : uint8
{
	P0,
	P1,
	P2,
	P3
};

/** 시작 조건이 아닌 분류용 메타데이터. CHAIN은 완료 후속을 나타낸다. */
UENUM(BlueprintType)
enum class EOutlierEventTriggerType : uint8
{
	None,
	AREA,
	INTERACT,
	STATE,
	SEQUENCE,
	CHAIN,
	TIME
};

UENUM(BlueprintType)
enum class EOutlierEventEndCondition : uint8
{
	Manual,
	Time,
	Overlap
};

UENUM(BlueprintType)
enum class EOutlierUpperInfoType : uint8
{
	Goal,
	Input
};

/** Row Name은 EventTag와 같아야 한다. 빈 참조 태그는 해당 UI 영역을 끈다. */
USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierEventDataRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event", meta = (Categories = "Event"))
	FGameplayTag EventTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event", meta = (Categories = "Audio.Context"))
	FGameplayTag TTSTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event", meta = (Categories = "UI.Info.Upper"))
	FGameplayTag InputTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event", meta = (Categories = "UI.Info.Upper"))
	FGameplayTag GoalTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	FGameplayTag GifTag;

	/** 공유 월드 가이드 DT의 선택적 참조. 기존 Event CSV도 임포트 가능하다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event", meta = (Categories = "UI.Info.World", DataTableImportOptional))
	FGameplayTag WorldGuideID;
};

/** Row Name은 기존 Voice 오디오 뱅크의 컨텍스트 태그인 TTSTag와 같아야 한다. */
USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierTTSDataRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TTS", meta = (Categories = "Audio.Context"))
	FGameplayTag TTSTag;

	/** 테이블 값을 그대로 쓰는 표시 이름. 예: Partner, Genesis. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TTS")
	FString Speaker;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TTS")
	EOutlierEventTriggerType TriggerType = EOutlierEventTriggerType::None;

	/** CHAIN: 이 태그의 TTS 완료 후 재생한다. TTS는 TTS에서만 이어진다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TTS", meta = (Categories = "Audio.Context", EditCondition = "TriggerType == EOutlierEventTriggerType::CHAIN"))
	FGameplayTag ChainFromTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TTS", meta = (MultiLine = "true"))
	FText Script;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TTS")
	EOutlierTTSPriority Priority = EOutlierTTSPriority::P1;
};

/** 역할별 재사용 입력 표현. Row Name은 InputTag와 같아야 한다. */
USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierInputDisplayDataRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input Display", meta = (Categories = "UI.Info"))
	FGameplayTag InputTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input Display")
	FInputDisplayDefinition Display;
};

/** 튜토리얼 텍스트와 수명. InputTag는 별도의 역할 InputDisplay DT도 참조한다. */
USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierInputDataRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input UI", meta = (Categories = "UI.Info.Upper"))
	FGameplayTag InputTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input UI")
	EOutlierEventTriggerType TriggerType = EOutlierEventTriggerType::None;

	/** CHAIN: 이 태그의 Input/Goal 행 완료 후 표시한다. Upper는 Upper에서만 이어진다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input UI", meta = (Categories = "UI.Info.Upper"))
	FGameplayTag ChainFromTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input UI", meta = (MultiLine = "true"))
	FText Text;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input UI")
	EOutlierEventEndCondition EndCondition = EOutlierEventEndCondition::Time;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input UI", meta = (ClampMin = "0.001", EditCondition = "EndCondition == EOutlierEventEndCondition::Time"))
	float Duration = 3.0f;
};

/** 공유 목표/알림. Row Name은 GoalTag와 같아야 한다. */
USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierGoalDataRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Goal UI", meta = (Categories = "UI.Info.Upper"))
	FGameplayTag GoalTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Goal UI")
	EOutlierEventTriggerType TriggerType = EOutlierEventTriggerType::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Goal UI", meta = (Categories = "UI.Info.Upper"))
	FGameplayTag ChainFromTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Goal UI", meta = (MultiLine = "true"))
	FText Text;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Goal UI")
	EOutlierEventEndCondition EndCondition = EOutlierEventEndCondition::Time;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Goal UI", meta = (ClampMin = "0.001", EditCondition = "EndCondition == EOutlierEventEndCondition::Time"))
	float Duration = 3.0f;
};

/** 해석된 표시 데이터일 뿐, DataTable 행 구조체가 아니다. */
USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierUpperData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upper UI", meta = (Categories = "UI.Info.Upper"))
	FGameplayTag UpperTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upper UI")
	EOutlierEventTriggerType TriggerType = EOutlierEventTriggerType::None;

	/** 체인은 이 사본이 아닌 테이블 행에서 이벤트 서브시스템이 해석한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upper UI", meta = (Categories = "UI.Info.Upper", EditCondition = "TriggerType == EOutlierEventTriggerType::CHAIN"))
	FGameplayTag ChainFromTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upper UI")
	EOutlierUpperInfoType Type = EOutlierUpperInfoType::Goal;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upper UI", meta = (MultiLine = "true"))
	FText Text;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upper UI", meta = (EditCondition = "Type == EOutlierUpperInfoType::Input", EditConditionHides))
	FInputDisplayDefinition InputDisplay;

	/** 선택된 Input/Goal DT 행에서 해석한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upper UI")
	EOutlierEventEndCondition EndCondition = EOutlierEventEndCondition::Time;

	/** Context.Duration이 0인 Time 요청에 쓴다. Manual/Overlap은 무시한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upper UI", meta = (ClampMin = "0.001"))
	float Duration = 3.0f;
};

/** Row Name은 Tag와 같아야 한다. GIF 메타데이터와 표시 수명은 따로 설정한다. */
USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierGifDataRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GIF UI")
	FGameplayTag Tag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GIF UI")
	FText Subject;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GIF UI")
	FName GifID;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GIF UI", meta = (MultiLine = "true"))
	FText Description;

	/** 기존 행이 이전 표시 수명을 유지하도록 기본값은 Time이다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GIF UI")
	EOutlierEventEndCondition EndCondition = EOutlierEventEndCondition::Time;

	/** 요청 duration이 명시되지 않은 Time 요청은 이 값을 쓴다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GIF UI", meta = (ClampMin = "0.001", EditCondition = "EndCondition == EOutlierEventEndCondition::Time"))
	float Time = 3.0f;

	/** 이 역할 InputDisplay DT의 선택적 참조. 튜토리얼 프롬프트와 공유한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GIF UI")
	FGameplayTag InputTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GIF UI", meta = (DisplayName = "Subject Icon Exist"))
	bool bSubjectIconExist = false;

	/** Subject Icon Exist가 켜졌을 때만 subject 옆에 표시한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GIF UI", meta = (EditCondition = "bSubjectIconExist"))
	TObjectPtr<UTexture2D> SubjectIconTexture = nullptr;

	/** 입력 키의 선택적 캡션. GIF 설명과 별개이다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GIF UI", meta = (MultiLine = "true", DisplayName = "Input Display Desc"))
	FText InputDisplayDesc;

	/** 오른쪽 영역 체인: 이 Tag의 GIF 완료 후 표시한다. GIF는 GIF에서만 이어진다. */
	UPROPERTY(EditAnywhere, Category = "GIF UI")
	FGameplayTag ChainFromTag;
};
