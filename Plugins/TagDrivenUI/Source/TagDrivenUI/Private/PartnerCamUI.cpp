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

	EnsureMaterialInitialized();

	if (PopupRetainerBox)
	{
		PopupRetainerBox->ResetPopup();
		PopupRetainerBox->OnClosed.RemoveDynamic(this, &UPartnerCamUI::HandlePopupClosed);
		PopupRetainerBox->OnClosed.AddDynamic(this, &UPartnerCamUI::HandlePopupClosed);
	}

	SetVisibility(ESlateVisibility::Collapsed);
}

void UPartnerCamUI::Activate()
{
	if (bHudActive)
	{
		EnsureMaterialInitialized();
		if (PopupRetainerBox && bCameraActive)
		{
			PopupRetainerBox->RequestRender();
		}
		return;
	}

	bHudActive = true;
	if (!bCameraActive)
	{
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	EnsureMaterialInitialized();
	Super::Activate();

	UE_LOG(LogTemp, Error, TEXT("Called"));

	if (PopupRetainerBox)
	{
		PopupRetainerBox->ResetPopup();
		PopupRetainerBox->PlayOpen();
	}
}

void UPartnerCamUI::Deactivate()
{
	bHudActive = false;
	if (PopupRetainerBox)
	{
		PopupRetainerBox->ResetPopup();
	}
	Super::Deactivate();
}

void UPartnerCamUI::TogglePartnerCamera()
{
	SetPartnerCameraActive(!bCameraActive);
}

void UPartnerCamUI::SetPartnerCameraActive(bool bActive)
{
	if (bCameraActive == bActive)
	{
		return;
	}

	bCameraActive = bActive;
	if (!bHudActive)
	{
		return;
	}

	if (bCameraActive)
	{
		EnsureMaterialInitialized();
		Super::Activate();

		if (PopupRetainerBox)
		{
			PopupRetainerBox->ResetPopup();
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
			Super::Deactivate();
		}
	}
}

void UPartnerCamUI::HandlePopupClosed()
{
	if (!bCameraActive || !bHudActive)
	{
		Super::Deactivate();
	}
}

void UPartnerCamUI::SetPartnerRenderTarget(UTextureRenderTarget2D* InRenderTarget)
{
	PartnerRenderTarget = InRenderTarget;
	EnsureMaterialInitialized();
}

void UPartnerCamUI::EnsureMaterialInitialized()
{
	if (!PartnerCamMID && PartnerCamMaterial)
	{
		PartnerCamMID = UMaterialInstanceDynamic::Create(PartnerCamMaterial, this);
	}

	if (!PartnerCamMID || !PartnerRenderTarget)
	{
		return;
	}

	PartnerCamMID->SetTextureParameterValue(TEXT("PartnerRT"), PartnerRenderTarget.Get());
	if (CamImage)
	{
		CamImage->SetBrushFromMaterial(PartnerCamMID);
	}
}
