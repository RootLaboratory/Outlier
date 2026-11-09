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
class UPopupRetainerBox;
struct FHackInfoRow;

UCLASS()
class OUTLIER_API UHackableInfoWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitializeInfo(AActor* InTargetActor, UHackableComponent* InHackableComponent);
	void SetHackProgress(float InProgress);
	void PlayPopup();
	void ResetPopup();

	float GetWidgetWidth() const { return WidgetWidth; }
	float GetMinWidgetHeight() const { return MinWidgetHeight; }
	float GetMaxWidgetHeight() const { return MaxWidgetHeight; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> InfoBorder;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UPopupRetainerBox> PopupRetainerBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UProgressBar> HackingProgressbar;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> hackingInfoImage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|InfoWidget")
	float WidgetWidth = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|InfoWidget")
	float MinWidgetHeight = 185.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|InfoWidget")
	float MaxWidgetHeight = 200.0f;

	// 이전 BP의 저장값만 보존한다. HUD 배치는 공통 설정을 사용하며 거리별 Offset은 적용하지 않는다.
	UPROPERTY()
	FVector2D MinOffset = FVector2D(28.0f, 24.0f);

	UPROPERTY()
	FVector2D MaxOffset = FVector2D(92.0f, 72.0f);

	UPROPERTY()
	FVector2D ViewportPadding = FVector2D(16.0f, 16.0f);

	UPROPERTY()
	float NearDistance = 300.0f;

	UPROPERTY()
	float FarDistance = 1200.0f;

private:
	UPROPERTY()
	TObjectPtr<AActor> TargetActor;

	UPROPERTY()
	TObjectPtr<UHackableComponent> HackableComponent;

	UPROPERTY(BlueprintReadOnly, Category = "Hack|InfoWidget", meta = (AllowPrivateAccess = "true"))
	FGameplayTag CurrentHackInfoTag;

	FSlateBrush DefaultInfoImageBrush;
	bool bCachedDefaultInfoImageBrush = false;
	bool bPopupAudioOpen = false;

	void SetPopupAudioOpen(bool bOpen);
	bool ResolveHackInfoTag(FGameplayTag& OutHackInfoTag) const;
	void UpdateInfoText();
	void UpdateInfoImage(const FHackInfoRow* HackInfo);
};
