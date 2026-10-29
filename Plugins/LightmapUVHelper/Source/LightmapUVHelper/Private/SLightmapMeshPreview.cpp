#include "SLightmapMeshPreview.h"

#include "AdvancedPreviewScene.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

namespace
{
	class FLightmapPreviewClient : public FEditorViewportClient
	{
	public:
		using FEditorViewportClient::FEditorViewportClient;
		void DetachScene() { PreviewScene = nullptr; }

		virtual void Tick(float DeltaSeconds) override
		{
			FEditorViewportClient::Tick(DeltaSeconds);
			// Match the Static Mesh Editor: advance the preview world as well as the camera.
			// A static world with event-only draws leaves temporal rendering without continuous frames.
			if (PreviewScene && IsRealtime())
			{
				PreviewScene->GetWorld()->Tick(LEVELTICK_All, DeltaSeconds);
			}
		}
	};
}

void SLightmapMeshPreview::Construct(const FArguments& InArgs)
{
	PreviewScene = MakeUnique<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());
	PreviewScene->SetFloorVisibility(false, true);
	PreviewScene->SetEnvironmentVisibility(false, true);
	MeshComponent = NewObject<UStaticMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewScene->AddComponent(MeshComponent, FTransform::Identity);
	SEditorViewport::Construct(SEditorViewport::FArguments());
}

SLightmapMeshPreview::~SLightmapMeshPreview()
{
	// The base viewport can outlive our scene during destruction. Release its scene reference first.
	if (Client.IsValid())
	{
		Client->Viewport = nullptr;
		StaticCastSharedPtr<FLightmapPreviewClient>(Client)->DetachScene();
	}
}

TSharedRef<FEditorViewportClient> SLightmapMeshPreview::MakeEditorViewportClient()
{
	TSharedRef<FEditorViewportClient> NewClient = MakeShared<FLightmapPreviewClient>(nullptr, PreviewScene.Get(), SharedThis(this));
	NewClient->SetViewMode(VMI_Lit);
	// SEditorViewport registers its active timer during Construct when this is enabled.
	NewClient->SetRealtime(true);
	NewClient->ToggleOrbitCamera(true);
	NewClient->SetViewRotation(FRotator(-15.0, -135.0, 0.0));
	NewClient->EngineShowFlags.SetGrid(false);
	NewClient->EngineShowFlags.SetSelectionOutline(false);
	NewClient->bSetListenerPosition = false;
	return NewClient;
}

void SLightmapMeshPreview::SetMesh(UStaticMesh* Mesh)
{
	if (MeshComponent->GetStaticMesh() == Mesh)
	{
		// Checkbox/list refreshes must not reset the camera or recreate the preview component.
		Invalidate();
		return;
	}
	MeshComponent->SetStaticMesh(Mesh);
	Invalidate();
	FocusMesh();
}

void SLightmapMeshPreview::FocusMesh()
{
	if (Client.IsValid() && MeshComponent->GetStaticMesh())
	{
		const FBox Box = MeshComponent->Bounds.GetBox();
		Client->SetViewLocationForOrbiting(Box.GetCenter(), FMath::Max(50.0, Box.GetExtent().Size() * 2.5));
		Client->FocusViewportOnBox(Box, true);
		Invalidate();
	}
}
