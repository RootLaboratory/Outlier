#pragma once

#include "CoreMinimal.h"
#include "Drone/Partner/PartnerCharacterComponentBase.h"
#include "GameplayTagContainer.h"
#include "HackType.h"
#include "Interface/ActorBasedWidgetInterface.h"
#include "UI/UILayerTypes.h"
#include "PartnerHackComponent.generated.h"

class UHackableComponent;
class AEnemyBase;
class UHackCandidateLayerWidget;
class UHackCandidateMarkerWidget;
class UHackableInfoWidget;
class UHackMiniGameWidget;
class ULocalPlayerUILayerSubsystem;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UMeshComponent;
class UPrimitiveComponent;

enum class EPartnerHackTargetType : uint8
{
	Other,
	Possession,
	RetryableObject
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPartnerHackFinished, EHackResult, EPartnerHackTargetType);

USTRUCT()
struct FPartnerHackCursorHighlightState
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TWeakObjectPtr<UMeshComponent> Mesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> OriginalMaterial;
};

USTRUCT(BlueprintType)
struct OUTLIER_API FPartnerHackAbilityData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hack")
	float EffectiveRange = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hack")
	float MiniGameTime = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hack")
	float FailPenaltyTime = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hack")
	bool bRequireLineOfSight = true;
};

UCLASS()
class OUTLIER_API UPartnerHackComponent : public UPartnerCharacterComponentBase, public IActorBasedWidgetInterface
{
	GENERATED_BODY()

public:
	UPartnerHackComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void DismissActorBasedWidgets_Implementation() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Candidate", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float CandidateHalfAngleDegrees = 55.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Candidate")
	uint8 bRequireLineOfSight : 1 = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Debug")
	uint8 bDebugHack : 1 = true;


	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Candidate")
	FGameplayTagContainer RequiredCandidateTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Candidate")
	FGameplayTagContainer BlockedCandidateTags;

	// 커서 벽 판정에서 건너뛸 액터 클래스. 드론 조명(BP_LSG 계열)처럼 화면을 가리지 않는 콜리전을 넣는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hack|Cursor")
	TArray<TSubclassOf<AActor>> CursorIgnoredActorClasses;

	// 해킹 탐색 중 커서가 가리키는 후보 몸체에 적용할 오버레이 머티리얼.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hack|Cursor")
	TObjectPtr<UMaterialInterface> HackTrackingHighlightMaterial;

	// 제외 태그가 없는 몸체 태그 컴포넌트가 있으면 해당 컴포넌트로만 커서 판정을 한다.
	static const FName CursorBodyTag;
	// 커서 판정·하이라이트·벽 판정에서 모두 제외할 컴포넌트 태그.
	static const FName CursorIgnoreTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hack|UI")
	TSubclassOf<UHackCandidateLayerWidget> CandidateLayerWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hack|UI")
	TSubclassOf<UHackCandidateMarkerWidget> CandidateMarkerWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hack|UI")
	TSubclassOf<UHackableInfoWidget> HackableInfoWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hack|UI")
	TSubclassOf<UHackMiniGameWidget> HackMiniGameWidgetClass;

	UFUNCTION(Server, Reliable)
	void TryHack();

	void EndHackHold();
	void CancelForReboot();
	FOnPartnerHackFinished OnHackFinished;

	UFUNCTION(BlueprintCallable, Category = "Hack")
	void CacheAbilityData(const FPartnerHackAbilityData& InAbilityData);

	UFUNCTION(Client, Reliable)
	void ClientStartCandidateSearch(float InEffectiveRange);
	UFUNCTION(Client, Reliable)
	void ClientStopCandidateSearch();

	UFUNCTION(Client, Reliable)
	void ClientStartHackMiniGame(AActor* TargetActor, UHackableComponent* HackableComponent,
		float ClickSuccessRangeMultiplier = 1.0f, float SpinningGapMultiplier = 1.0f, uint32 RequestId = 0,
		int32 GameplayGeneration = 0, int32 LeaseSerial = 0);

	UFUNCTION(Client, Reliable)
	void ClientStopHackMiniGame();

	UFUNCTION(Client, Reliable)
	void ClientAbortHackForInvalidTarget();

	UFUNCTION(Client, Reliable)
	void DefaultWidgetControl(bool InFlag);

	UFUNCTION(Server, Reliable)
	void ServerCompleteHack(const FHackResultContext& ResultContext);

	UFUNCTION(BlueprintCallable, Category = "Hack")
	void RefreshHackCandidates();

	UFUNCTION(Server, Reliable)
	void ServerTryStartHack(AActor* TargetActor, uint32 HoldId);

	UFUNCTION(Server, Reliable)
	void ServerBeginHackHold(AActor* TargetActor, uint32 HoldId);

	UFUNCTION(Server, Reliable)
	void ServerEndHackHold(uint32 HoldId);

	UFUNCTION(Client, Reliable)
	void ClientCancelHackHold(uint32 HoldId);

	UFUNCTION(BlueprintCallable, Category = "Hack")
	void ClientCompleteHack();

	UFUNCTION(BlueprintCallable, Category = "Hack")
	void ClearHackCandidates();

	UFUNCTION(BlueprintCallable, Category = "Hack")
	void StopHackCandidateSearch();

	UFUNCTION(BlueprintCallable, Category = "Hack")
	bool IsHackCandidateSearchActive() const { return bHackCandidateSearchActive; }

	UFUNCTION(BlueprintCallable, Category = "Hack")
	bool IsHackInteractionActive() const { return bHackCandidateSearchActive || ActiveHackableComponent || HackMiniGameWidget; }

	bool TryBeginHackHold();
	bool IsHackHoldActive() const { return HeldHackActor != nullptr; }
	void NotifyHackMarkerHovered(UHackCandidateMarkerWidget* MarkerWidget, AActor* TargetActor);
	void NotifyHackMarkerUnhovered(UHackCandidateMarkerWidget* MarkerWidget, AActor* TargetActor, bool bMarkerDestroying = false);
	void ResetLocalHackHoldProgress(bool bNotifyServer = true);

	UFUNCTION(BlueprintCallable, Category = "Hack")
	const TArray<AActor*>& GetHackCandidateActors() const { return HackCandidateActors; }

protected:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UHackableComponent>> HackCandidateComponents;

	UPROPERTY(Transient)
	TObjectPtr<UHackableComponent> ActiveHackableComponent;

	UPROPERTY(Transient)
	TObjectPtr<UHackCandidateLayerWidget> CandidateLayerWidget;

	UPROPERTY(Transient)
	TObjectPtr<UHackMiniGameWidget> HackMiniGameWidget;


private:
#if WITH_DEV_AUTOMATION_TESTS
	// 네트워크 검증은 실제 검색·RPC 결과를 관측한다. 테스트를 위한 판정 우회 API는 노출하지 않는다.
	friend class AHackTargetPointNetworkProbe;
	friend class FAutoTurretHackingTest;
	friend class FEnemyPoolHackStateTest;
	friend class FActorBodyTargetingTest;
#endif
	UPROPERTY(VisibleInstanceOnly, Category = "Hack")
	FPartnerHackAbilityData CachedAbilityData;

	TArray<AActor*> HackCandidateActors;

	uint8 bHackCandidateSearchActive : 1 = false;
	int32 LastDebugCandidateCount = INDEX_NONE;
	FUILayerHandle HackCandidateLayerHandle;
	FUILayerHandle HackMiniGameLayerHandle;

	UPROPERTY(Transient)
	TObjectPtr<AActor> HoveredHackActor;

	UPROPERTY(Transient)
	TObjectPtr<UHackCandidateMarkerWidget> HoveredMarkerWidget;

	UPROPERTY(Transient)
	TObjectPtr<AActor> HeldHackActor;

	// 마커 버튼 hover는 몸체 판정이 실패했을 때의 보조 선택 경로다.
	UPROPERTY(Transient)
	TObjectPtr<AActor> MarkerHoveredActor;

	UPROPERTY(Transient)
	TObjectPtr<UHackCandidateMarkerWidget> MarkerHoveredWidget;

	TWeakObjectPtr<AActor> HighlightedActor;
	// 덮어쓴 기존 머티리얼은 복원할 때까지 GC되지 않도록 보관한다.
	UPROPERTY(Transient)
	TArray<FPartnerHackCursorHighlightState> HighlightedMeshes;
	bool bLoggedHighlightSkeletalUsage = false;

	// hover 시작 시각(HoverStartTime)을 넣는 MID. 하이라이트 대상은 한 번에 하나라 재사용한다.
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HighlightMID;
	TWeakObjectPtr<AActor> ServerHeldHackActor;
	uint32 LocalHoldId = 0;
	uint32 ServerHoldId = 0;
	uint32 NextHackRequestId = 0;
	uint32 ActiveHackRequestId = 0;
	EPartnerHackTargetType ActiveHackTargetType = EPartnerHackTargetType::Other;
	TWeakObjectPtr<AEnemyBase> ActivePoolHackEnemy;
	int32 ActiveHackGeneration = 0;
	int32 ActiveHackLease = 0;
	TWeakObjectPtr<AEnemyBase> ServerHeldPoolEnemy;
	int32 ServerHoldGeneration = 0;
	int32 ServerHoldLease = 0;
	bool IsActiveHackLifetimeCurrent() const;
	bool IsServerHoldLifetimeCurrent() const;
	bool bHackCompletionPending = false;
	float HackHoldStartTime = 0.0f;
	float HackHoldDuration = 0.0f;
	void UpdateLocalHackHold();
	bool CanMaintainHackHold(AActor* Actor, const FHackQueryContext& Context) const;

	FHackQueryContext BuildQueryContext() const;
	UHackableComponent* ResolveHackableComponent(AActor* Actor) const;
	bool IsCandidateActorValid(AActor* Actor, UHackableComponent* HackableComponent, const FHackQueryContext& Context, FVector2D& OutScreenLocation) const;
	bool IsInsidePartnerFrustum(AActor* Actor, FVector2D& OutScreenLocation) const;
	bool HasLineOfSight(AActor* Actor) const;

	// 커서 대상 판정: 후보 몸체에 커서 레이를 대 보고(정밀 → 바운드), 벽보다 가까운 것만 인정한다.
	AActor* ResolveCursorHackTarget() const;
	AActor* ResolveCursorHackTargetAlongRay(const FVector& Start, const FVector& Direction) const;
	// 횟수 제한 없이 교차 지점까지 검사하며, 차폐 없음을 확인한 경우에만 true.
	bool HasClearCursorPath(const FVector& Start, const FVector& End) const;
	bool IsCursorIgnoredActor(const AActor* HitActor) const;
	static void GatherCursorBodyComponents(AActor* Actor, TArray<UPrimitiveComponent*>& OutBodies);
	void UpdateCursorHackHover();
	void SetHoveredHackTarget(AActor* TargetActor);
	void RefreshCursorHighlight();
	void ApplyCursorHighlight(AActor* TargetActor);
	void ClearCursorHighlight();

	void EnsureCandidateLayerWidget();
	void DestroyCandidateLayerWidget();
	float ResolveMiniGameSuccessRangeMultiplier(bool bSpinningCircle) const;
	bool EnsureHackMiniGameWidget(AActor* TargetActor, UHackableComponent* HackableComponent,
		float ClickSuccessRangeMultiplier, float SpinningGapMultiplier);
	void DestroyHackMiniGameWidget();
	ULocalPlayerUILayerSubsystem* GetUILayerSubsystem() const;
	void AddHackCandidate(AActor* Actor, UHackableComponent* HackableComponent, const FVector2D& ScreenLocation);
	void RemoveHackCandidateAt(int32 Index);
	void DeactivateUnselectedCandidates(UHackableComponent* SelectedComponent);
	void CancelActiveHack();
	void CompleteActiveHack(const FHackResultContext& ResultContext, bool bNotifyClient);
	void FinishDeferredHack(const FHackResultContext& ResultContext, bool bNotifyClient);
	void SetActiveHackableComponent(UHackableComponent* HackableComponent);
	void ClearActiveHackableComponent();
	void AbortLocalHackForInvalidTarget();
	void HandleHackTargetInvalidated(
		UHackableComponent* InvalidatedComponent,
		EEndPlayReason::Type EndPlayReason);
	void HandleHackTargetUnavailable(UHackableComponent* UnavailableComponent);

	void StartHackMiniGame(AActor* TargetActor, UHackableComponent* HackableComponent,
		float ClickSuccessRangeMultiplier, float SpinningGapMultiplier);

	UFUNCTION()
	void HandleHackMiniGameFinished(const FHackResultContext& ResultContext);
};
