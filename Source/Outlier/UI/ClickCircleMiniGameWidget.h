#pragma once

#include "CoreMinimal.h"
#include "UI/HackingMiniGameBase.h"
#include "ClickCircleMiniGameWidget.generated.h"

class UCanvasPanel;
class UHackCircleBorderWidget;
class UMaterialInterface;

UENUM(BlueprintType)
enum class EClickCirclePointerDirection : uint8
{
	Clockwise,
	CounterClockwise
};

UENUM(BlueprintType)
enum class EClickCircleMouseResult : uint8
{
	Clear,
	Failed,
	Valid
};

USTRUCT(BlueprintType)
struct FClickCircleWidget
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<UHackCircleBorderWidget> BaseBorderWidget;

	UPROPERTY(Transient)
	TObjectPtr<UHackCircleBorderWidget> ValidBorderWidget;

	UPROPERTY(Transient)
	TObjectPtr<UHackCircleBorderWidget> PointerBorderWidget;

	UPROPERTY(BlueprintReadOnly)
	float ClickRadius = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float ValidAngleDegrees = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	uint8 bCleared : 1 = false;
};

USTRUCT(BlueprintType)
struct FSpinningPointer
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	float Speed = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	EClickCirclePointerDirection Direction = EClickCirclePointerDirection::Clockwise;

	UPROPERTY(BlueprintReadOnly)
	float AngleDegrees = 0.0f;
};

UCLASS(Blueprintable)
class OUTLIER_API UClickCircleMiniGameWidget : public UHackingMiniGameBase
{
	GENERATED_BODY()

public:
	virtual void OnMiniGameStarted() override;
	virtual void OnMiniGameUpdated(float DeltaTime) override;
	virtual void OnMiniGameFinishedEvent(EHackResult Result) override;

public:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UCanvasPanel> CircleCanvas;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ClickCircle")
	TSubclassOf<UHackCircleBorderWidget> CircleBorderWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ClickCircle|Materials")
	TObjectPtr<UMaterialInterface> BaseCircleMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ClickCircle|Materials")
	TObjectPtr<UMaterialInterface> ValidCircleMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ClickCircle|Materials")
	TObjectPtr<UMaterialInterface> PointerCircleMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ClickCircle", meta = (ClampMin = "2"))
	int32 MinCircleCount = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ClickCircle", meta = (ClampMin = "2"))
	int32 MaxCircleCount = 5;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ClickCircle", meta = (ClampMin = "1.0"))
	float MinCircleRadius = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ClickCircle", meta = (ClampMin = "1.0"))
	float MaxCircleRadius = 280.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ClickCircle", meta = (ClampMin = "0.0"))
	float CircleThickness = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ClickCircle", meta = (ClampMin = "0.0"))
	float RingHitTolerance = 24.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ClickCircle", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float ClickAngleToleranceDegrees = 24.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ClickCircle", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float PointerArcDegrees = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ClickCircle")
	float MinRotationSpeedDegrees = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ClickCircle")
	float MaxRotationSpeedDegrees = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ClickCircle")
	FVector2D CircleCenter = FVector2D(0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ClickCircle")
	float StartAngleDegrees = -90.0f;

protected:
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	UPROPERTY(Transient)
	TArray<FClickCircleWidget> ClickCircleWidgets;

	UPROPERTY(Transient)
	FSpinningPointer SpinningPointer;

	int32 CurrentCircleIndex = INDEX_NONE;

private:
	void EnsureCircleCanvas();
	void SpawnCircleWidgets();
	void ClearCircleWidgets();
	UHackCircleBorderWidget* CreateCircleBorderLayer(float Radius, float Amount, float AngleDegrees, UMaterialInterface* Material);
	void UpdateSpinningPointer(float DeltaTime);
	void AdvanceToNextCircle();
	void RefreshCircleVisuals();
	void RandomizeSpinningPointer();
	EClickCircleMouseResult EvaluateMouseClick() const;
	FVector2D GetCircleCenterLocal() const;
	float GetAngularDistanceDegrees(float A, float B) const;
	float GetPointerSpeedScale() const;
};
