#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InteractionOutlineWidget.generated.h"

class UImage;
class UTexture2D;

/** One outer-sphere indicator whose texture reflects the lock flag. */
UCLASS()
class OUTLIER_API UInteractionOutlineWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Interaction")
	void SetLocked(bool bInLocked);
	void HideIndicator();

protected:
	virtual void NativePreConstruct() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> StatusImage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction")
	TObjectPtr<UTexture2D> AvailableTexture;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction")
	TObjectPtr<UTexture2D> LockedTexture;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
	bool bLocked = false;

private:
	void RefreshTexture();
};
