#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Audio/OutlierAudioTypes.h"
#include "Audio/OutlierSurfaceAudioSettings.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TimerManager.h"
#include "OutlierAudioSubsystem.generated.h"

struct FStreamableHandle;
class USoundBase;
class UAudioComponent;
class AActor;
class AFirstPersonPlayerController;
class ULocalPlayer;
class USkeletalMeshComponent;
class UVisualEventSubsystem;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnOutlierVoiceStarted, ULocalPlayer*, FGameplayTag);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnOutlierVoiceFinished, ULocalPlayer*, FGameplayTag, bool);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnOutlierVoiceUnavailable, ULocalPlayer*, FGameplayTag);

/** Ready는 음원 객체의 로드 완료다. 스트리밍 청크의 준비 완료까지 보장하지 않는다. */
enum class EOutlierMusicPreloadState : uint8
{
	Waiting,
	Loading,
	Ready,
	Failed,
	Released
};

/**
 * 오디오 재생 요청(Type + Context)에 맞는 사운드를 Audio Bank에서 선택하고,
 * 비동기로 로드한 뒤 현재 게임 월드에서 재생한다.
 *
 * 조회와 로드는 Audio Bank(Type) 단위로 처리한다. 시작 시에는 메타데이터만 조회하고,
 * bLoadImmediately가 설정된 Bank의 Definitions/Variants만 즉시 로드한다.
 * 나머지는 해당 Type의 첫 재생 요청 시 로드한다.
 */
UCLASS()
class OUTLIER_API UOutlierAudioSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Bank를 다시 검색하고 즉시 로드 대상 Bank를 로드한다. 비동기 완료 여부는 IsCatalogReady()로 확인한다. */
	UFUNCTION(BlueprintCallable, Category = "Outlier|Audio")
	bool ReloadCatalog();

	UFUNCTION(BlueprintPure, Category = "Outlier|Audio")
	bool IsCatalogReady() const { return CatalogEntriesByType.Num() > 0; }

	/** 월드 수명에 묶인 BGM 선로드 요청. 반환값 0은 요청 거절이며, 호출자가 해제 책임을 가진다. */
	uint64 RequestMusicPreload(UWorld* World, const TArray<FGameplayTag>& Contexts);
	/** 알 수 없거나 카탈로그 재로드/월드 정리로 해제된 요청은 Released를 반환한다. */
	EOutlierMusicPreloadState GetMusicPreloadState(uint64 RequestId) const;
	/** 마지막 선로드 사용자의 참조만 해제하며 현재 음악 재생은 중단하지 않는다. */
	void ReleaseMusicPreload(uint64 RequestId);

	/** 입력 및 UI 피드백에 사용하는 로컬 전용 2D 재생. */
	bool PlayLocal2D(const FOutlierAudioPlayRequest& Request);

	/** 로컬 Voice 뱅크 직렬 재생. P0 > P1 > P2 > P3 순이며, 더 높은 우선순위는 기존 음성과 자막을 중단한다. 같거나 낮으면 대기한다. */
	bool PlayLocalVoice(FGameplayTag SoundTag, int32 Priority, FGameplayTag RequestTag, ULocalPlayer* Listener);
	void StopLocalVoices(ULocalPlayer* Listener);
	/** Voice 오디오 컴포넌트 재생 시작 시 발생한다. RequestTag는 호출자의 EventTag이다. */
	FOnOutlierVoiceStarted OnVoiceStarted;
	/** 자연 종료 시 bCompleted=true, 중단/명시적 정지/재생 실패 시 false. */
	FOnOutlierVoiceFinished OnVoiceFinished;
	/** 음성을 아예 재생하지 못했을 때만(variant 없음, 로드/스폰 실패) OnVoiceFinished(false) 뒤에 발생한다. */
	FOnOutlierVoiceUnavailable OnVoiceUnavailable;

	/**
	 * 각 클라이언트에서 이미 실행되는 호출부를 위한 로컬 전용 3D 재생이다.
	 * GameplayCue Notify처럼 네트워크로 다시 전송하면 안 되는 경로에서 사용한다.
	 *
	 * Cue는 ASC가 이미 멀티캐스트하므로 Notify에서 Relevant* 함수를 호출하면
	 * 동일한 재생 요청이 네트워크로 한 번 더 전파된다.
	 *
	 * 이 경로는 서버가 Variant를 확정하지 않아 클라이언트마다 독립적으로 선택한다.
	 * 모든 플레이어가 같은 Variant를 들어야 한다면 Relevant* 함수를 사용한다.
	 */
	bool PlayLocalAtLocation(const FOutlierAudioPlayRequest& Request);

	void GetSurfaceContextCandidates(FGameplayTag Context, EPhysicalSurface Surface, TArray<FGameplayTag>& OutContexts) const;
	bool PlaySurfaceAtLocationFromServer(AActor* Emitter, FGameplayTag Type, FGameplayTag Context,
		EPhysicalSurface Surface, const FVector& Location, USoundDefinition* LegacySound = nullptr);
	bool PlaySurfaceAtLocationLocally(AActor* Emitter, FGameplayTag Type, FGameplayTag Context,
		EPhysicalSurface Surface, const FVector& Location);
	bool IsAnimationAudioSource(USkeletalMeshComponent* Mesh, bool bWorldMeshOnly) const;
	void SetAnimationAudioLoop(USkeletalMeshComponent* Mesh, FGameplayTag Type, FGameplayTag Context,
		bool bEnabled, bool bWorldMeshOnly, bool bTwoD = false);
	void StopAnimationAudioLoops(AActor* Emitter);
	bool PlayAnimationAudio(USkeletalMeshComponent* Mesh, FGameplayTag Type, FGameplayTag Context,
		FName SocketName, bool bQuerySurface, bool bWorldMeshOnly, float TraceDistance = 80.0f);

	/** 서버에서 확정한 2D 사운드를 소유 플레이어 한 명에게 전달한다. */
	bool PlayOwner2DFromServer(const FOutlierAudioPlayRequest& Request);

	/** 게임플레이는 Type + Context만 전달하고, 재생 수명은 오디오 서브시스템이 관리한다. */
	static bool PlayTaggedAtLocationFromServer(
		AActor* EmitterActor,
		FGameplayTag TypeTag,
		FGameplayTag ContextTag);

	/** 재생 주체와 Context로 식별한 지속 재생을 중단한다. */
	static bool StopTaggedAtLocationFromServer(
		AActor* EmitterActor,
		FGameplayTag TypeTag,
		FGameplayTag ContextTag);

	/** 소유 클라이언트가 자신의 Controller를 통해 월드 사운드 재생을 요청한다. 네트워크 관련성이 있는 클라이언트에 전달한다. */
	bool PlayRelevantAtLocationFromOwningClient(const FOutlierAudioPlayRequest& Request);

	/** 서버 게임플레이에서 월드 사운드 재생을 요청한다. 네트워크 관련성이 있는 클라이언트에 전달한다. */
	bool PlayRelevantAtLocationFromServer(const FOutlierAudioPlayRequest& Request);

	/** 소유 PlayerController의 월드 오디오 Server RPC에서만 호출한다. */
	bool HandleServerRelevantAtLocationRequest(
		AFirstPersonPlayerController* RequestingController,
		const FOutlierAudioPlayRequest& Request);

	/** RPC/GAS로 전달된 재생 요청의 최종 처리 지점. 네트워크로 다시 전송하지 않는다. */
	bool PlayResolvedAudioLocally(const FOutlierResolvedAudioPlay& ResolvedPlay);

	/** 로컬에서 재생 중인 루프 인스턴스 하나를 중단하고 해제한다. */
	bool StopLoopAudioLocally(int32 AudioInstanceId, float FadeOutDuration = 0.0f);

	UFUNCTION(BlueprintCallable, Category = "Outlier|Audio|Settings")
	void SetVolumeMultiplier(EOutlierAudioVolumeType VolumeType, float NewMultiplier);

	UFUNCTION(BlueprintPure, Category = "Outlier|Audio|Settings")
	float GetVolumeMultiplier(EOutlierAudioVolumeType VolumeType) const;

	/** 현재 재생과 대기 요청을 모두 정리한다. 이후 UI 재생 요청은 계속 사용할 수 있다. */
	UFUNCTION(BlueprintCallable, Category = "Outlier|Audio|Playback")
	void StopAllLocalAudio();
	/** 메뉴 진입 시 일시적인 피드백만 중단하고 게임플레이가 요청한 루프 상태는 유지한다. */
	void StopTransientLocalAudio();
	void StartFrontendMusic();
	void EnterGameplayMusic();

// 요청된 로컬 루프 상태. 핸들, 재시도, 재생 주체에 대한 부착은 서브시스템이 관리한다.
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
	struct FRuntimeCatalogEntry;
	friend class FOutlierSurfaceAudioSelectionTest;
	friend class FOutlierMusicPreloadTest;
	struct FMusicPreloadRequest
	{
		TWeakObjectPtr<UWorld> World;
		TArray<FGameplayTag> Contexts;
		TSet<FSoftObjectPath> SoundPaths;
		EOutlierMusicPreloadState State = EOutlierMusicPreloadState::Waiting;
		bool bMissingContext = false;
	};
	struct FMusicPreloadSound
	{
		TSharedPtr<FStreamableHandle> Handle;
		uint64 Serial = 0;
		int32 Users = 0;
		bool bComplete = false;
		bool bFailed = false;
	};
	TMap<uint64, FMusicPreloadRequest> MusicPreloadRequests;
	TMap<FSoftObjectPath, FMusicPreloadSound> MusicPreloadSounds;
	UPROPERTY(Transient)
	TMap<FSoftObjectPath, TObjectPtr<USoundBase>> RetainedMusicPreloadSounds;
	uint64 NextMusicPreloadRequestId = 0;
	uint64 NextMusicPreloadSoundSerial = 0;
	bool bBankDiscoveryComplete = false;
	bool bResumingMusicPreloads = false;
	void ResumeMusicPreloads();
	void AcquireMusicPreloadSound(const FSoftObjectPath& Path);
	void HandleMusicPreloadSoundLoaded(FSoftObjectPath Path, uint64 Serial);
	void ReleaseMusicPreloadsForWorld(UWorld* World);
	void ClearMusicPreloads();
	TMap<FGameplayTag, FOutlierSurfaceContextMapping> SurfaceContextMappings;
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, TSet<FGameplayTag>> AnimationAudioTokens;
	TWeakObjectPtr<UWorld> SurfaceVisualWorld;
	TWeakObjectPtr<UVisualEventSubsystem> SurfaceVisualSubsystem;
	UVisualEventSubsystem* GetSurfaceVisualSubsystem();
	const FRuntimeCatalogEntry* ResolveOrderedEntry(FGameplayTag Type, const TArray<FGameplayTag>& Contexts) const;
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
	TMap<FGameplayTag, TArray<TPair<FOutlierResolvedAudioPlay, TWeakObjectPtr<UWorld>>>> PendingResolvedPlaysByType;
	TMap<FSoftObjectPath, TArray<TPair<FOutlierAudioPlayRequest, FOutlierAudioExecutionPolicy>>> PendingSurfaceSelectionsBySound;
	TSet<FSoftObjectPath> FailedSurfaceSounds;
	UPROPERTY(Transient)
	TArray<TObjectPtr<USoundDefinition>> PendingLegacySounds;
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
		bool bOneShotSubmitted = false;
	};
	UPROPERTY(Transient)
	TArray<TObjectPtr<class UOutlierMenuAudioBinding>> MenuAudioBindings;
	TMap<FLoopPlaybackKey, FLocalEmitterLoop> LocalEmitterLoops;
	FTimerHandle LocalLoopTimer;
	TWeakObjectPtr<UWorld> LocalLoopWorld;
	int32 NextLocalLoopId = -1; // 네트워크 인스턴스 ID는 양수를 사용한다.
	void RefreshLocalEmitterLoops();
	void ClearLocalEmitterLoops();
	int32 NextAudioInstanceId = 1;
};
