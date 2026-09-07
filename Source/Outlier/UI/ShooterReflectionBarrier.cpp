#include "UI/ShooterReflectionBarrier.h"

#include "Components/Image.h"
#include "Engine/Texture.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
const FName ProgressParameterName(TEXT("Progress"));
const FName RippleProgressParameterName(TEXT("RippleProgress"));
const FName CenterParameterName(TEXT("Center"));
const FName RippleRadiusParameterName(TEXT("RippleRadius"));
const FName RippleHardnessParameterName(TEXT("RippleHardness"));
const FName SampleTextureParameterName(TEXT("SampleTexture"));
}

void UShooterReflectionBarrier::NativeConstruct()
{
	Super::NativeConstruct();

	if (BarrierTexture && ActivationMaterial)
	{
		// Preserve the texture configured on the WBP Image before replacing its
		// brush resource with the activation material.
		UTexture* SampleTexture = Cast<UTexture>(
			BarrierTexture->GetBrush().GetResourceObject());

		BarrierTexture->SetBrushFromMaterial(ActivationMaterial);
		ActivationMaterialInstance = BarrierTexture->GetDynamicMaterial();
		if (ActivationMaterialInstance)
		{
			if (SampleTexture)
			{
				ActivationMaterialInstance->SetTextureParameterValue(
					SampleTextureParameterName,
					SampleTexture);
			}

			ActivationMaterialInstance->SetVectorParameterValue(
				CenterParameterName,
				FLinearColor(0.5f, 0.5f, 0.0f, 0.0f));
		}
	}

	Init();
}

void UShooterReflectionBarrier::NativeTick(
	const FGeometry& MyGeometry,
	float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (bIsProgressUpdating)
	{
		Update(InDeltaTime);
	}
	if (bIsRippleUpdating)
	{
		UpdateRipple(InDeltaTime);
	}
}

void UShooterReflectionBarrier::Init()
{
	if (!ActivationMaterialInstance)
	{
		bIsProgressUpdating = false;
		return;
	}

	Progress = 0.0f;
	bIsProgressUpdating = true;
	ActivationMaterialInstance->SetScalarParameterValue(ProgressParameterName, Progress);
	ActivationMaterialInstance->SetScalarParameterValue(
		RippleRadiusParameterName,
		RippleRadius);
	ActivationMaterialInstance->SetScalarParameterValue(
		RippleHardnessParameterName,
		RippleHardness);
	RippleProgress = 1.0f;
	bIsRippleUpdating = false;
	ActivationMaterialInstance->SetScalarParameterValue(
		RippleProgressParameterName,
		RippleProgress);

	if (ProgressDuration <= 0.0f)
	{
		Progress = 1.0f;
		ActivationMaterialInstance->SetScalarParameterValue(ProgressParameterName, Progress);
		bIsProgressUpdating = false;
	}
}

void UShooterReflectionBarrier::PlayHitRipple(const FVector& IncomingOrigin)
{
	if (!ActivationMaterialInstance)
	{
		return;
	}

	APlayerController* PlayerController = GetOwningPlayer();
	if (!PlayerController)
	{
		return;
	}

	FVector2D CenterUV(0.5f, 0.5f);
	FVector2D ScreenPosition = FVector2D::ZeroVector;
	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	PlayerController->GetViewportSize(ViewportWidth, ViewportHeight);

	const bool bProjected = ViewportWidth > 0
		&& ViewportHeight > 0
		&& PlayerController->ProjectWorldLocationToScreen(
			IncomingOrigin,
			ScreenPosition,
			true);
	if (bProjected)
	{
		CenterUV.X = ScreenPosition.X / static_cast<float>(ViewportWidth);
		CenterUV.Y = ScreenPosition.Y / static_cast<float>(ViewportHeight);
	}
	else
	{
		FVector CameraLocation;
		FRotator CameraRotation;
		PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotation);

		const FVector WorldDirection = (IncomingOrigin - CameraLocation).GetSafeNormal();
		const FVector LocalDirection = CameraRotation.UnrotateVector(WorldDirection);
		const float DirectionAngle = FMath::Atan2(LocalDirection.Y, -LocalDirection.X);
		CenterUV.X = 0.5f + FMath::Sin(DirectionAngle) * 0.46f;
		CenterUV.Y = 0.5f + FMath::Cos(DirectionAngle) * 0.46f - LocalDirection.Z * 0.2f;
	}

	CenterUV.X = FMath::Clamp(CenterUV.X, 0.025f, 0.975f);
	CenterUV.Y = FMath::Clamp(CenterUV.Y, 0.025f, 0.975f);
	ActivationMaterialInstance->SetVectorParameterValue(
		CenterParameterName,
		FLinearColor(CenterUV.X, CenterUV.Y, 0.0f, 0.0f));

	RippleProgress = 0.0f;
	bIsRippleUpdating = true;
	ActivationMaterialInstance->SetScalarParameterValue(
		RippleProgressParameterName,
		RippleProgress);

	if (RippleDuration <= 0.0f)
	{
		RippleProgress = 1.0f;
		ActivationMaterialInstance->SetScalarParameterValue(
			RippleProgressParameterName,
			RippleProgress);
		bIsRippleUpdating = false;
	}
}

void UShooterReflectionBarrier::Update(float DeltaTime)
{
	if (!ActivationMaterialInstance || !bIsProgressUpdating)
	{
		return;
	}

	Progress = FMath::Clamp(
		Progress + DeltaTime * TimeScale / FMath::Max(ProgressDuration, KINDA_SMALL_NUMBER),
		0.0f,
		1.0f);
	ActivationMaterialInstance->SetScalarParameterValue(ProgressParameterName, Progress);

	if (Progress >= 1.0f)
	{
		bIsProgressUpdating = false;
	}
}

void UShooterReflectionBarrier::UpdateRipple(float DeltaTime)
{
	if (!ActivationMaterialInstance || !bIsRippleUpdating)
	{
		return;
	}

	RippleProgress = FMath::Clamp(
		RippleProgress + DeltaTime * RippleTimeScale
			/ FMath::Max(RippleDuration, KINDA_SMALL_NUMBER),
		0.0f,
		1.0f);
	ActivationMaterialInstance->SetScalarParameterValue(
		RippleProgressParameterName,
		RippleProgress);

	if (RippleProgress >= 1.0f)
	{
		bIsRippleUpdating = false;
	}
}
