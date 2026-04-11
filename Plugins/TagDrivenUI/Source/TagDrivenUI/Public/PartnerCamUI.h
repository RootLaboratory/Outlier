// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EventDrivenUI.h"
#include "PartnerCamUI.generated.h"

class UImage;
class UMaterialInterface;
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
	void SetPartnerCamera(bool bInFlag);
	void SetPartnerRenderTarget(UTextureRenderTarget2D* InRenderTarget);


	UPROPERTY(EditAnywhere, BlueprintReadOnly , Category = "UI")
	TObjectPtr<UTextureRenderTarget2D> PartnerRenderTarget;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PartnerCamMID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UIMaterial")
	TObjectPtr<UMaterialInterface> PartnerCamMaterial;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> CamImage;

private:
	//RT를 받고, 초기화 때 Camera Component 받게 해야함. 
	uint8 bFlag : 1 = true;
};
