#include "GAS/Cues/OutlierStunCueNotify.h"

#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "Enemy/EnemyBase.h"
#include "Engine/Texture.h"
#include "MaterialShared.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

DEFINE_LOG_CATEGORY_STATIC(LogOutlierStunOverlay, Log, All);

AOutlierStunCueNotify::AOutlierStunCueNotify()
{
	DeriveGameplayCueTagFromAssetName();

	// Different stun sources share one visual instance on the affected actor.
	bUniqueInstancePerInstigator = false;
	bUniqueInstancePerSourceObject = false;
	bAllowMultipleOnActiveEvents = false;
	bAllowMultipleWhileActiveEvents = false;
	bAutoAttachToOwner = true;
	bAutoDestroyOnRemove = true;
	DefaultPlacementInfo.AttachPolicy = EGameplayCueNotify_AttachPolicy::AttachToTarget;
}

void AOutlierStunCueNotify::DeriveGameplayCueTagFromAssetName()
{
	// UE's asset-name fallback restores an inherited tag without restoring its
	// registry name. This cue always represents the shared Stun GE, regardless
	// of the Blueprint's name, so keep both values synchronized.
	GameplayCueTag = OutlierGameplayTags::Cue::Status::Stun();
	GameplayCueName = GameplayCueTag.GetTagName();
}

bool AOutlierStunCueNotify::WhileActive_Implementation(AActor* Target, const FGameplayCueParameters& Parameters)
{
	const bool bResult = Super::WhileActive_Implementation(Target, Parameters);
	if (!bLoopingEffectsRemoved && GetNetMode() != NM_DedicatedServer)
	{
		ApplyOverlayMaterials(Target);
	}
	return bResult;
}

bool AOutlierStunCueNotify::OnRemove_Implementation(AActor* Target, const FGameplayCueParameters& Parameters)
{
	RestoreOverlayMaterials();
	return Super::OnRemove_Implementation(Target, Parameters);
}

bool AOutlierStunCueNotify::Recycle()
{
	RestoreOverlayMaterials();
	return Super::Recycle();
}

void AOutlierStunCueNotify::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RestoreOverlayMaterials();
	Super::EndPlay(EndPlayReason);
}

void AOutlierStunCueNotify::ApplyOverlayMaterials(AActor* Target)
{
	if (!IsValid(Target) || StunOverlayMaterials.IsEmpty() || !OverlayLayers.IsEmpty()) return;
	const AEnemyBase* Enemy = Cast<AEnemyBase>(Target);
	if (!Enemy) return;
	const EEnemyType EnemyType = Enemy->GetRuntimeStat().Type;
	switch (EnemyType)
	{
	case EEnemyType::Gun:
	case EEnemyType::Melee:
	case EEnemyType::Mortar:
	case EEnemyType::Sniper:
		break;
	default:
		// Niagara remains owned by Super::WhileActive, independently of overlays.
		return;
	}
	const TObjectPtr<UTexture>* ConfiguredTexture = StunOverlayTexturesByEnemyType.Find(EnemyType);
	UTexture* TypeTexture = ConfiguredTexture ? ConfiguredTexture->Get() : nullptr;

	// CharacterMesh0 is the world/third-person body. Never collect view-model,
	// weapon or other mesh components from the actor.
	const ACharacter* Character = Cast<ACharacter>(Target);
	if (!Character || !IsValid(Character->GetMesh())) return;
	TArray<UMeshComponent*, TInlineAllocator<1>> Meshes;
	Meshes.Add(Character->GetMesh());
	for (UMeshComponent* Mesh : Meshes)
	{
		if (!IsValid(Mesh) || Mesh->GetNumMaterials() == 0) continue;
		USkeletalMeshComponent* SkeletalSource = Cast<USkeletalMeshComponent>(Mesh);
		UStaticMeshComponent* StaticSource = Cast<UStaticMeshComponent>(Mesh);
		if (!SkeletalSource && !StaticSource) continue;

		for (int32 LayerIndex = 0; LayerIndex < StunOverlayMaterials.Num(); ++LayerIndex)
		{
			UMaterialInterface* Material = StunOverlayMaterials[LayerIndex];
			if (!Material) continue;
			// Checking usage via CheckMaterialUsage would allow PIE to auto-recompile
			// during scene-proxy creation, which caused the original rendering crash.
			const UMaterial* BaseMaterial = Material->GetMaterial();
			if (SkeletalSource && (!BaseMaterial || (!BaseMaterial->bUsedAsSpecialEngineMaterial
				&& !BaseMaterial->GetUsageByFlag(MATUSAGE_SkeletalMesh))))
			{
				UE_LOG(LogOutlierStunOverlay, Warning,
					TEXT("Skipping stun overlay %s: enable Used with Skeletal Mesh on its parent material, compile and save before playing."),
					*Material->GetPathName());
				continue;
			}
			if (!IsTranslucentBlendMode(Material->GetBlendMode()))
			{
				UE_LOG(LogOutlierStunOverlay, Warning,
					TEXT("Skipping stun overlay %s: ordered full-mesh layers require a translucent blend mode."),
					*Material->GetPathName());
				continue;
			}
			// One private instance per layer/cue prevents simultaneous enemies from
			// overwriting each other's type-specific texture or the BP's source asset.
			UMaterialInstanceDynamic* LayerMaterial = UMaterialInstanceDynamic::Create(Material, this);
			if (TypeTexture && !StunOverlayTextureParameterName.IsNone())
			{
				LayerMaterial->SetTextureParameterValue(StunOverlayTextureParameterName, TypeTexture);
			}

			UMeshComponent* Layer = nullptr;
			if (SkeletalSource)
			{
				USkeletalMeshComponent* SkeletalLayer = NewObject<USkeletalMeshComponent>(this, NAME_None, RF_Transient);
				SkeletalLayer->SetSkeletalMeshAsset(SkeletalSource->GetSkeletalMeshAsset());
				SkeletalLayer->SetLeaderPoseComponent(SkeletalSource);
				Layer = SkeletalLayer;
			}
			else
			{
				UStaticMeshComponent* StaticLayer = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
				StaticLayer->SetStaticMesh(StaticSource->GetStaticMesh());
				StaticLayer->SetForceDisableNanite(true);
				Layer = StaticLayer;
			}
			Layer->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Layer->SetGenerateOverlapEvents(false);
			Layer->SetCanEverAffectNavigation(false);
			Layer->SetCastShadow(false);
			Layer->SetRenderInDepthPass(false);
			Layer->SetRenderCustomDepth(false);
			Layer->SetVisibleInRayTracing(false);
			Layer->SetReceivesDecals(false);
			Layer->bUseAttachParentBound = true;
			Layer->SetVisibility(Mesh->IsVisible());
			Layer->SetHiddenInGame(Mesh->bHiddenInGame);
			Layer->SetOwnerNoSee(Mesh->bOwnerNoSee);
			Layer->SetOnlyOwnerSee(Mesh->bOnlyOwnerSee);
			Layer->SetComponentTickEnabled(false);
			Layer->SetTranslucentSortPriority(static_cast<int32>(FMath::Clamp<int64>(
				static_cast<int64>(Mesh->TranslucencySortPriority) + LayerIndex + 1, MIN_int32, MAX_int32)));
			// Every entry covers every section; array indices are never material-slot indices.
			for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
			{
				Layer->SetMaterial(Slot, LayerMaterial);
			}
			Layer->AttachToComponent(Mesh, FAttachmentTransformRules::SnapToTargetIncludingScale);
			OverlayLayers.Add(Layer);
			Layer->RegisterComponent();
		}
	}
}

void AOutlierStunCueNotify::RestoreOverlayMaterials()
{
	for (UMeshComponent* Layer : OverlayLayers)
	{
		if (IsValid(Layer)) Layer->DestroyComponent();
	}
	OverlayLayers.Reset();
}
