#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Interface/ActorBasedWidgetInterface.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideTypes.h"
#include "UI/WorldWidgetPresentation.h"
#include "OutlierWorldGuideTargetComponent.generated.h"

class AOutlierWorldGuideManager;
class UUserWidget;
class UWidgetComponent;

/** 화면 공간 UI 마커용 월드 앵커. Tuto 액터를 포함한 모든 액터에 추가할 수 있다. */
UCLASS(ClassGroup = (Tutorial), meta = (BlueprintSpawnableComponent))
class OUTLIER_API UOutlierWorldGuideTargetComponent : public USceneComponent, public IActorBasedWidgetInterface
{
	GENERATED_BODY()

public:
	UOutlierWorldGuideTargetComponent();
	virtual void DismissActorBasedWidgets_Implementation() override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Guide", meta = (Categories = "UI.Info.World"))
	FGameplayTag WidgetID;

	/** 소유 BP의 컴포넌트에서 렌더링할 WBP 클래스를 지정한다. 미지정 시 위젯을 생성하지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Guide")
	TSubclassOf<UUserWidget> WidgetClass;

	/** 화면 UI 크기. 뷰포트의 일반 DPI 스케일링을 따른다. Component Scale은 UI 크기에 영향을 주지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Guide", meta = (ClampMin = "1.0", DisplayName = "UI Draw Size"))
	FVector2D DrawSize = FVector2D(128.0, 128.0);

	/** 이 Component 위치에 더하는 월드 단위 오프셋. 소유자 회전/스케일의 영향을 받지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Guide")
	FVector WorldOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Guide|Widget")
	FWorldWidgetPresentation WidgetPresentation;

	/** 투영된 월드 앵커 기준 UI 마커 정렬. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Guide", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	FVector2D Pivot = FVector2D(0.5, 0.5);

	/** 선택 사항. Overlap 도착으로 완료될 때 ChainContexts 대신 이어갈 분기. 경로별 도착 볼륨에서 쓴다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Guide", meta = (Categories = "UI.Info.World"))
	FGameplayTag OverlapNextWidgetID;

	UFUNCTION(BlueprintPure, Category = "World Guide")
	bool IsGuideVisible() const { return bGuideVisible; }

	/** 그룹 안내에서 이 타깃이 먼저 완료되어 마커를 다시 띄우지 않는 상태. */
	UFUNCTION(BlueprintPure, Category = "World Guide")
	bool IsTargetCompleted() const { return bTargetCompleted; }

	/** 서버 전용. Manager가 그룹 타깃 완료를 기록하거나 초기화할 때 호출한다. */
	void SetTargetCompleted(bool bCompleted);

	/** 서버에서 게임플레이로 수동 완료한다. 그룹 Row면 이 타깃만 완료로 세고, 비어 있지 않은 다음 ID는 분기를 선택한다. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "World Guide")
	bool CompleteGuide(FGameplayTag NextWidgetID = FGameplayTag());

	/** 도착 볼륨에서 대상 Pawn을 거른 뒤 호출한다. Overlap Row만 완료된다. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "World Guide")
	bool NotifyGuideOverlap();

	void ApplyGuide(const FOutlierWorldGuideContext& Context);
	void ClearGuide();
	void SetGuideManager(AOutlierWorldGuideManager* Manager);
	static bool CompleteOnActor(AActor* Actor, FGameplayTag NextWidgetID = FGameplayTag());
	static bool NotifyOverlapOnActor(AActor* Actor);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	void UpdateDisplayPlacement();

	UPROPERTY(ReplicatedUsing = OnRep_TargetCompleted)
	bool bTargetCompleted = false;

	UFUNCTION()
	void OnRep_TargetCompleted();

	UPROPERTY(Transient)
	TObjectPtr<UWidgetComponent> DisplayComponent;

	TWeakObjectPtr<AOutlierWorldGuideManager> GuideManager;
	bool bGuideVisible = false;
};
