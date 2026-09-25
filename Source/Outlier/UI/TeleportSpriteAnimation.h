#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TeleportSpriteAnimation.generated.h"

class UImage;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTexture2D;

UCLASS()
class OUTLIER_API UTeleportSpriteAnimation : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	void StartTeleportAnimation(float InLayerTimeMultiplier = 1.0f);

	void SetSpriteTexture(UTexture2D* InSpriteTexture);

	void StopAnimation();

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> TeleportTexture;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Teleport Sprite Animation|Material")
	TObjectPtr<UMaterialInterface> AnimationMaterial;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Teleport Sprite Animation|Animation", meta = (ClampMin = "0.0"))
	float TimeMultiplierMin = 0.8f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Teleport Sprite Animation|Animation", meta = (ClampMin = "0.0"))
	float TimeMultiplierMax = 1.2f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Teleport Sprite Animation|Material")
	FName TimeParameterName = TEXT("Time");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Teleport Sprite Animation|Material")
	FName SpeedParameterName = TEXT("Speed");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Teleport Sprite Animation|Material")
	FName SpriteParameterName = TEXT("Sprite");

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> AnimationMaterialInstance;

	float AnimationPhase = 0.0f;
	float EffectiveTimeMultiplier = 1.0f;
	bool bIsPlaying = false;
	bool bHasLoggedPlaybackTick = false;
};
