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

/** 로컬 표시 진입점. World/UI/gameplay 트리거 모두 같은 EventTag를 제출한다. */
UCLASS()
class OUTLIER_API UOutlierEventSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** 각 Upper/GIF 행의 DT 종료 조건과 표시 시간을 쓴다. */
	bool SendEvent(FGameplayTag EventTag);

	/** 데이터 태그 직접 요청은 Event DT의 다른 영역을 디스패치하지 않는다. */
	bool SendTTS(FGameplayTag TTSTag);

	bool TryGetEventData(FGameplayTag EventTag, FOutlierEventDataRow& OutData);

	/** 이벤트 태그가 아닌 오디오 컨텍스트 태그로 TTS 행을 찾는다. */
	bool TryGetTTSData(FGameplayTag TTSTag, FOutlierTTSDataRow& OutData);

	bool TryGetUpperData(FGameplayTag UpperTag, FOutlierUpperData& OutData);

	bool TryGetEventUpperData(FGameplayTag EventTag, FOutlierUpperData& OutData);

	bool TryGetInputData(FGameplayTag InputTag, FOutlierInputDataRow& OutData);

	UFUNCTION(BlueprintCallable, Category = "Event|Input")
	bool TryGetInputDisplayData(FGameplayTag InputTag, FInputDisplayDefinition& OutDisplay);

	/** 이벤트의 InputTag를 해석한다. upper 입력이 없으면 GIF 행의 InputTag를 쓴다. */
	UFUNCTION(BlueprintCallable, Category = "Event|Input")
	bool TryGetEventInputDisplayData(FGameplayTag EventTag, FInputDisplayDefinition& OutDisplay);

	bool TryGetGoalData(FGameplayTag GoalTag, FOutlierGoalDataRow& OutData);

	bool TryGetGifData(FGameplayTag GifTag, FOutlierGifDataRow& OutData);

	/** 활성 역할의 이벤트 테이블에서 GIF 참조를 해석한다. */
	bool TryGetEventGifData(FGameplayTag EventTag, FOutlierGifDataRow& OutData);

	bool CompleteUpperEvent(FGameplayTag EventTag);
	bool CompleteGifEvent(FGameplayTag EventTag);

	bool CancelUpperEvent(FGameplayTag EventTag);

	bool CancelGifEvent(FGameplayTag EventTag);

	/** 해당 로컬 플레이어를 필터링한 뒤 overlap 소스가 호출한다. */
	bool NotifyOverlap(FGameplayTag EventTag);
	bool NotifyGifOverlap(FGameplayTag EventTag);

	/**
	 * 영역 항목이 화면에서 정상 종료됐다(시간 또는 음성 종료).
	 * 같은 영역에서 이어지는 행만 시작한다. 취소된 항목은 호출하지 않는다.
	 */
	void NotifyInfoCompleted(EOutlierInfoRegion Region, const FOutlierInfoWidgetRequest& Request);

private:
	void LoadConfiguredTables();
	bool ApplyDataAsset(UOutlierEventDataAsset* DataAsset);
	void ClearPresentation();
	void HandlePlayerRoleChanged(AOutlierPlayerState* PlayerState);
	FGameplayTag ResolveUpperTag(const FOutlierEventDataRow& Event) const;
	bool SubmitUpper(FGameplayTag UpperTag, FGameplayTag RequestTag, FGameplayTag OriginEventTag = FGameplayTag());
	bool SubmitGif(FGameplayTag GifTag, FGameplayTag RequestTag, FGameplayTag OriginEventTag = FGameplayTag());
	void HandleVoiceStarted(ULocalPlayer* Listener, FGameplayTag RequestTag);
	void HandleVoiceFinished(ULocalPlayer* Listener, FGameplayTag RequestTag, bool bCompleted);
	FGameplayTag ResolveRequestTTSTag(FGameplayTag RequestTag);
	void EnsureAudioCallbacks();
	/** 활성 테이블에서 영역별 ChainFrom -> rows 맵을 재구성하고, 끊긴 링크와 순환을 보고한다. */
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

	// ChainFrom 태그 -> 완료 시 시작할 행. 영역마다 맵 하나.
	TMultiMap<FGameplayTag, FGameplayTag> TTSChains;
	TMultiMap<FGameplayTag, FGameplayTag> UpperChains;
	TMultiMap<FGameplayTag, FGameplayTag> GifChains;

	TWeakObjectPtr<UOutlierAudioSubsystem> BoundAudioSubsystem;
	UPROPERTY(Transient)
	TObjectPtr<UOutlierEventDataAsset> CachedDataAsset;
	TWeakObjectPtr<AOutlierPlayerState> CachedPlayerState;
	EOutlierPlayerRole CachedRole = EOutlierPlayerRole::None;
	bool bRoleDataAssetResolved = false;
	bool bApplyingDataAsset = false;
};
