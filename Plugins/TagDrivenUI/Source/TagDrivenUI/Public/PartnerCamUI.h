// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EventDrivenUI.h"
#include "PartnerCamUI.generated.h"

class UImage;
class UMaterialInterface;
class UPopupRetainerBox;
class UTextureRenderTarget2D;
/**
 * 
 */
UCLASS(Blueprintable)
class TAGDRIVENUI_API UPartnerCamUI : public UEventDrivenUI
{
	GENERATED_BODY()

public:

	virtual void NativeConstruct() override;
	virtual void Activate() override;
	virtual void Deactivate() override;

	void TogglePartnerCamera();
	void SetPartnerCameraActive(bool bActive);
	void SetPartnerRenderTarget(UTextureRenderTarget2D* InRenderTarget);


	UPROPERTY(EditAnywhere, BlueprintReadOnly , Category = "UI")
	TObjectPtr<UTextureRenderTarget2D> PartnerRenderTarget;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PartnerCamMID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UIMaterial")
	TObjectPtr<UMaterialInterface> PartnerCamMaterial;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> CamImage;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UPopupRetainerBox> PopupRetainerBox;

private:
	void EnsureMaterialInitialized();

	UFUNCTION()
	void HandlePopupClosed();

	bool bHudActive = false;
	bool bCameraActive = true;
};
