#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "GAS/Cues/OutlierStunCueNotify.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Enemy/EnemyBase.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierStunOverlayUsageTest,
	"Outlier.GAS.StunOverlay.OrderedFullMeshLayers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierStunOverlayUsageTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Test world"), World)) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AEnemyBase* Target = World->SpawnActor<AEnemyBase>();
	AOutlierStunCueNotify* Cue = World->SpawnActor<AOutlierStunCueNotify>();
	if (!TestNotNull(TEXT("Target"), Target) || !TestNotNull(TEXT("Cue"), Cue)) return false;

	USkeletalMeshComponent* SkeletalMesh = Target->GetMesh();
	USkeletalMesh* Asset = NewObject<USkeletalMesh>();
	Asset->GetMaterials().Add(FSkeletalMaterial());
	SkeletalMesh->SetSkeletalMeshAsset(Asset);
	UMaterial* Parent = NewObject<UMaterial>();
	Parent->BlendMode = BLEND_Translucent;
	Parent->bUsedWithSkeletalMesh = false;
	UMaterial* First = Parent;
	Cue->StunOverlayMaterials.Add(First);
	AddExpectedError(TEXT("Skipping stun overlay"), EAutomationExpectedErrorFlags::Contains, 1);
	Cue->ApplyOverlayMaterials(Target);
	TestTrue(TEXT("Unprepared skeletal material creates no layer"), Cue->OverlayLayers.IsEmpty());
	TestFalse(TEXT("Stun never auto-enables usage flags"), Parent->GetUsageByFlag(MATUSAGE_SkeletalMesh));
	Parent->bUsedWithSkeletalMesh = true;
	UMaterialExpressionTextureSampleParameter2D* TextureParameter = NewObject<UMaterialExpressionTextureSampleParameter2D>(Parent);
	TextureParameter->ParameterName = Cue->StunOverlayTextureParameterName;
	UTexture2D* DefaultTexture = NewObject<UTexture2D>();
	TextureParameter->Texture = DefaultTexture;
	Parent->GetExpressionCollection().AddExpression(TextureParameter);
	Parent->GetEditorOnlyData()->EmissiveColor.Connect(0, TextureParameter);
	Parent->UpdateCachedExpressionData();
	UTexture2D* GunTexture = NewObject<UTexture2D>();
	UTexture2D* MeleeTexture = NewObject<UTexture2D>();
	Cue->StunOverlayTexturesByEnemyType.Add(EEnemyType::Gun, GunTexture);
	Cue->StunOverlayTexturesByEnemyType.Add(EEnemyType::Melee, MeleeTexture);

	USkeletalMeshComponent* Mesh = SkeletalMesh;
	Mesh->SetOwnerNoSee(true);
	USkeletalMeshComponent* FirstPersonMesh = NewObject<USkeletalMeshComponent>(Target);
	Target->AddInstanceComponent(FirstPersonMesh);
	FirstPersonMesh->SetSkeletalMeshAsset(Asset);
	FirstPersonMesh->SetOnlyOwnerSee(true);
	UMaterial* Original = NewObject<UMaterial>();
	Mesh->SetOverlayMaterial(Original);
	Mesh->MaterialSlotsOverlayMaterial.Add(Original);
	UMaterial* Second = NewObject<UMaterial>();
	Second->BlendMode = BLEND_Translucent;
	Second->bUsedWithSkeletalMesh = true;
	Cue->StunOverlayMaterials.Add(nullptr);
	Cue->StunOverlayMaterials.Add(Second);
	Cue->ApplyOverlayMaterials(Target);
	if (!TestEqual(TEXT("Two full-mesh layers even though source only has one slot"), Cue->OverlayLayers.Num(), 2)) return false;
	UMeshComponent* Bottom = Cue->OverlayLayers[0];
	UMeshComponent* Top = Cue->OverlayLayers[1];
	TestEqual(TEXT("First layer covers third-person geometry"), CastChecked<USkeletalMeshComponent>(Bottom)->GetSkeletalMeshAsset(), Asset);
	TestEqual(TEXT("Layer follows third-person pose"), CastChecked<USkeletalMeshComponent>(Bottom)->LeaderPoseComponent.Get(), static_cast<USkinnedMeshComponent*>(Mesh));
	TestTrue(TEXT("Layer preserves third-person owner visibility"), Bottom->bOwnerNoSee && !Bottom->bOnlyOwnerSee);
	TestEqual(TEXT("First entry is bottom layer"), CastChecked<UMaterialInstanceDynamic>(Bottom->GetMaterial(0))->Parent.Get(), static_cast<UMaterialInterface*>(First));
	TestEqual(TEXT("Last entry is top layer"), CastChecked<UMaterialInstanceDynamic>(Top->GetMaterial(0))->Parent.Get(), static_cast<UMaterialInterface*>(Second));
	UMaterialInstanceDynamic* GunMaterial = CastChecked<UMaterialInstanceDynamic>(Bottom->GetMaterial(0));
	UTexture* InjectedTexture = nullptr;
	GunMaterial->GetTextureParameterValue(FHashedMaterialParameterInfo(Cue->StunOverlayTextureParameterName), InjectedTexture);
	TestEqual(TEXT("Gun texture is injected into a private layer MID"), InjectedTexture, static_cast<UTexture*>(GunTexture));
	TestTrue(TEXT("Each layer has its own material instance"), Bottom->GetMaterial(0) != Top->GetMaterial(0));
	TestTrue(TEXT("Last layer draws later"), Top->TranslucencySortPriority > Bottom->TranslucencySortPriority);
	TestEqual(TEXT("Layer follows source transform"), Bottom->GetAttachParent(), static_cast<USceneComponent*>(Mesh));
	TestEqual(TEXT("Original global overlay is untouched"), Mesh->GetOverlayMaterial(), static_cast<UMaterialInterface*>(Original));
	TestEqual(TEXT("Original per-slot overlay is untouched"), Mesh->MaterialSlotsOverlayMaterial[0].Get(), static_cast<UMaterialInterface*>(Original));
	Cue->ApplyOverlayMaterials(Target);
	TestEqual(TEXT("Repeated start does not duplicate layers"), Cue->OverlayLayers.Num(), 2);
	Cue->RestoreOverlayMaterials();
	TestTrue(TEXT("Layer components are destroyed"), Bottom->IsBeingDestroyed() && Top->IsBeingDestroyed());
	TestTrue(TEXT("Cleanup clears layer references"), Cue->OverlayLayers.IsEmpty());

	Cue->StunOverlayMaterials.Swap(0, 2);
	Cue->ApplyOverlayMaterials(Target);
	if (TestEqual(TEXT("Reversed array still has two layers"), Cue->OverlayLayers.Num(), 2))
	{
		TestEqual(TEXT("Reordering moves second material to bottom"), CastChecked<UMaterialInstanceDynamic>(Cue->OverlayLayers[0]->GetMaterial(0))->Parent.Get(), static_cast<UMaterialInterface*>(Second));
		TestEqual(TEXT("Reordering moves first material to top"), CastChecked<UMaterialInstanceDynamic>(Cue->OverlayLayers[1]->GetMaterial(0))->Parent.Get(), static_cast<UMaterialInterface*>(First));
	}
	Cue->Recycle();
	const_cast<FEnemyStat&>(Target->GetRuntimeStat()).Type = EEnemyType::Melee;
	Cue->ApplyOverlayMaterials(Target);
	if (TestEqual(TEXT("Melee drone receives both layers"), Cue->OverlayLayers.Num(), 2))
	{
		UMaterialInstanceDynamic* MeleeMaterial = CastChecked<UMaterialInstanceDynamic>(Cue->OverlayLayers[1]->GetMaterial(0));
		MeleeMaterial->GetTextureParameterValue(FHashedMaterialParameterInfo(Cue->StunOverlayTextureParameterName), InjectedTexture);
		TestEqual(TEXT("Melee uses its configured texture"), InjectedTexture, static_cast<UTexture*>(MeleeTexture));
		GunMaterial->GetTextureParameterValue(FHashedMaterialParameterInfo(Cue->StunOverlayTextureParameterName), InjectedTexture);
		TestEqual(TEXT("Changing enemy type does not mutate another MID"), InjectedTexture, static_cast<UTexture*>(GunTexture));
	}
	Cue->RestoreOverlayMaterials();
	const_cast<FEnemyStat&>(Target->GetRuntimeStat()).Type = EEnemyType::Turret;
	Cue->WhileActive_Implementation(Target, FGameplayCueParameters());
	TestTrue(TEXT("Turret creates no material layers"), Cue->OverlayLayers.IsEmpty());
	TestFalse(TEXT("Turret still starts the standard looping VFX lifecycle"), Cue->bLoopingEffectsRemoved);
	Cue->OnRemove_Implementation(Target, FGameplayCueParameters());
	TestTrue(TEXT("Recycling cleans up layers"), Cue->OverlayLayers.IsEmpty());
	Cue->RestoreOverlayMaterials();
	return true;
}
#endif
