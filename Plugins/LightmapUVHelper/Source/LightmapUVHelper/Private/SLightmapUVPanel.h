#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "LightmapUVOperations.h"
#include "LightmapUVSettings.h"
#include "Engine/StaticMesh.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class FAssetThumbnail;
class FAssetThumbnailPool;
class IDetailsView;
class SEditableTextBox;
class SLightmapMeshPreview;

struct FLightmapUVRow
{
	FAssetData Asset;
	TStrongObjectPtr<UStaticMesh> Mesh;
	TSharedPtr<FAssetThumbnail> Thumbnail;
	bool bChecked = true;
	bool bHasPlan = false;
	FLightmapUVPlan Plan;
	FString Current;
	FString Result;
};

class SLightmapUVPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLightmapUVPanel) {} SLATE_END_ARGS()
	void Construct(const FArguments& InArgs);
	virtual ~SLightmapUVPanel() override;

private:
	FReply Scan();
	FReply PreviewChanges();
	FReply ApplyChanges();
	FReply SaveChanges();
	FReply SelectAll(bool bChecked);
	FReply RefreshLighting();
	TSharedRef<SWidget> MakePathPicker();
	TSharedRef<ITableRow> MakeRow(TSharedPtr<FLightmapUVRow> Item, const TSharedRef<STableViewBase>& Owner);
	void SelectMesh(TSharedPtr<FLightmapUVRow> Item, ESelectInfo::Type SelectInfo);
	void InvalidatePlan();
	void InvalidateSelection();
	void Refilter();
	bool CanApply() const;
	FText SelectedDescription() const;
	FText Counts() const;

	TStrongObjectPtr<ULightmapUVSettings> Settings;
	TSharedPtr<IDetailsView> Details;
	TSharedPtr<SEditableTextBox> PathBox;
	TSharedPtr<SListView<TSharedPtr<FLightmapUVRow>>> List;
	TSharedPtr<SLightmapMeshPreview> MeshPreview;
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
	TArray<TSharedPtr<FLightmapUVRow>> Rows;
	TArray<TSharedPtr<FLightmapUVRow>> VisibleRows;
	TSet<TWeakObjectPtr<UPackage>> ChangedPackages;
	TSharedPtr<FLightmapUVRow> Selected;
	FString Search;
	FString Lighting;
	FString Status;
	bool bRecursive = true;
	bool bOnlyNeedsChange = false;
	bool bPlanReady = false;
};
