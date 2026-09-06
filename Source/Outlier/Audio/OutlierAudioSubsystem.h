#pragma once

#include "CoreMinimal.h"
#include "Audio/OutlierAudioTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "OutlierAudioSubsystem.generated.h"

struct FStreamableHandle;
class USoundBase;
class UAudioComponent;
class AActor;
class AFirstPersonPlayerController;

/**
 * Resolves an audio play request (Type + Context) against Audio Bank content, loads the
 * selected sound asynchronously, and plays it in the current game world.
 *
 * Query/load unit is the Audio Bank (Type). Each Bank is discovered cheaply at boot
 * (metadata only); Banks flagged bLoadImmediately have their Definitions/Variants loaded
 * right away, the rest are loaded on first play request for that Type.
 */
UCLASS()
class OUTLIER_API UOutlierAudioSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Restarts Bank discovery and (re)loads every eager Bank. Asynchronous — use IsCatalogReady() to poll. */
	UFUNCTION(BlueprintCallable, Category = "Outlier|Audio")
	bool ReloadCatalog();

	UFUNCTION(BlueprintPure, Category = "Outlier|Audio")
	bool IsCatalogReady() const { return CatalogEntriesByType.Num() > 0; }

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

	UFUNCTION(BlueprintCallable, Category = "Outlier|Audio|Settings")
	void SetVolumeMultiplier(EOutlierAudioVolumeType VolumeType, float NewMultiplier);

	UFUNCTION(BlueprintPure, Category = "Outlier|Audio|Settings")
	float GetVolumeMultiplier(EOutlierAudioVolumeType VolumeType) const;

	UFUNCTION(BlueprintCallable, Category = "Outlier|Audio|Playback")
	void StopAllLocalAudio();

private:
	struct FRuntimeCatalogEntry
	{
		FString SourceName;
		FGameplayTag RequiredContext; // Invalid 면 기본값( 아무 Context 도 안 맞을 때의 fallback ) 후보
		TSoftObjectPtr<USoundBase> Sound;
		int32 VariantIndex = INDEX_NONE; // Bank 안에서 flatten 된 고유 인덱스 ( 네트워크 재생 해석용 )
		float Weight = 1.0f;
		float VolumeMultiplier = 1.0f;
		float PitchMultiplier = 1.0f;
		EOutlierAudioVolumeType VolumeType = EOutlierAudioVolumeType::SFX;
	};

	struct FActiveAudioPlayback
	{
		TWeakObjectPtr<UAudioComponent> Component;
		float BaseVolumeMultiplier = 1.0f;
		EOutlierAudioVolumeType VolumeType = EOutlierAudioVolumeType::SFX;
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
		EOutlierAudioVolumeType VolumeType = EOutlierAudioVolumeType::SFX;
	};

	// Bank 발견 결과. bLoadImmediately/BankId 는 Bank 메타데이터가 로드되는 즉시 알 수 있다 (가벼움).
	// bContentLoaded 는 그 Bank 의 Definitions/Variants 까지 실제로 카탈로그에 반영됐는지.
	struct FDiscoveredBank
	{
		FPrimaryAssetId BankId;
		bool bLoadImmediately = false;
		bool bContentLoaded = false;
	};

	const FRuntimeCatalogEntry* ResolveBestEntry(
		FGameplayTag TypeTag,
		const FGameplayTagContainer& ContextTags) const;
	const FRuntimeCatalogEntry* FindResolvedEntry(
		FGameplayTag TypeTag,
		int32 VariantIndex) const;
	bool PlayAudio(
		const FOutlierAudioPlayRequest& Request,
		const FOutlierAudioExecutionPolicy& Policy);

	bool BuildResolvedPlay(
		FGameplayTag TypeTag,
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
	void ExecutePlay(USoundBase* Sound, const FPendingPlay& PendingPlay);
	void TrackActiveAudioComponent(
		UAudioComponent* AudioComponent,
		float BaseVolumeMultiplier,
		EOutlierAudioVolumeType VolumeType);
	void RemoveInactiveAudioComponents();
	void RefreshActiveAudioComponentVolumes(EOutlierAudioVolumeType ChangedVolumeType);
	float GetCombinedVolumeMultiplier(EOutlierAudioVolumeType VolumeType) const;

	// ── Bank 발견 / 로드 ──────────────────────────────────────
	// GetPrimaryAssetIdList 로 Bank ID만 모으고( 가벼움 ), Bank DA 자체( 태그+플래그+참조 배열 )를
	// async 로 로드해 메타데이터를 읽는다. Definitions/Variants 는 이 시점엔 아직 안 당겨진다.
	void DiscoverAudioBanks();
	void HandleBankMetadataLoaded(TArray<FPrimaryAssetId> BankIds);
	// 특정 Type 의 Definitions 를 async 로 로드해 카탈로그에 반영한다. 이미 로드됐거나 로드 중이면 no-op.
	void LoadBankContent(FGameplayTag TypeTag);
	void HandleBankContentLoaded(FGameplayTag TypeTag);
	// Bank->Definitions 를 순회해 Variant 를 CatalogEntriesByType[Bank->TypeTag] 로 flatten 한다.
	void IngestBank(UOutlierAudioBank* Bank);
	// 콘텐츠 로드를 기다리며 큐잉돼있던 재생 요청을 재시도한다.
	void FlushPendingPlaysForType(FGameplayTag TypeTag);

	// Bank/Definition 오브젝트를 GC 로부터 지킨다 ( 서브시스템 생애 동안 상주 ).
	UPROPERTY(Transient)
	TArray<TObjectPtr<UOutlierAudioBank>> LoadedBanks;

	TMap<FGameplayTag /*TypeTag*/, TArray<FRuntimeCatalogEntry>> CatalogEntriesByType;
	TMap<FGameplayTag /*TypeTag*/, FDiscoveredBank> DiscoveredBanksByType;
	TMap<FGameplayTag /*TypeTag*/, TArray<TPair<FOutlierAudioPlayRequest, FOutlierAudioExecutionPolicy>>> PendingPlaysByType;
	TSharedPtr<FStreamableHandle> BankMetadataLoadHandle;
	TMap<FGameplayTag /*TypeTag*/, TSharedPtr<FStreamableHandle>> BankContentLoadHandles;

	TMap<FSoftObjectPath, TArray<FPendingPlay>> PendingPlaysBySound;
	TMap<FSoftObjectPath, TSharedPtr<FStreamableHandle>> ActiveLoadHandles;
	TMap<EOutlierAudioVolumeType, float> VolumeMultipliers;
	TArray<FActiveAudioPlayback> ActiveAudioPlaybacks;
};
