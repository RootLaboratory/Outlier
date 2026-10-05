// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DamageFeedbackLayer.generated.h"

class AActor;
class FViewport;
class UCanvasPanel;
class UDamageFeedbackIndicator;
class URedDamageFeedbackWidget;
class URetainerBox;
class UShieldDamageFeedbackWidget;

/**
 * Cached damage feedback layer placed under the main widget's root canvas, like InteractionLayer.
 * Indicators are created once and pooled; the layer drives their tick and collapses itself when none are active.
 */
UCLASS(Abstract, Blueprintable)
class TAGDRIVENUI_API UDamageFeedbackLayer : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Damage Feedback")
	void ShowDamageFeedback(AActor* InCharacter, const FVector& InDamageOrigin);

	UFUNCTION(BlueprintCallable, Category = "Damage Feedback")
	void ClearDamageFeedback();

	// 전체 Shield Canvas 의 실제 픽셀 크기. 뷰포트 크기 변경 시에는 자동으로 갱신된다.
	UFUNCTION(BlueprintCallable, Category = "Damage Feedback|Shield|Mask")
	void SetShieldCanvasResolution(FVector2D InCanvasResolution);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// 풀링된 인디케이터가 붙는 캔버스. 인디케이터는 이 캔버스 중앙 기준으로 배치된다.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UCanvasPanel> FeedbackCanvas;

	// WBP: FeedbackCanvas > ShieldMaskRetainer > ShieldMaskCanvas.
	// 전환 중 기존 WBP 도 열 수 있도록 optional 로 두지만, Shield 풀에는 이 Canvas 가 필요하다.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<URetainerBox> ShieldMaskRetainer;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UCanvasPanel> ShieldMaskCanvas;

	// 피격 시 Red / Shield 중 어떤 인디케이터를 띄울지. 둘 다 켜면 함께 뜬다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Damage Feedback")
	bool bShowRedFeedback = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Damage Feedback")
	bool bShowShieldFeedback = false;

	UPROPERTY(EditDefaultsOnly, Category = "Damage Feedback|Red")
	TSubclassOf<URedDamageFeedbackWidget> RedFeedbackClass;

	UPROPERTY(EditDefaultsOnly, Category = "Damage Feedback|Red", meta = (ClampMin = "1"))
	int32 RedPoolSize = 4;

	// 인디케이터 Render Scale 배율(x, y). 크기와 간격은 WBP desired size(SizeBox + Spacer)가 정하고 여기선 배율만 준다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage Feedback|Red", meta = (ClampMin = "0.0"))
	FVector2D RedScale = FVector2D(1.0f, 1.0f);

	// 중앙 앵커 기준 캔버스 슬롯 위치 오프셋(x, y).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage Feedback|Red")
	FVector2D RedDistance = FVector2D::ZeroVector;

	// 캔버스 슬롯 Alignment. 같은 값을 회전 Pivot 으로도 쓴다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage Feedback|Red")
	FVector2D RedAlignment = FVector2D(0.5f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category = "Damage Feedback|Shield")
	TSubclassOf<UShieldDamageFeedbackWidget> ShieldFeedbackClass;

	// 레이어 마스크가 유지할 기준 표시 크기. 정사각형 마스크는 가로/세로를 같은 값으로 둔다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage Feedback|Shield|Mask")
	FVector2D ShieldMaskReferenceSize = FVector2D(1080.0f, 1080.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage Feedback|Shield|Mask")
	FName ShieldUVScaleParameterName = TEXT("AspectScale");

	UPROPERTY(EditDefaultsOnly, Category = "Damage Feedback|Shield", meta = (ClampMin = "1"))
	int32 ShieldPoolSize = 4;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage Feedback|Shield", meta = (ClampMin = "0.0"))
	FVector2D ShieldScale = FVector2D(1.0f, 1.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage Feedback|Shield")
	FVector2D ShieldDistance = FVector2D::ZeroVector;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage Feedback|Shield")
	FVector2D ShieldAlignment = FVector2D(0.5f, 0.5f);

private:
	void HandleViewportResized(FViewport* Viewport, uint32 Unused);
	void ApplyShieldUVScale();
	void BuildPool(UCanvasPanel* ParentCanvas, TSubclassOf<UDamageFeedbackIndicator> IndicatorClass, int32 PoolSize, const FVector2D& Scale,
		const FVector2D& Distance, const FVector2D& Alignment, TArray<TObjectPtr<UDamageFeedbackIndicator>>& OutPool);
	bool ShowFromPool(const TArray<TObjectPtr<UDamageFeedbackIndicator>>& Pool, AActor* InCharacter,
		const FVector& InDamageOrigin);
	bool TickPool(const TArray<TObjectPtr<UDamageFeedbackIndicator>>& Pool, float DeltaTime);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UDamageFeedbackIndicator>> RedPool;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UDamageFeedbackIndicator>> ShieldPool;

	FVector2D ShieldCanvasResolution = FVector2D::ZeroVector;
	FDelegateHandle ViewportResizedHandle;
};
