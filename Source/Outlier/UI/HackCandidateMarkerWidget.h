#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HackCandidateMarkerWidget.generated.h"

class UButton;
class UHackableComponent;
class UPartnerHackComponent;

UCLASS()
class OUTLIER_API UHackCandidateMarkerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitializeMarker(AActor* InTargetActor, UHackableComponent* InHackableComponent, UPartnerHackComponent* InHackComponent);

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
	TObjectPtr<UHackableComponent> HackableComponent;

	UPROPERTY()
	TObjectPtr<UPartnerHackComponent> HackComponent;
};
