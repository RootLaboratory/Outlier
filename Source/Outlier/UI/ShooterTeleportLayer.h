#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ShooterTeleportLayer.generated.h"

class UTeleportSpriteAnimation;
class UCanvasPanel;
class UWidget;

UCLASS()
class OUTLIER_API UShooterTeleportLayer : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;

	void StartTeleportAnimation();

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UCanvasPanel> AnimationCanvas;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Teleport Layer|Animation", meta = (ClampMin = "0.0"))
	float LayerTimeMultiplier = 1.0f;

private:
	void CacheSpriteAnimations(UWidget* Widget);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTeleportSpriteAnimation>> CachedSpriteAnimations;
};
