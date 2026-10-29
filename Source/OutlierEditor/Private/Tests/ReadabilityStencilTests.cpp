#if WITH_DEV_AUTOMATION_TESTS

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Enemy/AutoTurret.h"
#include "Engine/World.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "PostProcess/MaterialPostProcessSubsystem.h"
#include "PostProcess/OutlierReadabilityTarget.h"
#include "PostProcess/OutlierPostProcessVolume.h"
#include "Materials/Material.h"
#include "UObject/Package.h"
#include "UObject/Script.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierReadabilityStencilTest,
	"Outlier.PostProcess.ReadabilityStencil",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierReadabilityStencilTest::RunTest(const FString& Parameters)
{
	// The transient world has not started play. Allow ProcessEvent to dispatch the interface
	// exactly as it will during gameplay, without booting the game's BeginPlay systems.
	FEditorScriptExecutionGuard ScriptExecutionGuard;
	const FName Name = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), NAME_None,
		EUniqueObjectNameOptions::GloballyUnique);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, Name, GetTransientPackage());
	if (!TestNotNull(TEXT("Test world"), World)) return false;
	World->AddToRoot();
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		World->RemoveFromRoot();
	};

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AFirstPersonCharacter* Character = World->SpawnActor<AFirstPersonCharacter>(SpawnParameters);
	AAutoTurret* Turret = World->SpawnActor<AAutoTurret>(SpawnParameters);
	if (!TestNotNull(TEXT("First-person character"), Character)
		|| !TestNotNull(TEXT("Turret"), Turret)) return false;

	UStaticMeshComponent* BodyPart = NewObject<UStaticMeshComponent>(Character);
	Character->AddInstanceComponent(BodyPart);
	BodyPart->AttachToComponent(Character->GetMesh(), FAttachmentTransformRules::KeepRelativeTransform);
	UWidgetComponent* Widget = NewObject<UWidgetComponent>(Character);
	Character->AddInstanceComponent(Widget);
	Widget->AttachToComponent(Character->GetMesh(), FAttachmentTransformRules::KeepRelativeTransform);
	USkeletalMeshComponent* Shadow = NewObject<USkeletalMeshComponent>(Character);
	Character->AddInstanceComponent(Shadow);
	Shadow->AttachToComponent(Character->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);

	IOutlierReadabilityTarget::Execute_InitializeReadabilityStencil(Character, false);
	TestEqual(TEXT("Default stencil uses the reserved readability number"), Character->ReadabilityStencilValue, 16);
	TestTrue(TEXT("3P body writes custom depth"), !!Character->GetMesh()->bRenderCustomDepth);
	TestEqual(TEXT("3P body stencil"), Character->GetMesh()->CustomDepthStencilValue, 16);
	TestEqual(TEXT("Attached 3P part stencil"), BodyPart->CustomDepthStencilValue, 16);
	TestFalse(TEXT("1P arms excluded"), !!Character->GetFirstPersonMesh()->bRenderCustomDepth);
	TestFalse(TEXT("Widget excluded"), !!Widget->bRenderCustomDepth);
	TestFalse(TEXT("Separate shadow mesh excluded"), !!Shadow->bRenderCustomDepth);

	// A presentation refresh must preserve the live scan value and its original restore state.
	UMaterialPostProcessSubsystem* PostProcess = World->GetSubsystem<UMaterialPostProcessSubsystem>();
	if (!TestNotNull(TEXT("Post-process subsystem"), PostProcess)) return false;
	PostProcess->ApplyScanStencil(Character, 1);
	IOutlierReadabilityTarget::Execute_InitializeReadabilityStencil(Character, true);
	TestEqual(TEXT("Presentation refresh preserves scan"), Character->GetMesh()->CustomDepthStencilValue, 1);
	PostProcess->ClearScanStencil(Character);
	TestEqual(TEXT("Scan restores readability"), Character->GetMesh()->CustomDepthStencilValue, 16);
	TestTrue(TEXT("Scan restore retains custom depth"), !!Character->GetMesh()->bRenderCustomDepth);
	TestFalse(TEXT("Scan restore leaves 1P arms excluded"), !!Character->GetFirstPersonMesh()->bRenderCustomDepth);

	USkeletalMeshComponent* NewPart = NewObject<USkeletalMeshComponent>(Character);
	Character->AddInstanceComponent(NewPart);
	NewPart->AttachToComponent(Character->GetMesh(), FAttachmentTransformRules::KeepRelativeTransform);
	IOutlierReadabilityTarget::Execute_InitializeReadabilityStencil(Character, true);
	TestEqual(TEXT("Refresh initializes newly attached meshes"), NewPart->CustomDepthStencilValue, 16);

	Turret->ReadabilityStencilValue = 17;
	IOutlierReadabilityTarget::Execute_InitializeReadabilityStencil(Turret, false);
	TArray<USkeletalMeshComponent*> TurretMeshes;
	Turret->GetComponents<USkeletalMeshComponent>(TurretMeshes);
	TestTrue(TEXT("Turret includes separate body and head"), TurretMeshes.Num() >= 2);
	for (const USkeletalMeshComponent* Mesh : TurretMeshes)
	{
		TestTrue(TEXT("EnemyBase initializes each turret mesh"), !!Mesh->bRenderCustomDepth);
		TestEqual(TEXT("Turret inherits the configurable stencil"), Mesh->CustomDepthStencilValue, 17);
	}

	AOutlierPostProcessVolume* Volume = World->SpawnActor<AOutlierPostProcessVolume>(SpawnParameters);
	if (!TestNotNull(TEXT("Post-process volume"), Volume)) return false;
	UMaterial* ReadabilityMaterial = NewObject<UMaterial>(Volume);
	ReadabilityMaterial->MaterialDomain = MD_PostProcess;
	UMaterial* OutlineMaterial = NewObject<UMaterial>(Volume);
	OutlineMaterial->MaterialDomain = MD_PostProcess;
	Volume->PostProcessMaterials.Add(EOutlierPostProcessMaterialType::Readability, ReadabilityMaterial);
	Volume->PostProcessMaterials.Add(EOutlierPostProcessMaterialType::PartnerOutline, OutlineMaterial);
	PostProcess->RegisterPostProcessVolume(Volume);
	const auto ReadabilityWeight = [Volume, ReadabilityMaterial]()
	{
		for (const FWeightedBlendable& Blendable : Volume->Settings.WeightedBlendables.Array)
		{
			if (Blendable.Object.Get() == ReadabilityMaterial) return Blendable.Weight;
		}
		return -1.0f;
	};
	TestEqual(TEXT("Registering volume enables readability"), ReadabilityWeight(), 1.0f);
	PostProcess->ResetAllPostProcess(false);
	TestEqual(TEXT("Hard reset disables readability"), ReadabilityWeight(), 0.0f);
	PostProcess->ResetAllPostProcess(true);
	TestEqual(TEXT("Normal reset restores readability"), ReadabilityWeight(), 1.0f);
	return true;
}

#endif
