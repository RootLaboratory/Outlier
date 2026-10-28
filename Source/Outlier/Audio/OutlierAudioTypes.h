#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/NetSerialization.h"
#include "GameplayTagContainer.h"
#include "OutlierAudioTypes.generated.h"

class USoundBase;
class USoundDefinition;
class AActor;

UENUM(BlueprintType)
enum class EOutlierAudioPlaybackPolicy : uint8
{
	OneShot,
	Loop,
	StopLoopThenOneShot,
	StopLoop
};

UENUM(BlueprintType)
enum class EOutlierAudioVolumeType : uint8
{
	Master UMETA(DisplayName = "Master"),
	BGM UMETA(DisplayName = "BGM"),
	SFX UMETA(DisplayName = "SFX"),
	Voice UMETA(DisplayName = "Voice")
};

// Context 는 사운드 1개당 태그 1개로 매핑된다 ( 조합/AND 조건 없음 — 작업 단순화 ).
// 같은 RequiredContext 를 가진 Variant 가 여러 개면 Weight 비례 랜덤으로 하나가 선택된다.
// RequiredContext 가 비어있으면( Invalid ) 그 어떤 Context 로도 못 찾았을 때 쓰이는 기본값 후보가 된다.
USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierAudioVariant
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (Categories = "Audio.Context"))
	FGameplayTag RequiredContext;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (AssetBundles = "Audio"))
	TSoftObjectPtr<USoundBase> Sound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	/** 지속 재생 동작은 오디오가 관리하며, 게임플레이는 Context 태그만 전달한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	EOutlierAudioPlaybackPolicy PlaybackPolicy = EOutlierAudioPlaybackPolicy::OneShot;

	/**
	 * 이 Variant가 제어할 루프의 식별 태그. Loop는 비워 두면 RequiredContext를 사용한다.
	 * StopLoop와 StopLoopThenOneShot은 중단할 루프의 태그를 반드시 지정해야 한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (Categories = "Audio.Context"))
	FGameplayTag LoopContextTag;
};

UCLASS(BlueprintType)
class OUTLIER_API UOutlierAudioEventDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static const FPrimaryAssetType PrimaryAssetType;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (ClampMin = "0.0"))
	float VolumeMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (ClampMin = "0.0"))
	float PitchMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	EOutlierAudioVolumeType VolumeType = EOutlierAudioVolumeType::SFX;

	// Context 만으로 선택되는 사운드 묶음. 이 Definition 이 속한 Bank 의 Variant 풀에 합류한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TArray<FOutlierAudioVariant> Variants;
};

// Type(카테고리) 정체성 + 로드 정책 + 그 Type 에 속한 Definition 목록을 갖는 컨테이너.
// 재생 요청은 이 Bank 의 TypeTag 로 라우팅되고, Query/Load 단위도 이 Bank 다.
UCLASS(BlueprintType)
class OUTLIER_API UOutlierAudioBank : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static const FPrimaryAssetType PrimaryAssetType;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

	// Audio.Type.UI / Player / Enemy / Interactable / Environment / Weapon / BGM / Voice
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (Categories = "Audio.Type"))
	FGameplayTag TypeTag;

	// true 면 GameInstance 부팅 시 즉시 로드( UI/Player/Enemy/Interactable/Environment/Weapon 등 ).
	// false 면 ID 만 발견해두고, 이 Type 의 재생 요청이 처음 들어올 때 로드( BGM/Voice 등 ).
	// 에디터에서 카테고리별로 직접 판단해 체크하는 항목 — 코드에 하드코딩된 정책이 아니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	bool bLoadImmediately = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TArray<TSoftObjectPtr<UOutlierAudioEventDefinition>> Definitions;
};

/** C++ 전용 실행 규칙. 오디오 정의는 네트워크 전달 흐름을 관리하지 않는다. */
enum class EOutlierAudioPlaybackMode : uint8
{
	TwoD,
	AtLocation
};

enum class EOutlierAudioAudience : uint8
{
	Local,
	Owner,
	Relevant
};

enum class EOutlierAudioRequestAuthority : uint8
{
	Local,
	OwningClient,
	Server
};

struct OUTLIER_API FOutlierAudioExecutionPolicy
{
	EOutlierAudioPlaybackMode PlaybackMode = EOutlierAudioPlaybackMode::TwoD;
	EOutlierAudioAudience Audience = EOutlierAudioAudience::Local;
	EOutlierAudioRequestAuthority RequestAuthority = EOutlierAudioRequestAuthority::Local;
};

USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierAudioPlayRequest
{
	GENERATED_BODY()

	// 필드명은 유지하지만 값은 이제 UOutlierAudioBank::TypeTag ( Audio.Type.* ) 를 넣는다.
	// 구체 사운드 선택은 ContextTags 단독으로 처리된다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	FGameplayTag EventTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	FGameplayTagContainer ContextTags;

	/** 표면 요청만 사용한다. 순서대로 정확히 일치하는 Context를 찾고 일반 Bank fallback은 생략한다. */
	UPROPERTY()
	TArray<FGameplayTag> OrderedContexts;
	UPROPERTY()
	bool bExactContexts = false;

	UPROPERTY()
	TObjectPtr<USoundDefinition> LegacySound = nullptr;

	/** 비동기 Bank 대기 중 월드가 바뀐 요청을 폐기하기 위한 로컬 정보. */
	TWeakObjectPtr<UWorld> OriginWorld;

	/** 실제 사운드 재생 주체. 소유권 검증, 기본 위치, 네트워크 관련성 판단에 사용한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TObjectPtr<AActor> EmitterActor = nullptr;

	/** Audience가 Owner일 때의 수신 대상. 생략하면 EmitterActor를 기준으로 결정한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TObjectPtr<AActor> RecipientActor = nullptr;

	/** AtLocation 재생에 사용할 명시적인 월드 위치. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	FVector Location = FVector::ZeroVector;

	/** 월드 원점도 유효한 재생 위치이므로 위치 지정 여부를 별도로 보관한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	bool bHasLocation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio", meta = (ClampMin = "0.0"))
	float StartTime = 0.0f;

};

/** 서버에서 확정한 재생 데이터. 클라이언트는 가중치 선택을 다시 수행하지 않는다. */
USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierResolvedAudioPlay
{
	GENERATED_BODY()

	/** 기존 소유자 오디오 경로로 전달할 BGM Context. */
	UPROPERTY(BlueprintReadOnly, Category = "Audio")
	FGameplayTag MusicContextTag;

	// UOutlierAudioBank::TypeTag ( Audio.Type.* ). 필드명은 유지, 의미는 Type.
	UPROPERTY(BlueprintReadOnly, Category = "Audio")
	FGameplayTag EventTag;

	UPROPERTY(BlueprintReadOnly, Category = "Audio")
	int32 VariantIndex = INDEX_NONE;

	/** 서버가 태그 후보를 모두 확인한 경우에만 선택한 기존 Provider 사운드. */
	UPROPERTY()
	TObjectPtr<USoundDefinition> LegacySound = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Audio")
	bool bAtLocation = false;

	UPROPERTY(BlueprintReadOnly, Category = "Audio")
	FVector_NetQuantize Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Audio")
	float StartTime = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Audio")
	bool bLooping = false;

	/** 각 클라이언트에서 이 루프를 중단할 때 사용하는 서버 발급 식별자. */
	UPROPERTY(BlueprintReadOnly, Category = "Audio")
	int32 AudioInstanceId = 0;
};
