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

/** Place one manager in the persistent level. Server owns one shared FIFO; clients only render ActiveContext. */
UCLASS(Blueprintable)
class OUTLIER_API AOutlierWorldGuideManager : public AActor
{
	GENERATED_BODY()

public:
	AOutlierWorldGuideManager();
	static AOutlierWorldGuideManager* Find(UWorld* World);

	/** Shooter DA slot. Keep the serialized name so existing BP/level bindings survive. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Guide", meta = (DisplayName = "Shooter Event Data Asset"))
	TObjectPtr<UOutlierEventDataAsset> EventDataAsset;

	/** Partner DA slot. Each empty slot falls back to its role DA in project settings. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Guide")
	TObjectPtr<UOutlierEventDataAsset> PartnerEventDataAsset;

	/** Only unconditional successors belong here. A branch actor supplies its selected NextWidgetID. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Guide", meta = (TitleProperty = "WidgetID"))
	TArray<FOutlierWorldGuideContext> ChainContexts;

	/** Match the stencil consumed by the existing Outlier outline material. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Guide", meta = (ClampMin = "1", ClampMax = "255"))
	int32 OutlineStencilValue = 1;

	UPROPERTY(ReplicatedUsing = OnRep_ActiveContext, BlueprintReadOnly, Category = "World Guide")
	FOutlierWorldGuideContext ActiveContext;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "World Guide")
	bool EnqueueGuide(FOutlierWorldGuideContext Context);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "World Guide")
	bool TriggerEvent(FGameplayTag EventTag);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "World Guide")
	bool CompleteWidget(FGameplayTag WidgetID, FGameplayTag NextWidgetID = FGameplayTag());

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "World Guide")
	bool NotifyWidgetOverlap(FGameplayTag WidgetID);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "World Guide")
	bool CancelWidget(FGameplayTag WidgetID);

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
	/** Read-only setup/queue snapshot used by the development console. */
	FString BuildDebugReport();
	FOnOutlierWorldGuideFinished OnGuideFinished;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void LoadTables();
	void AdvanceQueue();
	void FinishContext(const FOutlierWorldGuideContext& Context, bool bCompleted, FGameplayTag NextWidgetID = FGameplayTag());
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
	int32 NextRequestID = 1;
	bool bChangingQueue = false;
	bool bEndingPlay = false;
	bool bClearingQueue = false;
};
