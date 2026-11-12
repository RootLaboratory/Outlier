#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Looping.h"
#include "Enemy/EnemyStat.h"
#include "OutlierStunCueNotify.generated.h"

class UMaterialInterface;
class UMeshComponent;
class UTexture;

/**
 * Shared presentation for EMP and weapon stuns.
 * Create a Blueprint child in /Game/GameplayCues and assign the desired Niagara
 * system in Looping Effects -> Looping Particles and full-mesh layers in Stun Overlay Materials.
 * The ASC owns their lifetime through State.Stunned.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Outlier Stun Looping Cue"))
class OUTLIER_API AOutlierStunCueNotify : public AGameplayCueNotify_Looping
{
	GENERATED_BODY()

public:
	AOutlierStunCueNotify();

protected:
	/** Third-person Character mesh only. Full-mesh translucent layers in array order (last on top). Null entries are skipped. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stun|Overlay")
	TArray<TObjectPtr<UMaterialInterface>> StunOverlayMaterials;

	/** Reuses RuntimeStat.Type. Gun/Melee/Mortar/Sniper use overlays; Turret uses Niagara only.
	 * Missing or null entries keep the material's default texture. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stun|Overlay")
	TMap<EEnemyType, TObjectPtr<UTexture>> StunOverlayTexturesByEnemyType;

	/** Texture parameter on every overlay material instance. None disables texture injection. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stun|Overlay")
	FName StunOverlayTextureParameterName = TEXT("StunTexture");

	virtual bool WhileActive_Implementation(AActor* Target, const FGameplayCueParameters& Parameters) override;
	virtual bool OnRemove_Implementation(AActor* Target, const FGameplayCueParameters& Parameters) override;
	virtual bool Recycle() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Keep the shared cue registered even when the Blueprint is named GC_Stun. */
	virtual void DeriveGameplayCueTagFromAssetName() override;

private:

	void ApplyOverlayMaterials(AActor* Target);
	void RestoreOverlayMaterials();

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMeshComponent>> OverlayLayers;
};
