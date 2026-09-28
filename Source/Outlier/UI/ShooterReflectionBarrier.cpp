#include "UI/ShooterReflectionBarrier.h"

#include "Components/Image.h"
#include "Engine/Texture.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
const FName ProgressParameterName(TEXT("Progress"));
const FName SampleTextureParameterName(TEXT("SampleTexture"));
const FName AspectRatioParameterName(TEXT("AspectRatio"));
const FName RippleSlotParameterNames[] = {
	FName(TEXT("Ripple0")),
	FName(TEXT("Ripple1")),
	FName(TEXT("Ripple2")),
	FName(TEXT("Ripple3")),
};
static_assert(
	UE_ARRAY_COUNT(RippleSlotParameterNames) == UShooterReflectionBarrier::MaxRippleSlots,
	"Ripple slot parameter names must match MaxRippleSlots.");
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
		if (ActivationMaterialInstance && SampleTexture)
		{
			ActivationMaterialInstance->SetTextureParameterValue(
				SampleTextureParameterName,
				SampleTexture);
		}
	}

	Init();
}

void UShooterReflectionBarrier::NativeTick(
	const FGeometry& MyGeometry,
	float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	UpdateAspectRatio(MyGeometry);
	if (bIsProgressUpdating)
	{
		Update(InDeltaTime);
	}
	if (bIsRippleUpdating)
	{
		UpdateRipples(InDeltaTime);
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

	for (int32 SlotIndex = 0; SlotIndex < MaxRippleSlots; ++SlotIndex)
	{
		RippleSlots[SlotIndex] = FShooterReflectionRippleSlot();
		PushRippleSlot(SlotIndex);
	}
	bIsRippleUpdating = false;

	if (ProgressDuration <= 0.0f)
	{
		Progress = 1.0f;
		ActivationMaterialInstance->SetScalarParameterValue(ProgressParameterName, Progress);
		bIsProgressUpdating = false;
	}
}

void UShooterReflectionBarrier::PlayHitRipple(const FVector& IncomingOrigin)
{
	if (!ActivationMaterialInstance || RippleDuration <= 0.0f)
	{
		return;
	}

	APlayerController* PlayerController = GetOwningPlayer();
	if (!PlayerController)
	{
		return;
	}

	FVector2D ScreenPosition = FVector2D::ZeroVector;
	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	PlayerController->GetViewportSize(ViewportWidth, ViewportHeight);

	// 카메라 뒤는 투영 자체가 실패한다.
	const bool bProjected = ViewportWidth > 0
		&& ViewportHeight > 0
		&& PlayerController->ProjectWorldLocationToScreen(
			IncomingOrigin,
			ScreenPosition,
			true);
	if (!bProjected)
	{
		return;
	}

	FVector2D CenterUV(
		ScreenPosition.X / static_cast<float>(ViewportWidth),
		ScreenPosition.Y / static_cast<float>(ViewportHeight));

	// 현재 FOV 절두체 밖(측면)은 가장자리에 뭉개지므로 재생하지 않는다.
	const float MinUV = -RippleScreenMargin;
	const float MaxUV = 1.0f + RippleScreenMargin;
	if (CenterUV.X < MinUV || CenterUV.X > MaxUV
		|| CenterUV.Y < MinUV || CenterUV.Y > MaxUV)
	{
		return;
	}

	CenterUV.X = FMath::Clamp(CenterUV.X, 0.025f, 0.975f);
	CenterUV.Y = FMath::Clamp(CenterUV.Y, 0.025f, 0.975f);

	const int32 SlotIndex = FindRippleSlotForNewHit();
	FShooterReflectionRippleSlot& SlotS = RippleSlots[SlotIndex];
	SlotS.CenterUV = CenterUV;
	SlotS.ElapsedSeconds = 0.0f;
	SlotS.bActive = true;
	bIsRippleUpdating = true;
	PushRippleSlot(SlotIndex);
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

void UShooterReflectionBarrier::UpdateRipples(float DeltaTime)
{
	if (!ActivationMaterialInstance || !bIsRippleUpdating)
	{
		return;
	}

	bool bAnyActive = false;
	for (int32 SlotIndex = 0; SlotIndex < MaxRippleSlots; ++SlotIndex)
	{
		FShooterReflectionRippleSlot& Slots = RippleSlots[SlotIndex];
		if (!Slots.bActive)
		{
			continue;
		}

		Slots.ElapsedSeconds += DeltaTime * RippleTimeScale;
		if (Slots.ElapsedSeconds >= RippleDuration)
		{
			Slots.bActive = false;
		}
		bAnyActive |= Slots.bActive;
		PushRippleSlot(SlotIndex);
	}
	bIsRippleUpdating = bAnyActive;
}

void UShooterReflectionBarrier::UpdateAspectRatio(const FGeometry& MyGeometry)
{
	if (!ActivationMaterialInstance)
	{
		return;
	}

	const FVector2D LocalSize = MyGeometry.GetLocalSize();
	if (LocalSize.X <= 0.0 || LocalSize.Y <= 0.0)
	{
		return;
	}

	// 머티리얼이 UV.x 에 곱해서 리플이 찌그러지지 않고 원으로 퍼지게 한다.
	const float AspectRatio = static_cast<float>(LocalSize.X / LocalSize.Y);
	if (FMath::IsNearlyEqual(AspectRatio, CachedAspectRatio))
	{
		return;
	}

	CachedAspectRatio = AspectRatio;
	ActivationMaterialInstance->SetScalarParameterValue(AspectRatioParameterName, AspectRatio);
}

void UShooterReflectionBarrier::PushRippleSlot(int32 SlotIndex)
{
	if (!ActivationMaterialInstance)
	{
		return;
	}

	// 진행도 1 = 빈 슬롯. 머티리얼은 w >= 1 인 슬롯을 건너뛴다.
	const FShooterReflectionRippleSlot& Slots = RippleSlots[SlotIndex];
	const float SlotProgress = Slots.bActive
		? FMath::Clamp(Slots.ElapsedSeconds / FMath::Max(RippleDuration, KINDA_SMALL_NUMBER), 0.0f, 1.0f)
		: 1.0f;
	ActivationMaterialInstance->SetVectorParameterValue(
		RippleSlotParameterNames[SlotIndex],
		FLinearColor(Slots.CenterUV.X, Slots.CenterUV.Y, Slots.ElapsedSeconds, SlotProgress));
}

int32 UShooterReflectionBarrier::FindRippleSlotForNewHit() const
{
	// 빈 슬롯이 없으면 가장 오래된 리플을 덮어쓴다.
	int32 OldestSlotIndex = 0;
	for (int32 SlotIndex = 0; SlotIndex < MaxRippleSlots; ++SlotIndex)
	{
		const FShooterReflectionRippleSlot& Slots = RippleSlots[SlotIndex];
		if (!Slots.bActive)
		{
			return SlotIndex;
		}
		if (Slots.ElapsedSeconds > RippleSlots[OldestSlotIndex].ElapsedSeconds)
		{
			OldestSlotIndex = SlotIndex;
		}
	}
	return OldestSlotIndex;
}
