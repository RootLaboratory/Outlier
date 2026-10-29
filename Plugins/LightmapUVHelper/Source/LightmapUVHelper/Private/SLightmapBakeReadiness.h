#pragma once

#include "LightmapBakeReadiness.h"
#include "LightmapMeshDiagnostics.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;

class SLightmapBakeReadiness : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLightmapBakeReadiness) {} SLATE_END_ARGS()
	void Construct(const FArguments& Args);

private:
	FReply Refresh();
	FReply OpenPanel();
	FReply Export();
	FReply ScanMeshes();
	FReply ExportMeshes();
	FLightmapBakeReadiness Report;
	FLightmapMeshDiagnosticReport MeshReport;
	TSharedPtr<SVerticalBox> CheckRows;
	FString Status;
};
