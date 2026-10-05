#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "ScreenPass.h"

struct FSplitPrismParameters;

// Slate 이후 backbuffer를 화면 가운데 세로선 기준으로 반 가른다. 블러는 Tonemap 앞 FRDGSplitPrismDefocusPass가 맡는다.
class FRDGSplitPrismPass
{
public:
	static FScreenPassTexture AddPass(
		FRDGBuilder& GraphBuilder,
		const FScreenPassTexture& Input,
		const FSplitPrismParameters& Parameters);
};
