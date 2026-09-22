#include "UI/ShooterTeleportLayer.h"

#include "Components/PanelWidget.h"
#include "Components/CanvasPanel.h"
#include "UI/TeleportSpriteAnimation.h"

void UShooterTeleportLayer::NativeConstruct()
{
	Super::NativeConstruct();

	CachedSpriteAnimations.Reset();
	CacheSpriteAnimations(AnimationCanvas);

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[TeleportLayer] Construct Layer=%s Canvas=%s Children=%d CachedAnimations=%d"),
		*GetNameSafe(this),
		*GetNameSafe(AnimationCanvas),
		AnimationCanvas ? AnimationCanvas->GetChildrenCount() : 0,
		CachedSpriteAnimations.Num());
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

	for (UTeleportSpriteAnimation* SpriteAnimation : CachedSpriteAnimations)
	{
		if (SpriteAnimation)
		{
			SpriteAnimation->StartTeleportAnimation(LayerTimeMultiplier);
		}
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
