// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DamageFeedbackLayer.generated.h"

class AActor;
class FViewport;
class UCanvasPanel;
class UDamageFeedbackIndicator;
class UImage;
class UMaterialInstanceDynamic;
class URedDamageFeedbackWidget;
class URetainerBox;
class UShieldDamageFeedbackWidget;

/**
 * 메인 위젯 루트 캔버스 아래에 배치되는 캐시된 데미지 피드백 레이어.
 * 인디케이터는 한 번 생성해 풀링한다. 레이어가 Tick을 구동하고, 활성 항목이 없으면 스스로 축소한다.
 */
UCLASS(Abstract, Blueprintable)
class TAGDRIVENUI_API UDamageFeedbackLayer : public UUserWidget
{
	GENERATED_BODY()

public:
	// HealthDamage: 실드를 넘어 Health 에 실제로 들어간 피해량. 0 이면 방향 인디케이터만 띄운다.
	UFUNCTION(BlueprintCallable, Category = "Damage Feedback")
	void ShowDamageFeedback(AActor* InCharacter, const FVector& InDamageOrigin, float HealthDamage = 0.0f);

	UFUNCTION(BlueprintCallable, Category = "Damage Feedback")
	void ClearDamageFeedback();

	// Threshold 를 높은 단계부터 검사해 1~2 단계를 돌려준다. Level1Threshold 미만이면 0.
	UFUNCTION(BlueprintPure, Category = "Damage Feedback|Health")
	int32 ResolveHealthDamageLevel(float HealthDamage) const;

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

	// Health 피격 단계 HUD. Brush 에 M_DamageFeedbackHUD(또는 그 인스턴스)를 지정한다.
	// 머티리얼은 CurrentLevel / NextLevel / Blend / Opacity 파라미터로 단계 이미지와 전환을 그린다.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> DamageFeedbackHUD;

	// Health 피해량 단계 기준. 높은 단계부터 검사하며 피해량 >= 값이면 해당 단계다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage Feedback|Health", meta = (ClampMin = "0.0"))
	float HealthDamageLevel2Threshold = 20.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage Feedback|Health", meta = (ClampMin = "0.0"))
	float HealthDamageLevel1Threshold = 1.0f;

	// 마지막 피격부터 HUD 가 사라질 때까지의 시간. 새 피격마다 처음부터 다시 센다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage Feedback|Health", meta = (ClampMin = "0.0"))
	float HealthFadeOutDuration = 0.5f;

	// 페이드 중 다른 단계로 맞았을 때 Current → Next 로 넘어가는 시간.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage Feedback|Health", meta = (ClampMin = "0.0"))
	float HealthBlendDuration = 0.15f;

	UPROPERTY(EditDefaultsOnly, Category = "Damage Feedback|Health|Material")
	FName CurrentLevelParameterName = TEXT("CurrentLevel");

	UPROPERTY(EditDefaultsOnly, Category = "Damage Feedback|Health|Material")
	FName NextLevelParameterName = TEXT("NextLevel");

	UPROPERTY(EditDefaultsOnly, Category = "Damage Feedback|Health|Material")
	FName BlendParameterName = TEXT("Blend");

	UPROPERTY(EditDefaultsOnly, Category = "Damage Feedback|Health|Material")
	FName OpacityParameterName = TEXT("Opacity");

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

	bool StartHealthFeedback(int32 Level);
	bool TickHealthFeedback(float DeltaTime);
	void ClearHealthFeedback();
	void PushHealthFeedbackParameters();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HealthFeedbackMID;

	int32 HealthCurrentLevel = 0;
	int32 HealthNextLevel = 0;
	float HealthBlend = 0.0f;
	float HealthBlendElapsed = 0.0f;
	float HealthFadeElapsed = 0.0f;
	bool bHealthFeedbackActive = false;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UDamageFeedbackIndicator>> RedPool;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UDamageFeedbackIndicator>> ShieldPool;

	FVector2D ShieldCanvasResolution = FVector2D::ZeroVector;
	FDelegateHandle ViewportResizedHandle;
};
