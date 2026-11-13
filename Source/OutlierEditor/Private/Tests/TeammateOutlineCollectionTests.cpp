#if WITH_DEV_AUTOMATION_TESTS

#include "OutlierEditor/Tests/MeleeWeaponTestActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "PostProcess/OutlierStencilSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierTeammateOutlineCollectionTest,
	"Outlier.Rendering.TeammateOutline.MeshCollection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierTeammateOutlineCollectionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), NAME_None,
		EUniqueObjectNameOptions::GloballyUnique);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Collection test world"), World)) return false;
	World->AddToRoot();
	AFirstPersonCharacter* Character = World->SpawnActor<AFirstPersonCharacter>();
	AMeleeWeaponTestActor* Weapon = World->SpawnActor<AMeleeWeaponTestActor>();
	AActor* ForeignActor = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Character"), Character) || !TestNotNull(TEXT("Weapon"), Weapon)
		|| !TestNotNull(TEXT("Unrelated attached actor"), ForeignActor))
	{
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		World->RemoveFromRoot();
		return false;
	}

	Character->EquipWeapon(Weapon);
	Weapon->ShowEquippedPresentation();
	USkeletalMeshComponent* Body = Character->GetMesh();
	USkeletalMeshComponent* ThirdPerson = Weapon->GetThirdPersonWeaponMesh();
	ThirdPerson->AttachToComponent(Body, FAttachmentTransformRules::KeepRelativeTransform);
	Weapon->GetShadowWeaponMesh()->AttachToComponent(Body, FAttachmentTransformRules::KeepRelativeTransform);
	Weapon->GetShadowWeaponMesh()->SetHiddenInGame(false);
	UStaticMeshComponent* Attachment = NewObject<UStaticMeshComponent>(Weapon);
	Weapon->AddInstanceComponent(Attachment);
	Attachment->SetupAttachment(ThirdPerson);
	Attachment->RegisterComponent();
	UStaticMeshComponent* ForeignMesh = NewObject<UStaticMeshComponent>(ForeignActor);
	ForeignActor->AddInstanceComponent(ForeignMesh);
	ForeignMesh->SetupAttachment(Body);
	ForeignMesh->RegisterComponent();

	TArray<UMeshComponent*> Meshes;
	UOutlierStencilSubsystem::GatherCharacterOutlineMeshes(Character, false, Meshes);
	TestTrue(TEXT("Body contributes"), Meshes.Contains(Body));
	TestTrue(TEXT("Separate owned weapon contributes"), Meshes.Contains(ThirdPerson));
	TestTrue(TEXT("Owned TP attachment contributes"), Meshes.Contains(Attachment));
	TestFalse(TEXT("Shadow copy never contributes even if shown"), Meshes.Contains(Weapon->GetShadowWeaponMesh()));
	TestFalse(TEXT("FP weapon never contributes to TP mask"), Meshes.Contains(Weapon->GetFirstPersonWeaponMesh()));
	TestFalse(TEXT("Arbitrary attached actor is excluded"), Meshes.Contains(ForeignMesh));

	// 카메라 별도 루트에 있는 FP 무기도 CurrentWeapon 참조로 찾는다. 본체 경로에 의존하지 않는다.
	Meshes.Reset();
	UOutlierStencilSubsystem::GatherCharacterOutlineMeshes(Character, true, Meshes);
	TestTrue(TEXT("Local FP body contributes"), Meshes.Contains(Character->GetFirstPersonMesh()));
	TestTrue(TEXT("FP weapon outside body subtree contributes"), Meshes.Contains(Weapon->GetFirstPersonWeaponMesh()));
	TestFalse(TEXT("TP body never blocks local view as FP"), Meshes.Contains(Body));

	ThirdPerson->SetHiddenInGame(true);
	Attachment->SetHiddenInGame(true);
	Meshes.Reset();
	UOutlierStencilSubsystem::GatherCharacterOutlineMeshes(Character, false, Meshes);
	TestFalse(TEXT("Hidden stowed TP weapon is excluded"), Meshes.Contains(ThirdPerson));
	ThirdPerson->SetHiddenInGame(false);
	Weapon->SetTestOwner(nullptr);
	Meshes.Reset();
	UOutlierStencilSubsystem::GatherCharacterOutlineMeshes(Character, false, Meshes);
	TestFalse(TEXT("Lost ownership removes TP before detach arrives"), Meshes.Contains(ThirdPerson));
	Weapon->SetTestOwner(Character);
	ThirdPerson->ComponentTags.Add(TEXT("Outlier.StencilExclude"));
	Meshes.Reset();
	UOutlierStencilSubsystem::GatherCharacterOutlineMeshes(Character, false, Meshes);
	TestFalse(TEXT("Explicitly excluded mesh does not contribute"), Meshes.Contains(ThirdPerson));

	World->DestroyWorld(true);
	World->SetPhysicsScene(nullptr);
	World->RemoveFromRoot();
	return true;
}

#endif
