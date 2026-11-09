#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/UILayerTypes.h"
#include "TargetPointHudWidget.generated.h"

class APlayerController;
class UCanvasPanel;

// 월드 위치 보정은 두지 않는다. 기준점 위치와 거리만으로 화면 배치를 결정한다.
USTRUCT(BlueprintType)
struct OUTLIER_API FTargetPointHudSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD", meta = (ClampMin = "1.0", Units = "cm"))
	float ReferenceDistance = 200.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD", meta = (ClampMin = "0.01"))
	float MinScale = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD", meta = (ClampMin = "0.01"))
	float MaxScale = 1.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD", meta = (ClampMin = "0.0"))
	float ScreenPadding = 16.0f;

	float GetDistanceScale(float Distance) const;
	void CalculateLayout(FVector2D ScreenPoint, FVector2D ViewportSize, FVector2D ContentSize,
		float Distance, FVector2D& OutPosition, float& OutScale) const;
	bool Project(APlayerController* Controller, const FVector& WorldPoint, FVector2D ContentSize,
		FVector2D& OutPosition, float& OutScale) const;
};

// 같은 위젯 BP를 쓰는 모든 대상이 표시 설정을 공유한다. Actor는 기준점 위치만 제공한다.
UCLASS(Blueprintable)
class OUTLIER_API UTargetPointWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction|HUD")
	FTargetPointHudSettings HudPresentation;
};

// 소유 플레이어의 Gameplay 레이어에 표시한다. 갱신은 기존 표시 소유자의 활성 경로에서 호출한다.
UCLASS()
class OUTLIER_API UTargetPointHudWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	static UTargetPointHudWidget* CreateForPlayer(APlayerController* Controller, UObject* RequestOwner);
	void Release();
	void AddContent(UUserWidget* Content);
	void UpdateContent(UTargetPointWidget* Content, bool bShown, const FVector& WorldPoint, FVector2D FallbackSize);
protected:
	virtual void NativeOnInitialized() override;
private:
	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> ContentCanvas;
	FUILayerHandle LayerHandle;
};
