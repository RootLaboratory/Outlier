#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "Interaction/InteractInfoRow.h"
#include "InteractionDescWidget.generated.h"

class UPopupRetainerBox;
class UProgressBar;
class UTextBlock;

UCLASS()
class OUTLIER_API UInteractionDescWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void UpdateInteractionDesc(const FGameplayTag& InteractTag, const FInteractInfoRow& InteractInfo, float InProgress);
	void SetProgress(float InProgress);
	void ClearInteractionDesc();
	void PlayPopUp(bool InFlag);

protected:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UPopupRetainerBox> PopupRetainerBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> DescText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UProgressBar> ProgressBar;

	UPROPERTY(BlueprintReadOnly, Category = "Interaction")
	FGameplayTag CurrentInteractTag;
};
