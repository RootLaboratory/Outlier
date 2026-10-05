#include "UI/TeleportSpriteAnimation.h"

#include "Components/Image.h"
#include "Materials/MaterialInstanceDynamic.h"

void UTeleportSpriteAnimation::SetSpriteTexture(UTexture2D* InSpriteTexture)
{
	// A null override intentionally leaves the material's default Sprite value intact.
	if (InSpriteTexture && AnimationMaterialInstance)
	{
		AnimationMaterialInstance->SetTextureParameterValue(SpriteParameterName, InSpriteTexture);
	}
}

void UTeleportSpriteAnimation::NativeConstruct()
{
	Super::NativeConstruct();

	if (TeleportTexture && AnimationMaterial)
	{
		// The animation material owns the Sprite parameter and its default texture.
		// The WBP Image is only the material host; no source texture is required on
		// its brush.
		TeleportTexture->SetBrushFromMaterial(AnimationMaterial);
		AnimationMaterialInstance = TeleportTexture->GetDynamicMaterial();

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[TeleportSprite] Construct Widget=%s Image=%s Material=%s MID=%s"),
		*GetNameSafe(this),
		*GetNameSafe(TeleportTexture),
		*GetNameSafe(AnimationMaterial),
		*GetNameSafe(AnimationMaterialInstance));
	}
	else
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("[TeleportSprite] Construct missing binding/config Widget=%s Image=%s Material=%s"),
			*GetNameSafe(this),
			*GetNameSafe(TeleportTexture),
			*GetNameSafe(AnimationMaterial));
	}
	StopAnimation();
}

void UTeleportSpriteAnimation::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bIsPlaying || !AnimationMaterialInstance)
	{
		return;
	}

	AnimationPhase += InDeltaTime * EffectiveTimeMultiplier;
	AnimationMaterialInstance->SetScalarParameterValue(TimeParameterName, AnimationPhase);
	if (!bHasLoggedPlaybackTick)
	{
		bHasLoggedPlaybackTick = true;
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[TeleportSprite] First update Widget=%s Time=%.3f EffectiveMultiplier=%.3f"),
			*GetNameSafe(this),
			AnimationPhase,
			EffectiveTimeMultiplier);
	}
}

void UTeleportSpriteAnimation::StartTeleportAnimation(float InLayerTimeMultiplier)
{
	AnimationPhase = 0.0f;
	bHasLoggedPlaybackTick = false;
	EffectiveTimeMultiplier = FMath::Max(InLayerTimeMultiplier, 0.0f);
	bIsPlaying = EffectiveTimeMultiplier > 0.0f;

	if (AnimationMaterialInstance)
	{
		AnimationMaterialInstance->SetScalarParameterValue(TimeParameterName, AnimationPhase);
		AnimationMaterialInstance->SetScalarParameterValue(SpeedParameterName, EffectiveTimeMultiplier);
	}

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[TeleportSprite] Start Widget=%s LayerMultiplier=%.3f EffectiveMultiplier=%.3f MID=%s"),
		*GetNameSafe(this),
		InLayerTimeMultiplier,
		EffectiveTimeMultiplier,
		*GetNameSafe(AnimationMaterialInstance));
}

void UTeleportSpriteAnimation::StopAnimation()
{
	AnimationPhase = 0.0f;
	bIsPlaying = false;

	if (AnimationMaterialInstance)
	{
		AnimationMaterialInstance->SetScalarParameterValue(TimeParameterName, AnimationPhase);
	}
}
