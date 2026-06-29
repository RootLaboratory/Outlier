#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EMPLayerWidget.generated.h"

class UCanvasPanel;
class UImage;
class UMaterialInstanceDynamic;
class UProgressBar;
class UEMPMarkWidget;
class UEMPableComponent;
class UPartnerEMPComponent;

UCLASS()
class OUTLIER_API UEMPLayerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void BindEMPComponent(UPartnerEMPComponent* InEMPComponent);
	void SetMarkWidgetClass(TSubclassOf<UEMPMarkWidget> InMarkWidgetClass);
	void InitializeMarkingTimer(float Duration);
	void AddCandidate(AActor* TargetActor, UEMPableComponent* EMPableComponent);
	void RemoveCandidate(AActor* TargetActor, UEMPableComponent* EMPableComponent);
	void ClearMarkers();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UCanvasPanel> MarkerCanvas;

	// BP에서 배치하거나 코드에서 fallback 생성
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UProgressBar> EMPProgressBar;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> EMPRevealImage;

	UPROPERTY(EditDefaultsOnly, Category = "EMP")
	TSubclassOf<UEMPMarkWidget> MarkWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "EMP")
	FVector2D MarkerSize = FVector2D(64.0f, 64.0f);

	UPROPERTY(EditDefaultsOnly, Category = "EMP|Reveal")
	float RevealDuration = 0.35f;

	UPROPERTY(EditDefaultsOnly, Category = "EMP|Reveal")
	float RevealMaxRadius = 0.7072f;

	UPROPERTY(EditDefaultsOnly, Category = "EMP|Reveal")
	FName RevealRadiusParameterName = TEXT("RevealRadius");

private:
	void ExpireEMP();
	void StartReveal();
	void UpdateReveal(float InDeltaTime);
	void EnsureRevealMaterial();

	UPROPERTY()
	TObjectPtr<UPartnerEMPComponent> EMPComponent;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RevealMaterial;

	UPROPERTY()
	TMap<TObjectPtr<AActor>, TObjectPtr<UEMPMarkWidget>> MarkWidgets;

	float MarkingDuration = 3.0f;
	float ElapsedTime = 0.0f;
	float RevealElapsedTime = 0.0f;

	bool bTimerActive = false;
	bool bFinished = false;
	bool bRevealActive = false;
};
