#pragma once

#include "CoreMinimal.h"
#include "UI/HackingMiniGameBase.h"
#include "SpinningCircleMiniGameWidget.generated.h"

class UHackCircleBorderWidget;
class UCanvasPanel;

USTRUCT(BlueprintType)
struct FSpinningCircleLayer
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<UHackCircleBorderWidget> BorderWidget;

	UPROPERTY(BlueprintReadOnly)
	float Radius = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float GapSize = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float AngleDegrees = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float RotationSpeedDegrees = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	uint8 bSpinning : 1 = false;
};

UCLASS(Blueprintable)
class OUTLIER_API USpinningCircleMiniGameWidget : public UHackingMiniGameBase
{
	GENERATED_BODY()

	enum class EMouseAreaResult : uint8
	{
		Valid,
		Reset,
		Finished
	};

public:

	virtual void OnMiniGameStarted() override;

	virtual void OnMiniGameUpdated(float DeltaTime) override;

	virtual void OnMiniGameFinishedEvent(EHackResult Result) override;


public:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UCanvasPanel> CircleCanvas;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpinningCircle")
	TSubclassOf<UHackCircleBorderWidget> CircleBorderWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpinningCircle", meta = (ClampMin = "2"))
	int32 MinCircleCount = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpinningCircle", meta = (ClampMin = "2"))
	int32 MaxCircleCount = 5;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpinningCircle", meta = (ClampMin = "1.0"))
	float MinCircleRadius = 80.0f;

	// Requested centerline radius; spawn clamps it so the stroke fits the background.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpinningCircle", meta = (ClampMin = "1.0"))
	float MaxCircleRadius = 280.0f;

	// WBP background image diameter. Keep the full outer ring inside this area.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpinningCircle", meta = (ClampMin = "1.0"))
	float BackgroundCircleDiameter = 796.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpinningCircle", meta = (ClampMin = "0.0"))
	float CircleThickness = 12.0f;

	// 충돌 띠의 폭이다. 이동 경로를 검사해 고속 이동 시에도 띠를 건너뛰지 않게 한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpinningCircle", meta = (ClampMin = "0.0"))
	float RingHitTolerance = 24.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpinningCircle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinGapSize = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpinningCircle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxGapSize = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpinningCircle")
	float MinRotationSpeedDegrees = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpinningCircle")
	float MaxRotationSpeedDegrees = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpinningCircle")
	FVector2D CircleCenter = FVector2D(0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpinningCircle")
	float StartAngleDegrees = -90.0f;

protected:
	/*virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;*/

private:

	UPROPERTY(Transient)
	TArray<TObjectPtr<UHackCircleBorderWidget>> CircleBorders;

	UPROPERTY(Transient)
	TArray<FSpinningCircleLayer> CircleLayers;

	FVector2D InitialMouseLocalPosition = FVector2D::ZeroVector;
	FVector2D PreviousMouseLocalPosition = FVector2D::ZeroVector;
	uint8 bJudgementActive : 1 = false;
	uint8 bPendingInitialMouseReset : 1 = false;

private:
	float GetEffectiveGapSize(float BaseGapSize) const;
	void EnsureCircleCanvas();
	void SpawnCircleBorders();
	void ClearCircleBorders();
	void UpdateCircleLayers(float DeltaTime);
	bool TryInitializeMousePosition();
	void ResetMouseToInitialPosition();
	EMouseAreaResult EvaluateMouseArea(const FVector2D& MouseLocalPosition, float DeltaTime) const;
	FVector2D GetCircleCenterLocal() const;
	FVector2D GetMouseLocalPosition() const;
	bool IsAngleInsideGap(const FSpinningCircleLayer& Layer, float AngleDegrees) const;

};
