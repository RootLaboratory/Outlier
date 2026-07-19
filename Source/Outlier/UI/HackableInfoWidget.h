#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "HackableInfoWidget.generated.h"

class UBorder;
class UHackableComponent;
class UImage;
class UProgressBar;
class UTextBlock;

UCLASS()
class OUTLIER_API UHackableInfoWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitializeInfo(AActor* InTargetActor, UHackableComponent* InHackableComponent);
	void SetHackProgress(float InProgress);

	float GetWidgetWidth() const { return WidgetWidth; }
	float GetMinWidgetHeight() const { return MinWidgetHeight; }
	float GetMaxWidgetHeight() const { return MaxWidgetHeight; }
	FVector2D GetViewportPadding() const { return ViewportPadding; }
	FVector2D GetOffsetForDistance(float Distance) const;

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> InfoBorder;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UProgressBar> HackingProgressbar;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UBorder> hackingInputBorder;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> hackingInputKeytext;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> hackingInfoImage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|InfoWidget")
	float WidgetWidth = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|InfoWidget")
	float MinWidgetHeight = 185.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|InfoWidget")
	float MaxWidgetHeight = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|InfoWidget")
	FVector2D MinOffset = FVector2D(28.0f, 24.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|InfoWidget")
	FVector2D MaxOffset = FVector2D(92.0f, 72.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|InfoWidget")
	FVector2D ViewportPadding = FVector2D(16.0f, 16.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|InfoWidget", meta = (ClampMin = "0.01"))
	float NearDistance = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|InfoWidget", meta = (ClampMin = "0.01"))
	float FarDistance = 1200.0f;

private:
	UPROPERTY()
	TObjectPtr<AActor> TargetActor;

	UPROPERTY()
	TObjectPtr<UHackableComponent> HackableComponent;

	UPROPERTY(BlueprintReadOnly, Category = "Hack|InfoWidget", meta = (AllowPrivateAccess = "true"))
	FGameplayTag CurrentHackInfoTag;

	bool ResolveHackInfoTag(FGameplayTag& OutHackInfoTag) const;
	void UpdateInfoText();
};
