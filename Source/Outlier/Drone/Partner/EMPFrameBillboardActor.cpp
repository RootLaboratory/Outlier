#include "Drone/Partner/EMPFrameBillboardActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

AEMPFrameBillboardActor::AEMPFrameBillboardActor()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	BillboardRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BillboardRoot"));
	BillboardRoot->SetupAttachment(SceneRoot);

	TopLeftFrame = CreateFrameComponent(TEXT("TopLeftFrame"));
	TopRightFrame = CreateFrameComponent(TEXT("TopRightFrame"));
	BottomLeftFrame = CreateFrameComponent(TEXT("BottomLeftFrame"));
	BottomRightFrame = CreateFrameComponent(TEXT("BottomRightFrame"));
}

void AEMPFrameBillboardActor::BeginPlay()
{
	Super::BeginPlay();

	SetCollapseAlpha(CollapseAlpha);
	ApplyBillboardTextures();
	SetFrameColorFlag(FrameColorFlag);

}

void AEMPFrameBillboardActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bFaceCamera)
	{
		UpdateBillboardRotation();

	}

	if (bAnimatingFrame)
	{
		AnimationElapsed += DeltaSeconds;

		const float Duration = FMath::Max(ActiveAnimationDuration, KINDA_SMALL_NUMBER);
		const float Alpha = FMath::Clamp(AnimationElapsed / Duration, 0.0f, 1.0f);
		SetCollapseAlpha(FMath::Lerp(AnimationStartAlpha, AnimationTargetAlpha, Alpha));
	}

	if (bAnimatingFrameColor)
	{
		FrameColorTransitionElapsed += DeltaSeconds;

		const float Duration = FMath::Max(ActiveFrameColorTransitionDuration, KINDA_SMALL_NUMBER);
		const float Alpha = FMath::Clamp(FrameColorTransitionElapsed / Duration, 0.0f, 1.0f);
		SetFrameColorFlag(Alpha);

		if (Alpha >= 1.0f)
		{
			bAnimatingFrameColor = false;
		}
	}
}

void AEMPFrameBillboardActor::PlayCollapse(float InDuration)
{
	//SetActorHiddenInGame(false);

	AnimationElapsed = 0.0f;
	AnimationStartAlpha = CollapseAlpha;
	AnimationTargetAlpha = 1.0f;
	ActiveAnimationDuration = InDuration > 0.0f ? InDuration : AnimationDuration;
	bAnimatingFrame = true;
}

void AEMPFrameBillboardActor::PlayFrameColorTransition(float InDuration)
{
	FrameColorTransitionElapsed = 0.0f;
	ActiveFrameColorTransitionDuration = InDuration > 0.0f ? InDuration : FrameColorTransitionDuration;
	SetFrameColorFlag(0.0f);
	bAnimatingFrameColor = true;
}

void AEMPFrameBillboardActor::StopFrameAnimation()
{
	bAnimatingFrame = false;
}

void AEMPFrameBillboardActor::SetCollapseAlpha(float InAlpha)
{
	CollapseAlpha = FMath::Clamp(InAlpha, 0.0f, 1.0f);

	const float HalfDistance = FMath::Lerp(ExpandedHalfDistance, CollapsedHalfDistance, CollapseAlpha);
	ApplyFrameOffset(HalfDistance);
}

void AEMPFrameBillboardActor::UpdateBillboardRotation()
{
	const APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (!CameraManager)
	{
		return;
	}

	const FVector ToCamera = CameraManager->GetCameraLocation() - GetActorLocation();
	if (ToCamera.IsNearlyZero())
	{
		return;
	}

	BillboardRoot->SetWorldRotation(
		CameraManager->GetCameraRotation()
	);

}

void AEMPFrameBillboardActor::ApplyFrameOffset(float InHalfDistance)
{
	if (TopLeftFrame)
	{
		TopLeftFrame->SetRelativeLocation(FVector(0.0f, -InHalfDistance, InHalfDistance));
		TopLeftFrame->SetRelativeScale3D(FrameScale);
	}

	if (TopRightFrame)
	{
		TopRightFrame->SetRelativeLocation(FVector(0.0f, InHalfDistance, InHalfDistance));
		TopRightFrame->SetRelativeScale3D(FrameScale);
	}

	if (BottomLeftFrame)
	{
		BottomLeftFrame->SetRelativeLocation(FVector(0.0f, -InHalfDistance, -InHalfDistance));
		BottomLeftFrame->SetRelativeScale3D(FrameScale);
	}

	if (BottomRightFrame)
	{
		BottomRightFrame->SetRelativeLocation(FVector(0.0f, InHalfDistance, -InHalfDistance));
		BottomRightFrame->SetRelativeScale3D(FrameScale);
	}
}

void AEMPFrameBillboardActor::SetFrameColorFlag(float InFlag)
{
	FrameColorFlag = FMath::Clamp(InFlag, 0.0f, 1.0f);

	if (TopLeftFrame)
	{
		TopLeftFrame->SetScalarParameterValueOnMaterials(FrameColorFlagParameterName, FrameColorFlag);
	}

	if (TopRightFrame)
	{
		TopRightFrame->SetScalarParameterValueOnMaterials(FrameColorFlagParameterName, FrameColorFlag);
	}

	if (BottomLeftFrame)
	{
		BottomLeftFrame->SetScalarParameterValueOnMaterials(FrameColorFlagParameterName, FrameColorFlag);
	}

	if (BottomRightFrame)
	{
		BottomRightFrame->SetScalarParameterValueOnMaterials(FrameColorFlagParameterName, FrameColorFlag);
	}
}

void AEMPFrameBillboardActor::ApplyBillboardTextures()
{
	ApplyTextureToStaticMesh(BottomLeftFrame, TextureLeftDown);
	ApplyTextureToStaticMesh(TopLeftFrame, TextureLeftTop);
	ApplyTextureToStaticMesh(BottomRightFrame, TextureRightDown);
	ApplyTextureToStaticMesh(TopRightFrame, RightTop);

	const FRotator PlaneFacingXForwardRotation(0.0f, 90.0f, 90.0f);

	TopLeftFrame->SetRelativeRotation(PlaneFacingXForwardRotation);
	TopRightFrame->SetRelativeRotation(PlaneFacingXForwardRotation);
	BottomLeftFrame->SetRelativeRotation(PlaneFacingXForwardRotation);
	BottomRightFrame->SetRelativeRotation(PlaneFacingXForwardRotation);
}

void AEMPFrameBillboardActor::ApplyTextureToStaticMesh(UStaticMeshComponent* MeshComponent, UTexture2D* Texture)
{
	if (!MeshComponent || !Texture)
	{
		return;
	}

	UMaterialInterface* SourceMaterial = MeshComponent->GetMaterial(0);
	if (!SourceMaterial)
	{
		return;
	}

	UMaterialInstanceDynamic* DynamicMaterial =
		MeshComponent->CreateDynamicMaterialInstance(0, SourceMaterial);

	if (!DynamicMaterial)
	{
		return;
	}

	DynamicMaterial->SetTextureParameterValue(TEXT("Texture"), Texture);
}



UStaticMeshComponent* AEMPFrameBillboardActor::CreateFrameComponent(const FName ComponentName)
{
	UStaticMeshComponent* FrameComponent = CreateDefaultSubobject<UStaticMeshComponent>(ComponentName);
	FrameComponent->SetupAttachment(BillboardRoot);
	FrameComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FrameComponent->SetGenerateOverlapEvents(false);
	FrameComponent->SetCastShadow(false);
	FrameComponent->SetRelativeScale3D(FrameScale);
	return FrameComponent;
}
