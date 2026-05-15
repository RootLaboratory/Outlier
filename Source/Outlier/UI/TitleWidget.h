#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TitleWidget.generated.h"

class UButton;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTitleStartRequested);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTitleExitRequested);



UCLASS()
class OUTLIER_API UTitleWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;

public:
	UFUNCTION()
	void HandleStartButtonEvent();
	UFUNCTION()
	void HandleExitButtonEvent();

	UPROPERTY(BlueprintAssignable)
	FOnTitleStartRequested OnStartRequested;

	UPROPERTY(BlueprintAssignable)
	FOnTitleExitRequested OnExitRequested;

protected:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UButton> StartButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UButton> ExitButton;
};
