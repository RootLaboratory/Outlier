#pragma once

#include "CoreMinimal.h"
#include "Audio/OutlierAudioTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "OutlierAudioSubsystem.generated.h"

struct FStreamableHandle;
class USoundBase;
class AActor;
class AFirstPersonPlayerController;

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

	/** Local-only 2D playback. Suitable for input and UI feedback. */
	bool PlayLocal2D(const FOutlierAudioPlayRequest& Request);

	/** Server-authoritative 2D playback delivered to one owning player. */
	bool PlayOwner2DFromServer(const FOutlierAudioPlayRequest& Request);

	/** Relevant world playback that an owning client may request through its controller. */
	bool PlayRelevantAtLocationFromOwningClient(const FOutlierAudioPlayRequest& Request);

	/** Relevant world playback that must originate from server gameplay. */
	bool PlayRelevantAtLocationFromServer(const FOutlierAudioPlayRequest& Request);

	/** Called only by the owning PlayerController's world-audio Server RPC. */
	bool HandleServerRelevantAtLocationRequest(
		AFirstPersonPlayerController* RequestingController,
		const FOutlierAudioPlayRequest& Request);

	/** RPC/GAS delivery endpoint. Never routes across the network again. */
	bool PlayResolvedAudioLocally(const FOutlierResolvedAudioPlay& ResolvedPlay);

private:
	struct FRuntimeCatalogEntry
	{
		FString SourceName;
		FGameplayTagContainer RequiredContextTags;
		TSoftObjectPtr<USoundBase> Sound;
		int32 VariantIndex = INDEX_NONE;
		float Weight = 1.0f;
		float VolumeMultiplier = 1.0f;
		float PitchMultiplier = 1.0f;
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
	const FRuntimeCatalogEntry* FindResolvedEntry(
		FGameplayTag EventTag,
		int32 VariantIndex) const;
	bool PlayAudio(
		const FOutlierAudioPlayRequest& Request,
		const FOutlierAudioExecutionPolicy& Policy);

	bool BuildResolvedPlay(
		FGameplayTag EventTag,
		const FRuntimeCatalogEntry& Entry,
		const FOutlierAudioPlayRequest& Request,
		EOutlierAudioPlaybackMode PlaybackMode,
		FOutlierResolvedAudioPlay& OutResolvedPlay) const;
	bool RouteByAudience(
		const FOutlierAudioPlayRequest& Request,
		const FOutlierResolvedAudioPlay& ResolvedPlay,
		EOutlierAudioAudience Audience);
	bool RouteOwner(
		const FOutlierAudioPlayRequest& Request,
		const FOutlierResolvedAudioPlay& ResolvedPlay);
	bool RouteRelevant(
		AActor* EmitterActor,
		const FOutlierResolvedAudioPlay& ResolvedPlay);
	AFirstPersonPlayerController* ResolveOwningPlayerController(AActor* Actor) const;
	AFirstPersonPlayerController* ResolveLocalRequestController(AActor* EmitterActor) const;
	bool IsEmitterOwnedByController(
		const AActor* EmitterActor,
		const AFirstPersonPlayerController* Controller) const;

	bool QueueOrPlay(const FRuntimeCatalogEntry& Entry, const FPendingPlay& PendingPlay);
	void HandleSoundLoaded(FSoftObjectPath SoundPath);
	void ExecutePlay(USoundBase* Sound, const FPendingPlay& PendingPlay) const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UOutlierAudioEventDefinition>> LoadedDefinitions;

	TMap<FGameplayTag, TArray<FRuntimeCatalogEntry>> CatalogEntriesByEvent;
	TMap<FSoftObjectPath, TArray<FPendingPlay>> PendingPlaysBySound;
	TMap<FSoftObjectPath, TSharedPtr<FStreamableHandle>> ActiveLoadHandles;
};
