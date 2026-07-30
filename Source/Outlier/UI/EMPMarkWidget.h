#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EMPMarkWidget.generated.h"

class UButton;
class UEMPableComponent;
class UPartnerEMPComponent;
class AEMPFrameBillboardActor;
class USceneComponent;

UCLASS()
class OUTLIER_API UEMPMarkWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitializeMark(AActor* InTargetActor, UEMPableComponent* InEMPableComponent, UPartnerEMPComponent* InEMPComponent);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CollapsedTime")
	float CollaspsedTime = 1.0f;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> SelectButton;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FrameActor")
	TSubclassOf<AEMPFrameBillboardActor> BillboardFrameClass;

	

private:
	void DisableSelectButtonFocus();

	UFUNCTION()
	void HandleClicked();

	void CacheTargetSceneComponent();
	void SpawnBillboardFrame();
	void DestroyBillboardFrame();
	void AlignBillboardFrameToTarget() const;

	UPROPERTY()
	TObjectPtr<AActor> TargetActor;

	UPROPERTY()
	TObjectPtr<USceneComponent> TargetSceneComponent;

	UPROPERTY()
	TObjectPtr<UEMPableComponent> EMPableComponent;

	UPROPERTY()
	TObjectPtr<UPartnerEMPComponent> EMPComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FrameActor", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<AEMPFrameBillboardActor> BillboardFrame;

};
