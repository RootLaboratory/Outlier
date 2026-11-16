#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/DeveloperConvenienceTypes.h"
#include "DeveloperConvenienceOptionWidget.generated.h"

class AFirstPersonPlayerController;
class UButton;
class UTextBlock;

// 같은 WBP를 배치하고 Option만 바꿔 세 가지 공유 옵션을 표시한다.
UCLASS(Blueprintable)
class OUTLIER_API UDeveloperConvenienceOptionWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Developer Convenience", meta = (ExposeOnSpawn = "true"))
	EDeveloperConvenienceOption Option = EDeveloperConvenienceOption::Invincible;

	// 비워 두면 선택한 옵션의 기본 문구를 사용한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Developer Convenience")
	FText TitleText;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Developer Convenience")
	FText DescriptionText;

protected:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Title;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Desc;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ONOFF;

private:
	friend class FDeveloperConvenienceOptionWidgetTest;
	void RefreshLabels();
	bool GetOptionValue(const FDeveloperConvenienceOptions& Options) const;
	UFUNCTION()
	void HandleStateChanged(const FDeveloperConvenienceSnapshot& Snapshot);
	UFUNCTION()
	void HandleClicked();

	TWeakObjectPtr<AFirstPersonPlayerController> ObservedController;
	FDeveloperConvenienceSnapshot Context;
	bool bContextActive = false;
};
