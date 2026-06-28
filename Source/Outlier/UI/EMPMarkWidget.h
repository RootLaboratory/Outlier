#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EMPMarkWidget.generated.h"

class UButton;
class UEMPableComponent;
class UPartnerEMPComponent;

UCLASS()
class OUTLIER_API UEMPMarkWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitializeMark(AActor* InTargetActor, UEMPableComponent* InEMPableComponent, UPartnerEMPComponent* InEMPComponent);

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> SelectButton;

private:
	UFUNCTION()
	void HandleClicked();

	UPROPERTY()
	TObjectPtr<AActor> TargetActor;

	UPROPERTY()
	TObjectPtr<UEMPableComponent> EMPableComponent;

	UPROPERTY()
	TObjectPtr<UPartnerEMPComponent> EMPComponent;
};
