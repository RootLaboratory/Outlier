#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ShooterTeleportLayer.generated.h"

class UTeleportSpriteAnimation;
class UCanvasPanel;
class UWidget;
class UTexture2D;

USTRUCT(BlueprintType)
struct FTeleportSpriteAnimationSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform", meta = (ShowOnlyInnerProperties))
	FWidgetTransform Transform;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (ClampMin = "0.0"))
	float PlaybackSpeedMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	TObjectPtr<UTexture2D> Texture;
};

UCLASS()
class OUTLIER_API UShooterTeleportLayer : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void SynchronizeProperties() override;

	void StartTeleportAnimation();

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UCanvasPanel> AnimationCanvas;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTeleportSpriteAnimation> TeleportSpriteAnimation1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Teleport Layer|Animation")
	FTeleportSpriteAnimationSettings SpriteAnimation1;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTeleportSpriteAnimation> TeleportSpriteAnimation2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Teleport Layer|Animation")
	FTeleportSpriteAnimationSettings SpriteAnimation2;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTeleportSpriteAnimation> TeleportSpriteAnimation3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Teleport Layer|Animation")
	FTeleportSpriteAnimationSettings SpriteAnimation3;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTeleportSpriteAnimation> TeleportSpriteAnimation4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Teleport Layer|Animation")
	FTeleportSpriteAnimationSettings SpriteAnimation4;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTeleportSpriteAnimation> TeleportSpriteAnimation5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Teleport Layer|Animation")
	FTeleportSpriteAnimationSettings SpriteAnimation5;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTeleportSpriteAnimation> TeleportSpriteAnimation6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Teleport Layer|Animation")
	FTeleportSpriteAnimationSettings SpriteAnimation6;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTeleportSpriteAnimation> TeleportSpriteAnimation7;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Teleport Layer|Animation")
	FTeleportSpriteAnimationSettings SpriteAnimation7;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTeleportSpriteAnimation> TeleportSpriteAnimation8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Teleport Layer|Animation")
	FTeleportSpriteAnimationSettings SpriteAnimation8;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTeleportSpriteAnimation> TeleportSpriteAnimation9;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Teleport Layer|Animation")
	FTeleportSpriteAnimationSettings SpriteAnimation9;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTeleportSpriteAnimation> TeleportSpriteAnimation10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Teleport Layer|Animation")
	FTeleportSpriteAnimationSettings SpriteAnimation10;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Teleport Layer|Animation", meta = (ClampMin = "0.0"))
	float LayerTimeMultiplier = 1.0f;

private:
	void CacheSpriteAnimations(UWidget* Widget);
	void IndexSpriteAnimations();
	void CacheBaseSpriteTransforms();
	void ApplySpriteTransforms();
	void ApplySpriteTextures();
	void StartSpriteAnimation(UTeleportSpriteAnimation* SpriteAnimation, float SpeedMultiplier) const;
	void ConfigureSpriteAnimation(
		UTeleportSpriteAnimation* SpriteAnimation,
		const FWidgetTransform& LayerTransform,
		int32 SpriteIndex) const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTeleportSpriteAnimation>> CachedSpriteAnimations;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTeleportSpriteAnimation>> IndexedSpriteAnimations;

	TArray<FWidgetTransform> CachedBaseSpriteTransforms;
	bool bHasCachedBaseSpriteTransforms = false;
};
