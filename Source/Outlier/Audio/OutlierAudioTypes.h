#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "OutlierAudioTypes.generated.h"

class USoundBase;
class AActor;

USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierAudioVariant
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (Categories = "Audio.Context"))
	FGameplayTagContainer RequiredContextTags;

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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (Categories = "Audio.Event"))
	FGameplayTag EventTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (ClampMin = "0.0"))
	float VolumeMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (ClampMin = "0.0"))
	float PitchMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TArray<FOutlierAudioVariant> Variants;
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
