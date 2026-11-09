#pragma once

#include "CoreMinimal.h"
#include "UI/TargetPointHudWidget.h"
#include "GameplayTagContainer.h"
#include "Interaction/InteractInfoRow.h"
#include "InteractionDescWidget.generated.h"

class UPopupRetainerBox;
class UProgressBar;
class UTextBlock;

UCLASS()
class OUTLIER_API UInteractionDescWidget : public UTargetPointWidget
{
	GENERATED_BODY()

public:
	void UpdateInteractionDesc(const FGameplayTag& InteractTag, const FInteractInfoRow& InteractInfo, float InProgress);
	void SetProgress(float InProgress);
	void ClearInteractionDesc();
	void PlayPopUp(bool InFlag);

protected:
	virtual void NativeDestruct() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UPopupRetainerBox> PopupRetainerBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> DescText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UProgressBar> ProgressBar;

	UPROPERTY(BlueprintReadOnly, Category = "Interaction")
	FGameplayTag CurrentInteractTag;

private:
	void SetPopupAudioOpen(bool bOpen);

	bool bPopupAudioOpen = false;
};
