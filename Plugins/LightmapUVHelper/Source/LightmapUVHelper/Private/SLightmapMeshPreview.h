#pragma once

#include "SEditorViewport.h"
#include "AdvancedPreviewScene.h"

class FAdvancedPreviewScene;
class UStaticMesh;
class UStaticMeshComponent;

class SLightmapMeshPreview : public SEditorViewport
{
public:
	SLATE_BEGIN_ARGS(SLightmapMeshPreview) {} SLATE_END_ARGS()
	void Construct(const FArguments& InArgs);
	virtual ~SLightmapMeshPreview() override;
	void SetMesh(UStaticMesh* Mesh);
	void FocusMesh();

protected:
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
	virtual void OnFocusViewportToSelection() override { FocusMesh(); }

private:
	TUniquePtr<FAdvancedPreviewScene> PreviewScene;
	UStaticMeshComponent* MeshComponent = nullptr; // Referenced by PreviewScene's GC collector.
};
