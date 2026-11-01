#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BreathingWidget.generated.h"

class UImage;
class UMaterialInterface;
class UMaterialInstanceDynamic;

UENUM(BlueprintType)
enum class EBreathingShape : uint8
{
	// 머티리얼의 Shape 값과 일치시킨다. 기존 WBP에 저장된 값도 유지한다.
	Circle = 0,
	RoundedRectangle = 1,
	// Image의 가로/세로 크기를 그대로 따라가므로 높이를 줄여 납작하게 만든다.
	Ellipse = 2
};

// 공통 UI 머티리얼의 모양과 숨쉬기를 담당한다. 게임별 긴박도 계산은 사용하는 HUD가 맡는다.
UCLASS(BlueprintType, Blueprintable)
class OUTLIER_API UBreathingWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 상태 변경은 위상을 초기화하지 않는다. 주기를 바꿔도 숨쉬기가 끊기지 않는다.
	UFUNCTION(BlueprintCallable, Category = "Breathing")
	void SetBreathingState(FLinearColor Color, float Amount, float Period);

	UFUNCTION(BlueprintCallable, Category = "Breathing")
	void SetBreathingPlaying(bool bInPlaying);

	UFUNCTION(BlueprintCallable, Category = "Breathing")
	void ResetBreathing();

	// 마지막 밝기를 유지하고 색만 바꾼다. 종료 프레임이나 선택 상태 고정에 사용한다.
	UFUNCTION(BlueprintCallable, Category = "Breathing")
	void FreezeBreathing(FLinearColor Color);

	// WBP에서 설정 변수를 실행 중 변경한 경우 호출한다. Designer에서는 자동 적용된다.
	UFUNCTION(BlueprintCallable, Category = "Breathing")
	void ApplyBreathingSettings();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breathing|Material")
	TObjectPtr<UMaterialInterface> BreathingMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breathing|Shape")
	EBreathingShape Shape = EBreathingShape::RoundedRectangle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breathing|Shape", meta = (ClampMin = "0.0", ClampMax = "0.49"))
	float MaskPadding = 0.06f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breathing|Shape", meta = (ClampMin = "0.001", ClampMax = "1.0"))
	float Feather = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breathing|Shape", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float CornerRadius = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breathing|Opacity", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BaseOpacity = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breathing|Opacity", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SlowPeakOpacity = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breathing|Opacity", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FastPeakOpacity = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Breathing|Playback")
	FLinearColor PulseColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Breathing|Playback", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PulseAmount = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Breathing|Playback", meta = (ClampMin = "0.001", Units = "s"))
	float PulsePeriod = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Breathing|Playback")
	bool bPlaying = true;

	// Time 노드 없이 Designer에서 최소/최대 밝기를 확인한다. 게임 위상에는 영향을 주지 않는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Breathing|Preview", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PreviewPhase = 0.5f;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// WBP에서 직접 레이아웃할 때는 같은 이름의 Image를 둔다. 빈 위젯은 Image를 자동 생성한다.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Breathing")
	TObjectPtr<UImage> BreathingImage;

private:
	friend class FEnemyHackEffectHUDTest;
	void UpdateAspectRatio(const FVector2D& ImageSize);
	void UpdatePlayback();
	void ApplyPlayback();
	double GetGameTime() const;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DynamicMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> SourceMaterial;

	float Phase = 0.0f;
	float CachedAspectRatio = -1.0f;
	double LastGameTime = 0.0;
};
