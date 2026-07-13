#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "Interaction/InteractInfoRow.h"
#include "InteractInfoWidget.generated.h"

class USizeBox;
class UTextBlock;

UCLASS()
class OUTLIER_API UInteractInfoWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void UpdateInteractInfo(const FGameplayTag& InteractTag, const FInteractInfoRow& InteractInfo);
	void ClearInteractInfo();

protected:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> InteractText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<USizeBox> InteractSizeBox;

	UPROPERTY(BlueprintReadOnly, Category = "Interaction")
	FGameplayTag CurrentInteractTag;
};
