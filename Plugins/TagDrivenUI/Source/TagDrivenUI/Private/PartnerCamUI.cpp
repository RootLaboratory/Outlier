// Fill out your copyright notice in the Description page of Project Settings.


#include "PartnerCamUI.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/Image.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

void UPartnerCamUI::NativeConstruct()
{
	Super::NativeConstruct();

	PartnerCamMID = UMaterialInstanceDynamic::Create(PartnerCamMaterial, this);
}

void UPartnerCamUI::TogglePartnerCamera()
{
	bFlag = !bFlag;

	if (bFlag)
	{
		Activate();
	}

	else
	{
		Deactivate();
	}

}

void UPartnerCamUI::SetPartnerRenderTarget(UTextureRenderTarget2D* InRenderTarget)
{


	if (InRenderTarget && PartnerCamMID)
	{
		//UE_LOG(LogTemp, Error, TEXT("RenderTargetBinded"));
		PartnerRenderTarget = InRenderTarget;

		if (!PartnerCamMID)
		{
			//UE_LOG(LogTemp, Error, TEXT("SetPartnerRenderTarget: PartnerCamMID is null, waiting for NativeConstruct"));
			return;
		}
		else
		{
		PartnerCamMID->SetTextureParameterValue(TEXT("PartnerRT"), PartnerRenderTarget.Get());
		//UE_LOG(LogTemp, Error, TEXT("PartnerCamMID"));

		}

		if (CamImage)
		{
			//UE_LOG(LogTemp, Error, TEXT("CamImage"));

			CamImage->SetBrushFromMaterial(PartnerCamMID);
		}
		else
		{
			//UE_LOG(LogTemp, Error, TEXT("CamImage: CamImage is null, waiting for NativeConstruct"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Cant RenderTargetBinded"));

	}

}
