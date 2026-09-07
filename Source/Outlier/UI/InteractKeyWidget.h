#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InteractKeyWidget.generated.h"

class UBorder;
class UTextBlock;

UCLASS()
class OUTLIER_API UInteractKeyWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void UpdateInteractKey(const FText& InInteractKeyText);
	void ClearInteractKey();

protected:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> InteractKeyText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UBorder> RoundBorder;
};
