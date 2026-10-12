#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "OutlierEventTypes.generated.h"

class UInputAction;
class UTexture2D;

UENUM(BlueprintType)
enum class EOutlierTTSPriority : uint8
{
	P0,
	P1,
	P2,
	P3
};

/** Classification metadata, not a start gate. CHAIN identifies completion continuations. */
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

/** Runtime upper UI lifetime. A canceled UI never starts its next event. */
USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierEventContext
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event")
	EOutlierEventEndCondition EndCondition = EOutlierEventEndCondition::Manual;

	/** Zero uses the Input/Goal DT's Duration. A positive value explicitly overrides it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event", meta = (ClampMin = "0.0", EditCondition = "EndCondition == EOutlierEventEndCondition::Time"))
	float Duration = 0.0f;

	/** Fired after the upper UI completes; not after its accompanying voice finishes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event", meta = (Categories = "Event"))
	FGameplayTag NextEventTag;
};

/** Row Name must equal EventTag. Empty reference tags disable their respective UI regions. */
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
};

/** Row Name must equal TTSTag, the context tag used by the existing Voice audio bank. */
USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierTTSDataRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TTS", meta = (Categories = "Audio.Context"))
	FGameplayTag TTSTag;

	/** Display name taken verbatim from the table, e.g. Partner or Genesis. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TTS")
	FString Speaker;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TTS")
	EOutlierEventTriggerType TriggerType = EOutlierEventTriggerType::None;

	/** CHAIN: plays after the TTS with this tag completes. TTS only chains from TTS. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TTS", meta = (Categories = "Audio.Context", EditCondition = "TriggerType == EOutlierEventTriggerType::CHAIN"))
	FGameplayTag ChainFromTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TTS", meta = (MultiLine = "true"))
	FText Script;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TTS")
	EOutlierTTSPriority Priority = EOutlierTTSPriority::P1;
};

/** Role-specific input table. Row Name must equal InputTag; the table selects the role. */
USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierInputDataRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input UI", meta = (Categories = "UI.Info.Upper"))
	FGameplayTag InputTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input UI")
	EOutlierEventTriggerType TriggerType = EOutlierEventTriggerType::None;

	/** CHAIN: shown after the Input/Goal row with this tag completes. Upper only chains from Upper. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input UI", meta = (Categories = "UI.Info.Upper"))
	FGameplayTag ChainFromTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input UI", meta = (MultiLine = "true"))
	FText Text;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input UI")
	TObjectPtr<UInputAction> InputAction = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input UI", meta = (ClampMin = "0.001"))
	float Duration = 3.0f;
};

/** Shared goals/notices. Row Name must equal GoalTag. */
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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Goal UI", meta = (ClampMin = "0.001"))
	float Duration = 3.0f;
};

/** Resolved presentation data only; this is not a DataTable row structure. */
USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierUpperData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upper UI", meta = (Categories = "UI.Info.Upper"))
	FGameplayTag UpperTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upper UI")
	EOutlierEventTriggerType TriggerType = EOutlierEventTriggerType::None;

	/** Chains are resolved by the event subsystem from the table rows, not from this copy. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upper UI", meta = (Categories = "UI.Info.Upper", EditCondition = "TriggerType == EOutlierEventTriggerType::CHAIN"))
	FGameplayTag ChainFromTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upper UI")
	EOutlierUpperInfoType Type = EOutlierUpperInfoType::Goal;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upper UI", meta = (MultiLine = "true"))
	FText Text;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upper UI", meta = (EditCondition = "Type == EOutlierUpperInfoType::Input", EditConditionHides))
	TObjectPtr<UInputAction> InputAction = nullptr;

	/** Used for Time requests when Context.Duration is zero. Manual/Overlap ignore this value. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upper UI", meta = (ClampMin = "0.001"))
	float Duration = 3.0f;
};

/** Row Name must equal Tag. GIF metadata and its display lifetime are configured independently. */
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

	/** Time requests use this value when no explicit request duration is supplied. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GIF UI", meta = (ClampMin = "0.001"))
	float Time = 3.0f;

	/** Optional action whose current rebound key is displayed by the Right widget. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GIF UI")
	TObjectPtr<UInputAction> InputAction = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GIF UI", meta = (DisplayName = "Subject Icon Exist"))
	bool bSubjectIconExist = false;

	/** Displayed beside the subject only when Subject Icon Exist is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GIF UI", meta = (EditCondition = "bSubjectIconExist"))
	TObjectPtr<UTexture2D> SubjectIconTexture = nullptr;

	/** Optional caption for the input key, independent of the GIF description. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GIF UI", meta = (MultiLine = "true", DisplayName = "Input Display Desc"))
	FText InputDisplayDesc;

	/** Right-region chain: shown after the GIF with this Tag completes. GIFs only chain from GIFs. */
	UPROPERTY(EditAnywhere, Category = "GIF UI")
	FGameplayTag ChainFromTag;
};
