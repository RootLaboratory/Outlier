#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DeveloperConvenienceActionWidget.generated.h"

class UButton;
class UTextBlock;

UENUM(BlueprintType)
enum class EDeveloperConvenienceAction : uint8
{
	TeamRecovery,
	LevelSelection,
	ReturnToTitle
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDeveloperConvenienceActionClicked,
	EDeveloperConvenienceAction, Action);

// 클릭은 메뉴에 전달하고, 요청/결과/선택 화면 수명은 메뉴가 함께 관리한다.
UCLASS(Blueprintable)
class OUTLIER_API UDeveloperConvenienceActionWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Developer Convenience", meta = (ExposeOnSpawn = "true"))
	EDeveloperConvenienceAction Action = EDeveloperConvenienceAction::TeamRecovery;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Developer Convenience")
	FText TitleText;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Developer Convenience")
	FText DescriptionText;
	UPROPERTY(BlueprintAssignable, Category = "Developer Convenience")
	FOnDeveloperConvenienceActionClicked OnActionClicked;

protected:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Title;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Desc;

private:
	void RefreshLabels();
	UFUNCTION()
	void HandleClicked();
};
