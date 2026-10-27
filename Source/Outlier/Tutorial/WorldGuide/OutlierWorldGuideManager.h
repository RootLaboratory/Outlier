#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideTypes.h"
#include "OutlierWorldGuideManager.generated.h"

class UDataTable;
class UPrimitiveComponent;
class UOutlierWorldGuideTargetComponent;
class UOutlierEventDataAsset;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnOutlierWorldGuideFinished, const FOutlierWorldGuideContext&, bool);

struct FOutlierGuideStencilState
{
	bool bOriginalCustomDepth = false;
	int32 OriginalStencil = 0;
	int32 AppliedStencil = 0;
	int32 Users = 0;
};

/** Persistent 레벨에 Manager 하나를 배치한다. 서버가 공유 FIFO 하나를 소유하고, 클라이언트는 ActiveContext만 렌더링한다. */
UCLASS(Blueprintable)
class OUTLIER_API AOutlierWorldGuideManager : public AActor
{
	GENERATED_BODY()

public:
	AOutlierWorldGuideManager();
	static AOutlierWorldGuideManager* Find(UWorld* World);

	/** Shooter DA 슬롯. 기존 BP/레벨 바인딩 유지를 위해 직렬화 이름을 바꾸지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Guide", meta = (DisplayName = "Shooter Event Data Asset"))
	TObjectPtr<UOutlierEventDataAsset> EventDataAsset;

	/** Partner DA 슬롯. 빈 슬롯은 프로젝트 설정의 역할별 DA로 대체된다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Guide")
	TObjectPtr<UOutlierEventDataAsset> PartnerEventDataAsset;

	/** 무조건 후속 목표만 둔다. 분기 액터는 선택한 NextWidgetID를 직접 전달한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Guide", meta = (TitleProperty = "WidgetID"))
	TArray<FOutlierWorldGuideContext> ChainContexts;

	/** 가이드 전용 Stencil. 복원 가드가 구분할 수 있도록 스캔/근접 Outline(1, 2), 무기 조준(3)과 달라야 한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Guide", meta = (ClampMin = "1", ClampMax = "255"))
	int32 OutlineStencilValue = 8;

	UPROPERTY(ReplicatedUsing = OnRep_ActiveContext, BlueprintReadOnly, Category = "World Guide")
	FOutlierWorldGuideContext ActiveContext;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "World Guide")
	bool EnqueueGuide(FOutlierWorldGuideContext Context);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "World Guide")
	bool TriggerEvent(FGameplayTag EventTag);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "World Guide")
	bool CompleteWidget(FGameplayTag WidgetID, FGameplayTag NextWidgetID = FGameplayTag());

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "World Guide")
	bool NotifyWidgetOverlap(FGameplayTag WidgetID, FGameplayTag NextWidgetID = FGameplayTag());

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "World Guide")
	bool CancelWidget(FGameplayTag WidgetID);

	/** 서버 전용. 그룹 Row(RequiredTargetCount > 1)는 타깃별 완료를 기록하고, 필요한 수가 모이면 WidgetID를 완료한다.
	 *  RequiredTargetCount가 1이면 CompleteWidget과 같다. */
	bool CompleteTarget(UOutlierWorldGuideTargetComponent* Target, FGameplayTag NextWidgetID = FGameplayTag());
	/** 타깃 하나의 마커/Outline만 현재 활성 안내와 완료 상태에 맞춘다. 다른 타깃의 위젯은 다시 만들지 않는다. */
	void RefreshTarget(UOutlierWorldGuideTargetComponent* Target);

	bool CompleteEvent(FGameplayTag EventTag);
	bool CancelEvent(FGameplayTag EventTag);
	bool NotifyEventOverlap(FGameplayTag EventTag);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "World Guide")
	void ClearGuides(bool bResetCompleted = false);

	bool TryGetDefinition(FGameplayTag WidgetID, FOutlierWorldGuideDataRow& OutRow);
	void RegisterTarget(UOutlierWorldGuideTargetComponent* Target);
	void UnregisterTarget(UOutlierWorldGuideTargetComponent* Target);
	int32 GetTargetCount(FGameplayTag WidgetID) const;
	int32 GetPendingCount() const { return PendingContexts.Num(); }
	FOnOutlierWorldGuideFinished OnGuideFinished;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void LoadTables();
	void AdvanceQueue();
	void FinishContext(const FOutlierWorldGuideContext& Context, bool bCompleted, FGameplayTag NextWidgetID = FGameplayTag());
	/** ChainContexts를 따라 Ancestor에서 Descendant에 도달하면 true. 같은 ID는 false. */
	bool IsChainAncestor(FGameplayTag Ancestor, FGameplayTag Descendant) const;
	/** 체인상 뒤 단계가 이미 활성/대기/완료이면 이 안내는 지나간 것이다. */
	bool IsPassedByLaterGuide(FGameplayTag WidgetID) const;
	/** Reached의 앞 단계인 활성/대기 안내와 그 사이 단계를 표시 없이 완료로 기록한다. */
	void SkipGuidesBefore(FGameplayTag Reached);
	bool FinishMatching(FGameplayTag Tag, bool bByEvent, bool bCompleted, bool bOverlapOnly, FGameplayTag NextWidgetID = FGameplayTag());
	void RefreshPresentation();
	void AddOutline(UOutlierWorldGuideTargetComponent* Target);
	void RemoveOutline(UOutlierWorldGuideTargetComponent* Target);
	UFUNCTION()
	void OnRep_ActiveContext();

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> GuideTable;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UDataTable>> EventTables;
	UPROPERTY(Transient)
	TArray<FOutlierWorldGuideContext> PendingContexts;

	TMap<FGameplayTag, TArray<TWeakObjectPtr<UOutlierWorldGuideTargetComponent>>> Targets;
	TMap<TWeakObjectPtr<UOutlierWorldGuideTargetComponent>, TArray<TWeakObjectPtr<UPrimitiveComponent>>> OutlinedTargets;
	TMap<TWeakObjectPtr<UPrimitiveComponent>, FOutlierGuideStencilState> StencilStates;
	TSet<FGameplayTag> CompletedWidgetIDs;
	/** 서버 전용. 그룹 안내의 완료된 타깃 경로. sublevel 리로드로 다시 생성된 타깃의 완료 표시를 복원한다. */
	TMap<FGameplayTag, TSet<FString>> CompletedTargets;
	int32 NextRequestID = 1;
	bool bChangingQueue = false;
	bool bEndingPlay = false;
	bool bClearingQueue = false;
};
