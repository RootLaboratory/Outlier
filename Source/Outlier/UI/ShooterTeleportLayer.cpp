#include "UI/ShooterTeleportLayer.h"

#include "Components/PanelWidget.h"
#include "Components/CanvasPanel.h"
#include "UI/TeleportSpriteAnimation.h"

void UShooterTeleportLayer::NativePreConstruct()
{
	Super::NativePreConstruct();
	IndexSpriteAnimations();
	CacheBaseSpriteTransforms();
	ApplySpriteTransforms();
}

void UShooterTeleportLayer::NativeConstruct()
{
	Super::NativeConstruct();

	CachedSpriteAnimations.Reset();
	CacheSpriteAnimations(AnimationCanvas);
	IndexSpriteAnimations();
	CacheBaseSpriteTransforms();
	ApplySpriteTransforms();
	ApplySpriteTextures();

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[TeleportLayer] Construct Layer=%s Canvas=%s Children=%d CachedAnimations=%d"),
		*GetNameSafe(this),
		*GetNameSafe(AnimationCanvas),
		AnimationCanvas ? AnimationCanvas->GetChildrenCount() : 0,
		IndexedSpriteAnimations.Num());
}

void UShooterTeleportLayer::CacheBaseSpriteTransforms()
{
	if (bHasCachedBaseSpriteTransforms)
	{
		return;
	}

	CachedBaseSpriteTransforms.Reset();
	bool bHasAnySpriteAnimation = false;

	for (UTeleportSpriteAnimation* SpriteAnimation : IndexedSpriteAnimations)
	{
		bHasAnySpriteAnimation |= SpriteAnimation != nullptr;
		CachedBaseSpriteTransforms.Add(
			SpriteAnimation ? SpriteAnimation->GetRenderTransform() : FWidgetTransform());
	}

	bHasCachedBaseSpriteTransforms = bHasAnySpriteAnimation;
}

void UShooterTeleportLayer::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	ApplySpriteTransforms();
}

void UShooterTeleportLayer::IndexSpriteAnimations()
{
	IndexedSpriteAnimations.Reset();
	IndexedSpriteAnimations.Add(TeleportSpriteAnimation1);
	IndexedSpriteAnimations.Add(TeleportSpriteAnimation2);
	IndexedSpriteAnimations.Add(TeleportSpriteAnimation3);
	IndexedSpriteAnimations.Add(TeleportSpriteAnimation4);
	IndexedSpriteAnimations.Add(TeleportSpriteAnimation5);
	IndexedSpriteAnimations.Add(TeleportSpriteAnimation6);
	IndexedSpriteAnimations.Add(TeleportSpriteAnimation7);
	IndexedSpriteAnimations.Add(TeleportSpriteAnimation8);
	IndexedSpriteAnimations.Add(TeleportSpriteAnimation9);
	IndexedSpriteAnimations.Add(TeleportSpriteAnimation10);

	for (int32 Index = 0; Index < IndexedSpriteAnimations.Num(); ++Index)
	{
		if (IndexedSpriteAnimations[Index])
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[TeleportLayer] Indexed SpriteAnimation Index=%d Widget=%s"),
				Index + 1,
				*GetNameSafe(IndexedSpriteAnimations[Index]));
		}
	}
}

void UShooterTeleportLayer::ApplySpriteTransforms()
{
	ConfigureSpriteAnimation(TeleportSpriteAnimation1, SpriteAnimation1.Transform, 0);
	ConfigureSpriteAnimation(TeleportSpriteAnimation2, SpriteAnimation2.Transform, 1);
	ConfigureSpriteAnimation(TeleportSpriteAnimation3, SpriteAnimation3.Transform, 2);
	ConfigureSpriteAnimation(TeleportSpriteAnimation4, SpriteAnimation4.Transform, 3);
	ConfigureSpriteAnimation(TeleportSpriteAnimation5, SpriteAnimation5.Transform, 4);
	ConfigureSpriteAnimation(TeleportSpriteAnimation6, SpriteAnimation6.Transform, 5);
	ConfigureSpriteAnimation(TeleportSpriteAnimation7, SpriteAnimation7.Transform, 6);
	ConfigureSpriteAnimation(TeleportSpriteAnimation8, SpriteAnimation8.Transform, 7);
	ConfigureSpriteAnimation(TeleportSpriteAnimation9, SpriteAnimation9.Transform, 8);
	ConfigureSpriteAnimation(TeleportSpriteAnimation10, SpriteAnimation10.Transform, 9);
}

void UShooterTeleportLayer::ApplySpriteTextures()
{
	if (TeleportSpriteAnimation1) TeleportSpriteAnimation1->SetSpriteTexture(SpriteAnimation1.Texture);
	if (TeleportSpriteAnimation2) TeleportSpriteAnimation2->SetSpriteTexture(SpriteAnimation2.Texture);
	if (TeleportSpriteAnimation3) TeleportSpriteAnimation3->SetSpriteTexture(SpriteAnimation3.Texture);
	if (TeleportSpriteAnimation4) TeleportSpriteAnimation4->SetSpriteTexture(SpriteAnimation4.Texture);
	if (TeleportSpriteAnimation5) TeleportSpriteAnimation5->SetSpriteTexture(SpriteAnimation5.Texture);
	if (TeleportSpriteAnimation6) TeleportSpriteAnimation6->SetSpriteTexture(SpriteAnimation6.Texture);
	if (TeleportSpriteAnimation7) TeleportSpriteAnimation7->SetSpriteTexture(SpriteAnimation7.Texture);
	if (TeleportSpriteAnimation8) TeleportSpriteAnimation8->SetSpriteTexture(SpriteAnimation8.Texture);
	if (TeleportSpriteAnimation9) TeleportSpriteAnimation9->SetSpriteTexture(SpriteAnimation9.Texture);
	if (TeleportSpriteAnimation10) TeleportSpriteAnimation10->SetSpriteTexture(SpriteAnimation10.Texture);
}

void UShooterTeleportLayer::ConfigureSpriteAnimation(
	UTeleportSpriteAnimation* SpriteAnimation,
	const FWidgetTransform& LayerTransform,
	int32 SpriteIndex) const
{
	if (!SpriteAnimation || !CachedBaseSpriteTransforms.IsValidIndex(SpriteIndex))
	{
		return;
	}

	const FWidgetTransform& BaseTransform = CachedBaseSpriteTransforms[SpriteIndex];
	FWidgetTransform FinalTransform = BaseTransform;

	// Compose the WBP transform first, then apply the Layer transform.
	FinalTransform.Translation = BaseTransform.Translation + LayerTransform.Translation;
	FinalTransform.Scale = BaseTransform.Scale * LayerTransform.Scale;
	FinalTransform.Shear = BaseTransform.Shear + LayerTransform.Shear;
	FinalTransform.Angle = BaseTransform.Angle + LayerTransform.Angle;

	SpriteAnimation->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	SpriteAnimation->SetRenderTransform(FinalTransform);
}

void UShooterTeleportLayer::StartTeleportAnimation()
{
	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[TeleportLayer] Start Layer=%s LayerMultiplier=%.3f CachedAnimations=%d"),
		*GetNameSafe(this),
		LayerTimeMultiplier,
		CachedSpriteAnimations.Num());

	StartSpriteAnimation(TeleportSpriteAnimation1, SpriteAnimation1.PlaybackSpeedMultiplier);
	StartSpriteAnimation(TeleportSpriteAnimation2, SpriteAnimation2.PlaybackSpeedMultiplier);
	StartSpriteAnimation(TeleportSpriteAnimation3, SpriteAnimation3.PlaybackSpeedMultiplier);
	StartSpriteAnimation(TeleportSpriteAnimation4, SpriteAnimation4.PlaybackSpeedMultiplier);
	StartSpriteAnimation(TeleportSpriteAnimation5, SpriteAnimation5.PlaybackSpeedMultiplier);
	StartSpriteAnimation(TeleportSpriteAnimation6, SpriteAnimation6.PlaybackSpeedMultiplier);
	StartSpriteAnimation(TeleportSpriteAnimation7, SpriteAnimation7.PlaybackSpeedMultiplier);
	StartSpriteAnimation(TeleportSpriteAnimation8, SpriteAnimation8.PlaybackSpeedMultiplier);
	StartSpriteAnimation(TeleportSpriteAnimation9, SpriteAnimation9.PlaybackSpeedMultiplier);
	StartSpriteAnimation(TeleportSpriteAnimation10, SpriteAnimation10.PlaybackSpeedMultiplier);
}

void UShooterTeleportLayer::StartSpriteAnimation(
	UTeleportSpriteAnimation* SpriteAnimation,
	float SpeedMultiplier) const
{
	if (SpriteAnimation)
	{
		SpriteAnimation->StartTeleportAnimation(
			LayerTimeMultiplier * FMath::Max(SpeedMultiplier, 0.0f));
	}
}

void UShooterTeleportLayer::CacheSpriteAnimations(UWidget* Widget)
{
	if (!Widget)
	{
		return;
	}

	if (UTeleportSpriteAnimation* SpriteAnimation = Cast<UTeleportSpriteAnimation>(Widget))
	{
		CachedSpriteAnimations.Add(SpriteAnimation);
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[TeleportLayer] Cached SpriteAnimation Layer=%s Widget=%s"),
			*GetNameSafe(this),
			*GetNameSafe(SpriteAnimation));
		return;
	}

	if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
	{
		for (int32 ChildIndex = 0; ChildIndex < Panel->GetChildrenCount(); ++ChildIndex)
		{
			CacheSpriteAnimations(Panel->GetChildAt(ChildIndex));
		}
	}
}
