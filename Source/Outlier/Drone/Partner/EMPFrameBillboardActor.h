#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EMPFrameBillboardActor.generated.h"

class UStaticMeshComponent;
class USceneComponent;

UCLASS()
class OUTLIER_API AEMPFrameBillboardActor : public AActor
{
	GENERATED_BODY()

public:
	AEMPFrameBillboardActor();

	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "EMP Frame")
	void PlayCollapse(float InDuration = -1.0f);

	UFUNCTION(BlueprintCallable, Category = "EMP Frame")
	void PlayFrameColorTransition(float InDuration = -1.0f);

	UFUNCTION(BlueprintCallable, Category = "EMP Frame")
	void StopFrameAnimation();

	UFUNCTION(BlueprintCallable, Category = "EMP Frame")
	void SetCollapseAlpha(float InAlpha);

	UPROPERTY(EditAnywhere, Category = "LeftDown")
	TObjectPtr<UTexture2D> TextureLeftDown;

	UPROPERTY(EditAnywhere, Category = "LeftTop")
	TObjectPtr<UTexture2D> TextureLeftTop;

	UPROPERTY(EditAnywhere, Category = "RightDown")
	TObjectPtr<UTexture2D> TextureRightDown;

	UPROPERTY(EditAnywhere, Category = "RightTop")
	TObjectPtr<UTexture2D> RightTop;

	

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> BillboardRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> TopLeftFrame;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> TopRightFrame;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BottomLeftFrame;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BottomRightFrame;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMP Frame")
	float ExpandedHalfDistance = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMP Frame")
	float CollapsedHalfDistance = 16.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMP Frame")
	float AnimationDuration = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMP Frame")
	float FrameColorTransitionDuration = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMP Frame")
	FName FrameColorFlagParameterName = TEXT("Flag");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMP Frame")
	FVector FrameScale = FVector(2.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMP Frame")
	uint8 bFaceCamera : 1 = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMP Frame")
	FRotator BillboardRotationOffset = FRotator(0.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMP Frame")
	uint8 bHideWhenFinished : 1 = false;

private:
	void UpdateBillboardRotation();
	void ApplyFrameOffset(float InHalfDistance);
	void ApplyBillboardTextures();
	void SetFrameColorFlag(float InFlag);
	void ApplyTextureToStaticMesh(UStaticMeshComponent* MeshComponent,UTexture2D* Texture);

	UStaticMeshComponent* CreateFrameComponent(const FName ComponentName);

	float CollapseAlpha = 0.0f;
	float AnimationElapsed = 0.0f;
	float AnimationStartAlpha = 0.0f;
	float AnimationTargetAlpha = 1.0f;
	float ActiveAnimationDuration = 1.0f;
	uint8 bAnimatingFrame : 1 = false;

	float FrameColorFlag = 0.0f;
	float FrameColorTransitionElapsed = 0.0f;
	float ActiveFrameColorTransitionDuration = 1.0f;
	uint8 bAnimatingFrameColor : 1 = false;
};
