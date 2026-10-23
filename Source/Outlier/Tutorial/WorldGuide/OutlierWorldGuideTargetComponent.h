#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Interface/ActorBasedWidgetInterface.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideTypes.h"
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

	/** 여기에 렌더링할 WBP를 지정한다. MainUI/Canvas 참조는 필요하지 않다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Guide")
	TSubclassOf<UUserWidget> WidgetClass;

	/** 화면 UI 크기. 뷰포트의 일반 DPI 스케일링을 따른다. Component Scale은 UI 크기에 영향을 주지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Guide", meta = (ClampMin = "1.0", DisplayName = "UI Draw Size"))
	FVector2D DrawSize = FVector2D(128.0, 128.0);

	/** 이 Component 위치에 더하는 월드 단위 오프셋. 소유자 회전/스케일의 영향을 받지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Guide")
	FVector WorldOffset = FVector::ZeroVector;

	/** 투영된 월드 앵커 기준 UI 마커 정렬. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Guide", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	FVector2D Pivot = FVector2D(0.5, 0.5);

	UFUNCTION(BlueprintPure, Category = "World Guide")
	bool IsGuideVisible() const { return bGuideVisible; }

	/** 서버에서 게임플레이로 수동 완료한다. 비어 있지 않은 다음 ID는 분기를 선택한다. */
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

private:
	void UpdateDisplayPlacement();

	UPROPERTY(Transient)
	TObjectPtr<UWidgetComponent> DisplayComponent;

	TWeakObjectPtr<AOutlierWorldGuideManager> GuideManager;
	bool bGuideVisible = false;
};
