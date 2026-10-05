#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "ScreenPass.h"

struct FScreenBlackoutParameters;

// Slate 이후 backbuffer 체인 맨 끝에서 HUD까지 검정으로 덮는다. 꺼져 있거나 Alpha가 0이면 입력을 그대로 돌려준다.
class FRDGScreenBlackoutPass
{
public:
	static FScreenPassTexture AddPass(
		FRDGBuilder& GraphBuilder,
		const FScreenPassTexture& Input,
		const FScreenBlackoutParameters& Parameters);
};
