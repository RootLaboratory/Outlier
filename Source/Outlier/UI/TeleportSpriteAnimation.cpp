#include "UI/TeleportSpriteAnimation.h"

#include "Components/Image.h"
#include "Materials/MaterialInstanceDynamic.h"

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

	AnimationPhase = FMath::Fmod(
		AnimationPhase + InDeltaTime * EffectiveTimeMultiplier,
		1.0f);
	if (AnimationPhase < 0.0f)
	{
		AnimationPhase += 1.0f;
	}
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
	const float MinMultiplier = FMath::Max(TimeMultiplierMin, 0.0f);
	const float MaxMultiplier = FMath::Max(TimeMultiplierMax, MinMultiplier);
	const float RandomMultiplier = FMath::FRandRange(MinMultiplier, MaxMultiplier);
	EffectiveTimeMultiplier = FMath::Max(InLayerTimeMultiplier, 0.0f)
		* RandomMultiplier;
	bIsPlaying = EffectiveTimeMultiplier > 0.0f;

	if (AnimationMaterialInstance)
	{
		AnimationMaterialInstance->SetScalarParameterValue(TimeParameterName, AnimationPhase);
		AnimationMaterialInstance->SetScalarParameterValue(SpeedParameterName, EffectiveTimeMultiplier);
	}

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[TeleportSprite] Start Widget=%s LayerMultiplier=%.3f RandomMultiplier=%.3f EffectiveMultiplier=%.3f MID=%s"),
		*GetNameSafe(this),
		InLayerTimeMultiplier,
		RandomMultiplier,
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
