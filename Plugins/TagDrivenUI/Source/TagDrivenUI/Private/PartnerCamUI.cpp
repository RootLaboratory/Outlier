// Fill out your copyright notice in the Description page of Project Settings.


#include "PartnerCamUI.h"

#include "Components/Image.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PopupRetainerBox.h"

void UPartnerCamUI::NativeConstruct()
{
	Super::NativeConstruct();

	PartnerCamMID = UMaterialInstanceDynamic::Create(PartnerCamMaterial, this);

	if (PopupRetainerBox)
	{
		PopupRetainerBox->ResetPopup();
		PopupRetainerBox->OnClosed.RemoveDynamic(this, &UPartnerCamUI::HandlePopupClosed);
		PopupRetainerBox->OnClosed.AddDynamic(this, &UPartnerCamUI::HandlePopupClosed);
	}

	if (PartnerRenderTarget && PartnerCamMID)
	{
		PartnerCamMID->SetTextureParameterValue(TEXT("PartnerRT"), PartnerRenderTarget.Get());

		if (CamImage)
		{
			CamImage->SetBrushFromMaterial(PartnerCamMID);
		}
	}
}

void UPartnerCamUI::TogglePartnerCamera()
{
	bFlag = !bFlag;

	if (bFlag)
	{
		Activate();

		if (PopupRetainerBox)
		{
			PopupRetainerBox->PlayOpen();
		}
	}

	else
	{
		if (PopupRetainerBox)
		{
			PopupRetainerBox->PlayClose();
		}
		else
		{
			Deactivate();
		}
	}

}

void UPartnerCamUI::HandlePopupClosed()
{
	if (!bFlag)
	{
		Deactivate();
	}
}

void UPartnerCamUI::SetPartnerRenderTarget(UTextureRenderTarget2D* InRenderTarget)
{
	PartnerRenderTarget = InRenderTarget;

	if (InRenderTarget && PartnerCamMID)
	{
		//UE_LOG(LogTemp, Error, TEXT("RenderTargetBinded"));
		PartnerCamMID->SetTextureParameterValue(TEXT("PartnerRT"), PartnerRenderTarget.Get());
		//UE_LOG(LogTemp, Error, TEXT("PartnerCamMID"));

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
