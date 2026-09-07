#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HackCandidateLayerWidget.generated.h"

class UCanvasPanel;
class UHackableInfoWidget;
class UHackCandidateMarkerWidget;
class UHackableComponent;
class UPartnerHackComponent;

UCLASS()
class OUTLIER_API UHackCandidateLayerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void BindHackComponent(UPartnerHackComponent* InHackComponent);
	void SetMarkerWidgetClass(TSubclassOf<UHackCandidateMarkerWidget> InMarkerWidgetClass);
	void SetHackableInfoWidgetClass(TSubclassOf<UHackableInfoWidget> InHackableInfoWidgetClass);
	void AddCandidate(AActor* TargetActor, UHackableComponent* HackableComponent);
	void RemoveCandidate(AActor* TargetActor, UHackableComponent* HackableComponent);
	void ClearMarkers();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UCanvasPanel> MarkerCanvas;

	UPROPERTY(EditDefaultsOnly, Category = "Hack")
	TSubclassOf<UHackCandidateMarkerWidget> MarkerWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Hack")
	TSubclassOf<UHackableInfoWidget> HackableInfoWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Hack")
	FVector2D MarkerSize = FVector2D(64.0f, 64.0f);


private:
	UPROPERTY()
	TObjectPtr<UPartnerHackComponent> HackComponent;

	UPROPERTY()
	TMap<TObjectPtr<AActor>, TObjectPtr<UHackCandidateMarkerWidget>> MarkerWidgets;
};
