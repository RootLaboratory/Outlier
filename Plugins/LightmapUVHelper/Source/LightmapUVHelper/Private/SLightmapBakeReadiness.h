#pragma once

#include "LightmapBakeReadiness.h"
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
	FLightmapBakeReadiness Report;
	TSharedPtr<SVerticalBox> CheckRows;
	FString Status;
};
