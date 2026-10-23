#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Audio/OutlierAudioTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TimerManager.h"
#include "OutlierAudioSubsystem.generated.h"

struct FStreamableHandle;
class USoundBase;
class UAudioComponent;
class AActor;
class AFirstPersonPlayerController;
class ULocalPlayer;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnOutlierVoiceStarted, ULocalPlayer*, FGameplayTag);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnOutlierVoiceFinished, ULocalPlayer*, FGameplayTag, bool);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnOutlierVoiceUnavailable, ULocalPlayer*, FGameplayTag);

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

	/** 로컬 Voice 뱅크 직렬 재생. P0는 P2/P3를 중단할 수 있고, 나머지는 대기한다. */
	bool PlayLocalVoice(FGameplayTag SoundTag, int32 Priority, FGameplayTag RequestTag, ULocalPlayer* Listener);
	void StopLocalVoices(ULocalPlayer* Listener);
	/** Voice 오디오 컴포넌트 재생 시작 시 발생한다. RequestTag는 호출자의 EventTag이다. */
	FOnOutlierVoiceStarted OnVoiceStarted;
	/** 자연 종료 시 bCompleted=true, 중단/명시적 정지/재생 실패 시 false. */
	FOnOutlierVoiceFinished OnVoiceFinished;
	/** 음성을 아예 재생하지 못했을 때만(variant 없음, 로드/스폰 실패) OnVoiceFinished(false) 뒤에 발생한다. */
	FOnOutlierVoiceUnavailable OnVoiceUnavailable;

	/**
	 * Local-only 3D playback, for callers that already execute on every client and must not
	 * route across the network again — GameplayCue notifies in particular.
	 *
	 * A cue is already multicast by the ASC and runs locally on each client, so calling one of
	 * the Relevant* entry points from inside a notify would propagate a second time.
	 *
	 * Note the trade-off against the Relevant* path: the server does not resolve the variant
	 * here, so each client rolls its own. Use a Relevant* entry point when every player must
	 * hear the same variant.
	 */
	bool PlayLocalAtLocation(const FOutlierAudioPlayRequest& Request);

	/** Server-authoritative 2D playback delivered to one owning player. */
	bool PlayOwner2DFromServer(const FOutlierAudioPlayRequest& Request);

	/** Gameplay submits only Type + Context. Playback lifetime is owned by the audio subsystem. */
	static bool PlayTaggedAtLocationFromServer(
		AActor* EmitterActor,
		FGameplayTag TypeTag,
		FGameplayTag ContextTag);

	/** Stops the persistent playback identified by the emitter and context. */
	static bool StopTaggedAtLocationFromServer(
		AActor* EmitterActor,
		FGameplayTag TypeTag,
		FGameplayTag ContextTag);

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

	/** Stops and releases one locally playing loop instance. */
	bool StopLoopAudioLocally(int32 AudioInstanceId, float FadeOutDuration = 0.0f);

	UFUNCTION(BlueprintCallable, Category = "Outlier|Audio|Settings")
	void SetVolumeMultiplier(EOutlierAudioVolumeType VolumeType, float NewMultiplier);

	UFUNCTION(BlueprintPure, Category = "Outlier|Audio|Settings")
	float GetVolumeMultiplier(EOutlierAudioVolumeType VolumeType) const;

	/** Stops current playback and discards all queued plays; future UI requests remain usable. */
	UFUNCTION(BlueprintCallable, Category = "Outlier|Audio|Playback")
	void StopAllLocalAudio();
	/** Menu entry stops transient feedback without discarding gameplay-owned desired loops. */
	void StopTransientLocalAudio();
	void StartFrontendMusic();
	void EnterGameplayMusic();

// Desired local loop state. The subsystem owns handles, retries and emitter attachment.
	void SetLocalEmitterLoop(AActor* Emitter, FGameplayTag Type, FGameplayTag Context, bool bEnabled,
		bool bTwoD = false, float FadeOutDuration = 0.0f);

	/**
	 * Ambient Volume 전용 BGM 채널. 기존 MusicComponent( 프론트엔드/전투 )와는 별개로 동시에 재생된다.
	 * 가장 마지막에 들어간 영역의 Context 한 곡만 키우고 나머지는 줄인다. 줄어들던 곡으로 돌아오면
	 * 새로 재생하지 않고 그 자리에서 다시 키운다.
	 */
	void SetZoneMusic(const UObject* Requester, FGameplayTag Context, bool bEnabled,
		float FadeInDuration, float FadeOutDuration);

	/**
	 * Context 를 가진 Bank 의 TypeTag 를 찾는다. 아직 내용이 안 올라온 Bank 가 남아 있으면 그 로드를 걸고
	 * bOutPending = true 로 돌려준다( 잠시 뒤 다시 호출 ). 모든 Bank 를 봤는데도 없으면 false + bOutPending = false.
	 */
	bool ResolveTypeForContext(FGameplayTag Context, FGameplayTag& OutType, bool& bOutPending);
	void StopLocalEmitterLoops(AActor* Emitter);
	void BindMenuButtons(class UUserWidget* Widget);
	uint64 MenuAudioSerial = 0;
	uint64 LastMenuAudioFrame = MAX_uint64;
	void MarkMenuAction() { ++MenuAudioSerial; LastMenuAudioFrame = GFrameCounter; }

private:
	void HandleAudioWorldTick(UWorld* World, ELevelTick TickType, float DeltaTime);
	void SyncAudioPause(UAudioComponent* Component);
	FDelegateHandle AudioWorldTickHandle;
	TSet<TWeakObjectPtr<UAudioComponent>> WorldPausedAudioComponents;

	bool bStoppingAllLocalAudio = false;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> MusicComponent;
	FGameplayTag DesiredMusicContext;
	TSharedPtr<FStreamableHandle> MusicLoadHandle;
	FTSTicker::FDelegateHandle MusicTickerHandle;
	uint64 MusicRevision = 0;
	bool bFrontendMusic = false;
	bool bCombatMusic = false;
	float MusicBaseVolume = 1.0f;
	FGameplayTag ResolveMusicContext(const FOutlierAudioPlayRequest& Request) const;
	void HandleMusicContext(FGameplayTag Context);
	void SetCombatMusicActive(bool bActive, bool bPlayEnd = true);
	void RequestMusic(FGameplayTag Context);
	void StopMusicPlayback();
	bool TickMusic(float DeltaTime);
	void UpdateMusicVolume();
	UFUNCTION() void HandleMusicFinished();

	struct FZoneMusicRequest
	{
		TWeakObjectPtr<const UObject> Requester;
		FGameplayTag Context;
		float FadeInDuration = 0.0f;
		float FadeOutDuration = 0.0f;
	};

	struct FZoneMusicTrack
	{
		TWeakObjectPtr<UAudioComponent> Component;
		FGameplayTag Context;
		float BaseVolume = 1.0f;
		EOutlierAudioVolumeType VolumeType = EOutlierAudioVolumeType::BGM;
		float FadeAlpha = 0.0f;
		float TargetAlpha = 1.0f;
		float FadeInDuration = 0.0f;
		float FadeOutDuration = 0.0f;
	};

	TArray<FZoneMusicRequest> ZoneMusicRequests; // 마지막 원소가 지금 재생할 곡
	TArray<FZoneMusicTrack> ZoneMusicTracks;
	TSharedPtr<FStreamableHandle> ZoneMusicLoadHandle;
	FGameplayTag ZoneMusicLoadingContext;
	TWeakObjectPtr<UWorld> ZoneMusicWorld;
	FTSTicker::FDelegateHandle ZoneMusicTickerHandle;
	const FZoneMusicRequest* RefreshZoneMusicTargets();
	void TryStartZoneMusicTrack(const FZoneMusicRequest& Request);
	bool TickZoneMusic(float DeltaTime);
	void ApplyZoneMusicVolume(const FZoneMusicTrack& Track) const;
	void UpdateZoneMusicVolumes();
	void ClearZoneMusic();
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
		EOutlierAudioPlaybackPolicy PlaybackPolicy = EOutlierAudioPlaybackPolicy::OneShot;
		FGameplayTag LoopContextTag;
	};

	struct FActiveAudioPlayback
	{
		bool bPlayWhenPaused = false;
		TWeakObjectPtr<UAudioComponent> Component;
		float BaseVolumeMultiplier = 1.0f;
		EOutlierAudioVolumeType VolumeType = EOutlierAudioVolumeType::SFX;
		int32 AudioInstanceId = 0;
		bool bLooping = false;
	};

	struct FPendingPlay
	{
		bool bPlayWhenPaused = false;
		TWeakObjectPtr<UWorld> World;
		FString SourceName;
		bool bAtLocation = false;
		FVector Location = FVector::ZeroVector;
		float VolumeMultiplier = 1.0f;
		float PitchMultiplier = 1.0f;
		float StartTime = 0.0f;
		EOutlierAudioVolumeType VolumeType = EOutlierAudioVolumeType::SFX;
		int32 AudioInstanceId = 0;
		bool bLooping = false;
		TWeakObjectPtr<AActor> AttachedEmitter;
		bool bAttachToEmitter = false;
		int32 VoiceRequestId = 0;
	};

	struct FVoiceRequest
	{
		int32 Id = 0;
		FGameplayTag SoundTag;
		FGameplayTag RequestTag;
		TWeakObjectPtr<ULocalPlayer> Listener;
		TWeakObjectPtr<UWorld> World;
		int32 Priority = 1;
	};

	void StartNextVoice();
	void FinishActiveVoice(bool bCompleted);
	/** 중단이 아닌 재생 실패. 활성 음성을 종료한 뒤 사용 불가로 알린다. */
	void FailActiveVoice();
	void HandleVoiceAudioFinished(UAudioComponent* AudioComponent);
	void HandleVoiceWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	TArray<FVoiceRequest> VoiceQueue;
	FVoiceRequest ActiveVoice;
	TWeakObjectPtr<UAudioComponent> VoiceAudioComponent;
	FDelegateHandle VoiceWorldCleanupHandle;
	int32 NextVoiceRequestId = 1;
	bool bStartingVoice = false;

	// Bank 발견 결과. bLoadImmediately/BankId 는 Bank 메타데이터가 로드되는 즉시 알 수 있다 (가벼움).
	// bContentLoaded 는 그 Bank 의 Definitions/Variants 까지 실제로 카탈로그에 반영됐는지.
	struct FDiscoveredBank
	{
		FPrimaryAssetId BankId;
		bool bLoadImmediately = false;
		bool bContentLoaded = false;
	};

	struct FLoopPlaybackKey
	{
		TWeakObjectPtr<AActor> EmitterActor;
		FGameplayTag LoopContextTag;

		bool operator==(const FLoopPlaybackKey& Other) const
		{
			return EmitterActor == Other.EmitterActor
				&& LoopContextTag == Other.LoopContextTag;
		}

		friend uint32 GetTypeHash(const FLoopPlaybackKey& Key)
		{
			return HashCombine(GetTypeHash(Key.EmitterActor), GetTypeHash(Key.LoopContextTag));
		}
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
	bool StopLoopAtLocationFromServer(int32 AudioInstanceId);

	bool BuildResolvedPlay(
		FGameplayTag TypeTag,
		const FRuntimeCatalogEntry& Entry,
		const FOutlierAudioPlayRequest& Request,
		EOutlierAudioPlaybackMode PlaybackMode,
		int32 AudioInstanceId,
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
		EOutlierAudioVolumeType VolumeType,
		int32 AudioInstanceId = 0,
		bool bLooping = false,
		bool bPlayWhenPaused = false);
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
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAudioComponent>> LoopAudioComponentPool;
	TSet<int32> CancelledLoopAudioInstances;
	TMap<FLoopPlaybackKey, int32> LoopInstances;

	struct FLocalEmitterLoop
	{
		FGameplayTag Type;
		int32 InstanceId = 0;
		bool bTwoD = false;
	};
	UPROPERTY(Transient)
	TArray<TObjectPtr<class UOutlierMenuAudioBinding>> MenuAudioBindings;
	TMap<FLoopPlaybackKey, FLocalEmitterLoop> LocalEmitterLoops;
	FTimerHandle LocalLoopTimer;
	TWeakObjectPtr<UWorld> LocalLoopWorld;
	int32 NextLocalLoopId = -1; // Network instance IDs are positive.
	void RefreshLocalEmitterLoops();
	void ClearLocalEmitterLoops();
	int32 NextAudioInstanceId = 1;
};
