#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InputCoreTypes.h"
#include "CinematicHintkey.generated.h"

class UInputActionKeyDisplayWidget;
class UTextBlock;

/** 시네마틱 재생 동안 표시하는 스킵 키와 안내 문구. */
UCLASS(Abstract, Blueprintable)
class OUTLIER_API UCinematicHintkey : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Cinematic")
	void SetCompleteCinematicKey(FKey Key);

	UFUNCTION(BlueprintCallable, Category = "Cinematic")
	void SetHintText(const FText& InText);

	virtual void SynchronizeProperties() override;

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UInputActionKeyDisplayWidget> InputDisplayWidget;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> HintText;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cinematic")
	FText HintMessage = NSLOCTEXT("CinematicHintkey", "Skip", "스킵");

private:
	void RefreshHint();
	FKey CompleteCinematicKey = EKeys::Enter;
};
