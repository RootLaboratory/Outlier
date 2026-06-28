#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EMPLayerWidget.generated.h"

class UCanvasPanel;
class UProgressBar;
class UEMPMarkWidget;
class UEMPableComponent;
class UPartnerEMPComponent;

UCLASS()
class OUTLIER_API UEMPLayerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void BindEMPComponent(UPartnerEMPComponent* InEMPComponent);
	void SetMarkWidgetClass(TSubclassOf<UEMPMarkWidget> InMarkWidgetClass);
	void InitializeMarkingTimer(float Duration);
	void AddCandidate(AActor* TargetActor, UEMPableComponent* EMPableComponent);
	void RemoveCandidate(AActor* TargetActor, UEMPableComponent* EMPableComponent);
	void ClearMarkers();

	// Tab 입력 바인딩 쪽에서 직접 호출
	void OnTabPressed();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UCanvasPanel> MarkerCanvas;

	// BP에서 배치하거나 코드에서 fallback 생성
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> EMPProgressBar;

	UPROPERTY(EditDefaultsOnly, Category = "EMP")
	TSubclassOf<UEMPMarkWidget> MarkWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "EMP")
	FVector2D MarkerSize = FVector2D(64.0f, 64.0f);

private:
	void FinishEMP();
	void ExpireEMP();

	UPROPERTY()
	TObjectPtr<UPartnerEMPComponent> EMPComponent;

	UPROPERTY()
	TMap<TObjectPtr<AActor>, TObjectPtr<UEMPMarkWidget>> MarkWidgets;

	float MarkingDuration = 3.0f;
	float ElapsedTime = 0.0f;
	bool bTimerActive = false;
	bool bFinished = false;
};
