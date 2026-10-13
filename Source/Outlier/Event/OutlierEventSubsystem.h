#pragma once

#include "CoreMinimal.h"
#include "Event/OutlierEventTypes.h"
#include "OutlierInfoTypes.h"
#include "OutlierPlayerState.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "OutlierEventSubsystem.generated.h"

class UOutlierAudioSubsystem;
class UOutlierEventDataAsset;
struct FOutlierWorldGuideDataRow;

/** Local presentation endpoint. World/UI/gameplay triggers all submit the same EventTag. */
UCLASS()
class OUTLIER_API UOutlierEventSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Overrides the Config-selected role asset, e.g. for a tutorial loaded at runtime. */
	bool SetEventDataAsset(UOutlierEventDataAsset* DataAsset);

	/** Clears a runtime override and refreshes the asset selected by PlayerState.Role. */
	void RefreshEventDataAsset();

	UOutlierEventDataAsset* GetEventDataAsset();

	/** Uses each Upper/GIF row's DT end condition and display time. */
	bool SendEvent(FGameplayTag EventTag);
	/** Explicit runtime upper-lifetime override retained for existing callers. GIF still uses its own row. */
	bool SendEvent(FGameplayTag EventTag, const FOutlierEventContext& Context);

	/** Direct data-tag requests do not dispatch other regions from the Event DT. */
	bool SendTTS(FGameplayTag TTSTag);

	bool SendUpper(FGameplayTag UpperTag);
	bool SendUpper(FGameplayTag UpperTag, const FOutlierEventContext& Context);
	bool SendGif(FGameplayTag GifTag);

	bool TryGetEventData(FGameplayTag EventTag, FOutlierEventDataRow& OutData);

	/** Looks up a TTS row by its audio context tag, not an event tag. */
	bool TryGetTTSData(FGameplayTag TTSTag, FOutlierTTSDataRow& OutData);

	bool TryGetUpperData(FGameplayTag UpperTag, FOutlierUpperData& OutData);

	bool TryGetEventUpperData(FGameplayTag EventTag, FOutlierUpperData& OutData);

	bool TryGetInputData(FGameplayTag InputTag, FOutlierInputDataRow& OutData);

	UFUNCTION(BlueprintCallable, Category = "Event|Input")
	bool TryGetInputDisplayData(FGameplayTag InputTag, FInputDisplayDefinition& OutDisplay);

	/** Resolve the event's InputTag, or its GIF row's InputTag if no upper input is selected. */
	UFUNCTION(BlueprintCallable, Category = "Event|Input")
	bool TryGetEventInputDisplayData(FGameplayTag EventTag, FInputDisplayDefinition& OutDisplay);

	bool TryGetGoalData(FGameplayTag GoalTag, FOutlierGoalDataRow& OutData);

	bool TryGetGifData(FGameplayTag GifTag, FOutlierGifDataRow& OutData);

	/** Resolves the GIF reference from the active role's event table. */
	bool TryGetEventGifData(FGameplayTag EventTag, FOutlierGifDataRow& OutData);

	bool TryGetWorldGuideData(FGameplayTag WidgetID, FOutlierWorldGuideDataRow& OutData);
	/** Shared progression is authoritative; client-local events do not create another guide queue. */
	bool SendWorldGuide(FGameplayTag WidgetID);

	bool CompleteUpperEvent(FGameplayTag EventTag);
	bool CompleteGifEvent(FGameplayTag EventTag);

	bool CancelUpperEvent(FGameplayTag EventTag);

	bool CancelGifEvent(FGameplayTag EventTag);

	bool CompleteInputEvent(FGameplayTag EventTag);

	/** Called by the overlap source, after filtering the appropriate local player. */
	bool NotifyOverlap(FGameplayTag EventTag);
	bool NotifyGifOverlap(FGameplayTag EventTag);

	/** Cancels all pending/displayed input tutorials without chaining. */
	void CancelInputEvent();

	FGameplayTag GetActiveInputEventTag() const;

	/**
	 * A region's item finished normally on screen (time, voice end or fallback subtitle).
	 * Starts the rows that chain from it in the same region only. Cancelled items never call this.
	 */
	void NotifyInfoCompleted(EOutlierInfoRegion Region, const FOutlierInfoWidgetRequest& Request);

private:
	void LoadConfiguredTables();
	bool ApplyDataAsset(UOutlierEventDataAsset* DataAsset);
	void ClearPresentation();
	void HandlePlayerRoleChanged(AOutlierPlayerState* PlayerState);
	FGameplayTag ResolveUpperTag(const FOutlierEventDataRow& Event) const;
	bool SendEventInternal(FGameplayTag EventTag, const FOutlierEventContext* Context);
	bool SubmitUpper(FGameplayTag UpperTag, FGameplayTag RequestTag, const FOutlierEventContext* Context,
		FGameplayTag OriginEventTag = FGameplayTag());
	bool SubmitGif(FGameplayTag GifTag, FGameplayTag RequestTag, FGameplayTag OriginEventTag = FGameplayTag());
	void HandleVoiceStarted(ULocalPlayer* Listener, FGameplayTag RequestTag);
	void HandleVoiceFinished(ULocalPlayer* Listener, FGameplayTag RequestTag, bool bCompleted);
	void HandleVoiceUnavailable(ULocalPlayer* Listener, FGameplayTag RequestTag);
	FGameplayTag ResolveRequestTTSTag(FGameplayTag RequestTag);
	void EnsureAudioCallbacks();
	/** Rebuilds the per-region ChainFrom -> rows maps from the active tables, reporting broken links and cycles. */
	void RebuildChains();

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> EventTable;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> TTSTable;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> InputTable;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> InputDisplayTable;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> GoalTable;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> GifTable;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> WorldGuideTable;

	// ChainFrom tag -> rows that start when it completes, one map per region.
	TMultiMap<FGameplayTag, FGameplayTag> TTSChains;
	TMultiMap<FGameplayTag, FGameplayTag> UpperChains;
	TMultiMap<FGameplayTag, FGameplayTag> GifChains;

	TWeakObjectPtr<UOutlierAudioSubsystem> BoundAudioSubsystem;
	UPROPERTY(Transient)
	TObjectPtr<UOutlierEventDataAsset> CachedDataAsset;
	TWeakObjectPtr<AOutlierPlayerState> CachedPlayerState;
	EOutlierPlayerRole CachedRole = EOutlierPlayerRole::None;
	bool bRoleDataAssetResolved = false;
	bool bDataAssetOverridden = false;
	bool bApplyingDataAsset = false;
};
