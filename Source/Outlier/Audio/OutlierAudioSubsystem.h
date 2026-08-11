#pragma once

#include "CoreMinimal.h"
#include "Audio/OutlierAudioTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "OutlierAudioSubsystem.generated.h"

struct FStreamableHandle;
class USoundBase;

/**
 * Resolves an audio event and its gameplay context to a catalog row, loads the
 * selected sound asynchronously, and plays it in the current game world.
 */
UCLASS()
class OUTLIER_API UOutlierAudioSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Rebuilds the runtime index from Audio Event Definition primary assets. */
	UFUNCTION(BlueprintCallable, Category = "Outlier|Audio")
	bool ReloadCatalog();

	UFUNCTION(BlueprintPure, Category = "Outlier|Audio")
	bool IsCatalogReady() const { return CatalogEntriesByEvent.Num() > 0; }

	/** Resolves, loads, and plays a non-spatialized sound. */
	UFUNCTION(BlueprintCallable, Category = "Outlier|Audio")
	bool PlayAudio2D(const FOutlierAudioPlayRequest& Request);

	/** Resolves, loads, and plays a sound at a world location. */
	UFUNCTION(BlueprintCallable, Category = "Outlier|Audio")
	bool PlayAudioAtLocation(const FOutlierAudioPlayRequest& Request, FVector Location);

private:
	struct FRuntimeCatalogEntry
	{
		FString SourceName;
		FGameplayTagContainer RequiredContextTags;
		TSoftObjectPtr<USoundBase> Sound;
		float Weight = 1.0f;
	};

	struct FPendingPlay
	{
		TWeakObjectPtr<UWorld> World;
		FString SourceName;
		bool bAtLocation = false;
		FVector Location = FVector::ZeroVector;
		float VolumeMultiplier = 1.0f;
		float PitchMultiplier = 1.0f;
		float StartTime = 0.0f;
	};

	const FRuntimeCatalogEntry* ResolveBestEntry(
		FGameplayTag EventTag,
		const FGameplayTagContainer& ContextTags) const;

	bool QueueOrPlay(const FRuntimeCatalogEntry& Entry, const FPendingPlay& PendingPlay);
	void HandleSoundLoaded(FSoftObjectPath SoundPath);
	void ExecutePlay(USoundBase* Sound, const FPendingPlay& PendingPlay) const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UOutlierAudioEventDefinition>> LoadedDefinitions;

	TMap<FGameplayTag, TArray<FRuntimeCatalogEntry>> CatalogEntriesByEvent;
	TMap<FSoftObjectPath, TArray<FPendingPlay>> PendingPlaysBySound;
	TMap<FSoftObjectPath, TSharedPtr<FStreamableHandle>> ActiveLoadHandles;
};
