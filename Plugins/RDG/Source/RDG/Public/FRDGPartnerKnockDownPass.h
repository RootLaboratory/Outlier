#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "ScreenPass.h"

class FSceneView;
class FRHITexture;
struct FPartnerKnockDownParameters;

// Partner KnockDown(Reboot) 화면 연출.
// 마스크 패스(RDG.PartnerKnockDown.Mask)로 글리치 가중치를 굽고, 메인 패스(RDG.PartnerKnockDown)가
// 장면과 DamageHud에 같은 가중치와 슬라이스 이동량을 적용해 합성한다.
class FRDGPartnerKnockDownPass
{
public:
	// 0~1 회복 진행도. 디버그 오버라이드가 켜져 있으면 그 값을 쓴다.
	static float GetProgress(const FPartnerKnockDownParameters& Parameters);
	// DamageHud 페이드 아웃까지 포함한 불투명도.
	static float GetDamageHudOpacity(const FPartnerKnockDownParameters& Parameters);
	// DamageHud 공용 펄스 타임라인. Min → Max(EaseIn, PulseInTime) → Min(EaseOut, PulseOutTime), 이후 Min.
	static float EvaluateDamageHudPulse(const FPartnerKnockDownParameters& Parameters, float MinValue, float MaxValue);
	// 지수 펄스값(0 = 원본).
	static float GetDamageHudExponentPulse(const FPartnerKnockDownParameters& Parameters);
	// 세기 펄스값(AddDarken 덧셈 배율).
	static float GetDamageHudIntensity(const FPartnerKnockDownParameters& Parameters);
	// 셰이더에 들어가는 실제 지수. 1 / (1 + 펄스값). 펄스가 클수록 HUD가 진해진다.
	static float GetDamageHudExponent(const FPartnerKnockDownParameters& Parameters);
	// 숨쉬기 배율(BreathMin ~ 1). 펄스가 끝나기 전과 꺼져 있을 때는 1.
	static float GetDamageHudBreath(const FPartnerKnockDownParameters& Parameters);

	// MaskTexture가 없으면 전 화면 1로 취급한다.
	static FScreenPassTexture AddPass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FScreenPassTexture& SceneColor,
		const FPartnerKnockDownParameters& Parameters,
		FRHITexture* MaskTexture,
		FRHITexture* DamageHudTexture,
		const FScreenPassRenderTarget& OverrideOutput = FScreenPassRenderTarget());

private:
	static FRDGTextureRef AddMaskPass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FIntPoint& ViewSize,
		const FPartnerKnockDownParameters& Parameters,
		FRHITexture* MaskTexture);
};
