#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "OutlierAudioTypes.generated.h"

class USoundBase;
class AActor;

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

/** Native-only execution rules. Audio definitions intentionally do not own network flow. */
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

	/** Actual sound emitter. Used for ownership validation, default location, and relevancy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TObjectPtr<AActor> EmitterActor = nullptr;

	/** Target listener when Audience is Owner. Falls back to EmitterActor when omitted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TObjectPtr<AActor> RecipientActor = nullptr;

	/** Explicit world location used by AtLocation playback. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	FVector Location = FVector::ZeroVector;

	/** Kept separate because the world origin is a valid audio location. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	bool bHasLocation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio", meta = (ClampMin = "0.0"))
	float StartTime = 0.0f;
};

/** Server-resolved playback data. Clients do not run weighted selection again. */
USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierResolvedAudioPlay
{
	GENERATED_BODY()

	// UOutlierAudioBank::TypeTag ( Audio.Type.* ). 필드명은 유지, 의미는 Type.
	UPROPERTY(BlueprintReadOnly, Category = "Audio")
	FGameplayTag EventTag;

	UPROPERTY(BlueprintReadOnly, Category = "Audio")
	int32 VariantIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Audio")
	bool bAtLocation = false;

	UPROPERTY(BlueprintReadOnly, Category = "Audio")
	FVector_NetQuantize Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Audio")
	float StartTime = 0.0f;
};
