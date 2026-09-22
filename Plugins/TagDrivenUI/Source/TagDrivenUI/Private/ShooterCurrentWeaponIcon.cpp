// Fill out your copyright notice in the Description page of Project Settings.


#include "ShooterCurrentWeaponIcon.h"

#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "TagDrivenUIGameplayTags.h"

namespace
{
	// 세 무기는 원을 3등분한 120도 간격으로 배치한다.
	constexpr int32 WeaponCarouselItemCount = 3;
	constexpr float WeaponCarouselStepDegrees = 360.0f / static_cast<float>(WeaponCarouselItemCount);
}

void UShooterCurrentWeaponIcon::NativePreConstruct()
{
	Super::NativePreConstruct();

	RefreshWeaponTextures();

	// 디자이너에서도 실제 실행 결과에 가까운 배치를 확인할 수 있게 한다.
	if (IsDesignTime() && HasCarouselWidgets())
	{
		const int32 PreviewIndex = GetCarouselIndex(PreviewWeaponType);
		if (PreviewIndex != INDEX_NONE)
		{
			CurrentWeaponType = PreviewWeaponType;
			CurrentRotationDegrees = FocusAngleDegrees - PreviewIndex * WeaponCarouselStepDegrees;
			RefreshCarouselVisuals();
		}
	}
}

void UShooterCurrentWeaponIcon::NativeConstruct()
{
	Super::NativeConstruct();

	ModuleTag = TagDrivenUITags::Shooter::CurrentWeapon();

	RefreshWeaponTextures();
	SetCurrentWeapon(CurrentWeaponType);
}

void UShooterCurrentWeaponIcon::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bIsRotating)
	{
		return;
	}

	// 시작 각도에서 목표 각도까지 Ease In/Out으로 보간한다.
	RotationElapsed += InDeltaTime;
	const float SafeDuration = FMath::Max(RotationDuration, UE_KINDA_SMALL_NUMBER);
	const float LinearAlpha = FMath::Clamp(RotationElapsed / SafeDuration, 0.0f, 1.0f);
	const float EasedAlpha = FMath::InterpEaseInOut(0.0f, 1.0f, LinearAlpha, 2.0f);
	CurrentRotationDegrees = FMath::Lerp(StartRotationDegrees, TargetRotationDegrees, EasedAlpha);
	RefreshCarouselVisuals();

	if (LinearAlpha >= 1.0f)
	{
		CurrentRotationDegrees = FMath::UnwindDegrees(TargetRotationDegrees);
		TargetRotationDegrees = CurrentRotationDegrees;
		bIsRotating = false;
		RefreshCarouselVisuals();
	}
}

void UShooterCurrentWeaponIcon::SetCurrentWeapon(EWidgetWeaponType WeaponType)
{
	UE_LOG(LogTemp, Warning,
		TEXT("[ShooterHUD][WeaponWidget] SetCurrentWeapon Widget=%s Type=%s Rifle=%s Pistol=%s Melee=%s"),
		*GetNameSafe(this),
		*StaticEnum<EWidgetWeaponType>()->GetNameStringByValue(static_cast<int64>(WeaponType)),
		*GetNameSafe(RifleWeaponImage),
		*GetNameSafe(PistolWeaponImage),
		*GetNameSafe(MeleeWeaponImage));

	CurrentWeaponType = WeaponType;

	// 새 이미지 3개가 모두 준비되기 전에는 기존 BP 동작을 그대로 사용한다.
	if (!HasCarouselWidgets())
	{
		SetLegacyWeaponImage(WeaponType);
		return;
	}

	if (CurrentWeaponImage)
	{
		CurrentWeaponImage->SetVisibility(ESlateVisibility::Collapsed);
	}

	const int32 TargetIndex = GetCarouselIndex(WeaponType);
	if (TargetIndex == INDEX_NONE)
	{
		bIsRotating = false;
		RefreshCarouselVisuals();
		return;
	}

	const float DesiredRotation = FocusAngleDegrees - TargetIndex * WeaponCarouselStepDegrees;

	// 최초 표시 때는 애니메이션 없이 선택 무기를 기준 위치에 바로 놓는다.
	if (!bCarouselInitialized)
	{
		CurrentRotationDegrees = FMath::UnwindDegrees(DesiredRotation);
		TargetRotationDegrees = CurrentRotationDegrees;
		bCarouselInitialized = true;
		bIsRotating = false;
		RefreshCarouselVisuals();
		return;
	}

	StartRotationDegrees = CurrentRotationDegrees;

	// 현재 각도와 같은 표현을 갖는 목표 각도 중 가장 가까운 값을 선택한다.
	// 예: Rifle(0) -> Melee(2)는 +240도가 아니라 -120도로 이동한다.
	TargetRotationDegrees = StartRotationDegrees
		+ FMath::FindDeltaAngleDegrees(StartRotationDegrees, DesiredRotation);
	RotationElapsed = 0.0f;
	bIsRotating = !FMath::IsNearlyEqual(StartRotationDegrees, TargetRotationDegrees);

	if (!bIsRotating)
	{
		RefreshCarouselVisuals();
	}
}

void UShooterCurrentWeaponIcon::RefreshWeaponTextures()
{
	if (RifleWeaponImage && RifleTexture)
	{
		RifleWeaponImage->SetBrushFromTexture(RifleTexture, false);
	}

	if (PistolWeaponImage && PistolTexture)
	{
		PistolWeaponImage->SetBrushFromTexture(PistolTexture, false);
	}

	if (MeleeWeaponImage && MeleeTexture)
	{
		MeleeWeaponImage->SetBrushFromTexture(MeleeTexture, false);
	}
}

void UShooterCurrentWeaponIcon::RefreshCarouselVisuals()
{
	// 배열 순서는 GetCarouselIndex()의 Rifle, Melee, Pistol 순서와 같아야 한다.
	struct FCarouselItem
	{
		UImage* Image;
		int32 Index;
		EWidgetWeaponType WeaponType;
	};

	// 화면상 시계 방향 순서: Rifle -> Melee -> Pistol.
	// Rifle이 선택된 경우 Pistol은 왼쪽 아래, Melee는 오른쪽 아래에 놓인다.
	const FCarouselItem Items[] =
	{
		{ RifleWeaponImage, 0, EWidgetWeaponType::Rifle },
		{ MeleeWeaponImage, 1, EWidgetWeaponType::Melee },
		{ PistolWeaponImage, 2, EWidgetWeaponType::Pistol }
	};
	const int32 SelectedIndex = GetCarouselIndex(CurrentWeaponType);

	for (const FCarouselItem& Item : Items)
	{
		if (!Item.Image)
		{
			continue;
		}

		const float DisplayAngle = Item.Index * WeaponCarouselStepDegrees + CurrentRotationDegrees;
		const FWeaponCarouselLayoutSettings& LayoutSettings = GetLayoutSettings(Item.WeaponType);
		const FVector2D Position = GetCarouselPosition(LayoutSettings, DisplayAngle);

		const float DistanceFromFocus = FMath::Abs(
			FMath::FindDeltaAngleDegrees(DisplayAngle, FocusAngleDegrees));

		// 기준 위치에 가까워질수록 선택 크기와 선택 Tint로 자연스럽게 전환한다.
		const float LinearFocus = 1.0f
			- FMath::Clamp(DistanceFromFocus / WeaponCarouselStepDegrees, 0.0f, 1.0f);
		const float FocusWeight = SelectedIndex == INDEX_NONE
			? 0.0f
			: FMath::SmoothStep(0.0f, 1.0f, LinearFocus);
		const FVector2D RenderScale = FMath::Lerp(
			LayoutSettings.InactiveScale,
			LayoutSettings.ActiveScale,
			FocusWeight);
		const FLinearColor Tint = FMath::Lerp(InactiveTint, SelectedTint, FocusWeight);

		Item.Image->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		Item.Image->SetRenderTranslation(Position);
		Item.Image->SetRenderScale(RenderScale);
		Item.Image->SetColorAndOpacity(Tint);
		Item.Image->SetVisibility(ESlateVisibility::HitTestInvisible);

		// 선택 무기가 다른 작은 아이콘 뒤에 가려지지 않도록 앞으로 올린다.
		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Item.Image->Slot))
		{
			// BP에서 Stretch Anchor나 Size To Content가 설정되어 있어도
			// 세 아이콘은 같은 중심과 같은 크기에서 출발하도록 강제한다.
			CanvasSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
			CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			CanvasSlot->SetPosition(FVector2D::ZeroVector);
			CanvasSlot->SetSize(IconSize);
			CanvasSlot->SetAutoSize(false);

			CanvasSlot->SetZOrder(FMath::RoundToInt(FocusWeight * 100.0f));
		}
	}
}

void UShooterCurrentWeaponIcon::SetLegacyWeaponImage(EWidgetWeaponType WeaponType)
{
	if (!CurrentWeaponImage)
	{
		return;
	}

	if (UTexture2D* WeaponTexture = GetTextureForWeapon(WeaponType))
	{
		CurrentWeaponImage->SetBrushFromTexture(WeaponTexture, true);
		CurrentWeaponImage->SetVisibility(ESlateVisibility::Visible);
		return;
	}

	CurrentWeaponImage->SetVisibility(ESlateVisibility::Collapsed);
}

bool UShooterCurrentWeaponIcon::HasCarouselWidgets() const
{
	return RifleWeaponImage && PistolWeaponImage && MeleeWeaponImage;
}

int32 UShooterCurrentWeaponIcon::GetCarouselIndex(EWidgetWeaponType WeaponType)
{
	switch (WeaponType)
	{
	case EWidgetWeaponType::Rifle:
		return 0;
	case EWidgetWeaponType::Melee:
		return 1;
	case EWidgetWeaponType::Pistol:
		return 2;
	case EWidgetWeaponType::Unarmed:
	default:
		return INDEX_NONE;
	}
}

const FWeaponCarouselLayoutSettings& UShooterCurrentWeaponIcon::GetLayoutSettings(
	EWidgetWeaponType WeaponType) const
{
	switch (WeaponType)
	{
	case EWidgetWeaponType::Pistol:
		return PistolLayout;
	case EWidgetWeaponType::Melee:
		return MeleeLayout;
	case EWidgetWeaponType::Rifle:
	case EWidgetWeaponType::Unarmed:
	default:
		return RifleLayout;
	}
}

FVector2D UShooterCurrentWeaponIcon::GetCarouselPosition(
	const FWeaponCarouselLayoutSettings& LayoutSettings,
	float DisplayAngle) const
{
	// 각 무기가 자신의 세 지정 위치를 순환하도록 각도를 0~3 구간으로 변환한다.
	float Phase = FMath::Fmod(
		(DisplayAngle - FocusAngleDegrees) / WeaponCarouselStepDegrees,
		static_cast<float>(WeaponCarouselItemCount));
	if (Phase < 0.0f)
	{
		Phase += static_cast<float>(WeaponCarouselItemCount);
	}

	// 회전 순서에 맞춰 Active -> Right -> Left -> Active로 이어지는 닫힌 곡선을 만든다.
	const FVector2D Points[WeaponCarouselItemCount] =
	{
		LayoutSettings.ActivePosition,
		LayoutSettings.InactiveRightPosition,
		LayoutSettings.InactiveLeftPosition
	};

	const int32 StartIndex = FMath::FloorToInt(Phase) % WeaponCarouselItemCount;
	const int32 EndIndex = (StartIndex + 1) % WeaponCarouselItemCount;
	const int32 PreviousIndex = (StartIndex + WeaponCarouselItemCount - 1) % WeaponCarouselItemCount;
	const int32 NextIndex = (EndIndex + 1) % WeaponCarouselItemCount;
	const float SegmentAlpha = Phase - static_cast<float>(StartIndex);

	const FVector2D StartTangent =
		(Points[EndIndex] - Points[PreviousIndex]) * PositionCurveTension;
	const FVector2D EndTangent =
		(Points[NextIndex] - Points[StartIndex]) * PositionCurveTension;

	return FMath::CubicInterp(
		Points[StartIndex],
		StartTangent,
		Points[EndIndex],
		EndTangent,
		SegmentAlpha);
}

UTexture2D* UShooterCurrentWeaponIcon::GetTextureForWeapon(EWidgetWeaponType WeaponType) const
{
	switch (WeaponType)
	{
	case EWidgetWeaponType::Pistol:
		return PistolTexture;
	case EWidgetWeaponType::Rifle:
		return RifleTexture;
	case EWidgetWeaponType::Melee:
		return MeleeTexture;
	case EWidgetWeaponType::Unarmed:
	default:
		return UnarmedTexture;
	}
}
