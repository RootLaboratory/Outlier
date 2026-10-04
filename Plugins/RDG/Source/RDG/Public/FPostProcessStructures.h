// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

struct FDatamoshingParameters
{
	int32 bEnabled = false;
	float Progress = 1.0f;
};

enum class EPixelSortingMode : int32
{
	White = 0,
	Black = 1,
	Bright = 2,
	Dark = 3
};

enum class EPixelSortingCurve : int32
{
	Linear = 0,
	CubicEaseIn = 1
};

struct FPixelSortingParameters
{
	int32 bEnabled = false;
	int32 Mode = static_cast<int32>(EPixelSortingMode::Bright);
	int32 Curve = static_cast<int32>(EPixelSortingCurve::Linear);
	float Threshold = 255.0f;
	float Progress = 0.0f;
	// false면 Pixel Sorting만 수행하고 아래 목표 색상 보간은 건너뛴다.
	int32 bColorInterpolationEnabled = false;
	// 정렬 대상 픽셀은 Progress가 0->1로 진행되는 동안 이 색상으로 보간된다.
	// 알파는 원본 픽셀 값을 유지하므로 RGB만 사용한다.
	FLinearColor TargetColor = FLinearColor::Blue;
	int32 MinThreshold = 90;
	float Scale = 90.0f;
	int32 bSortRows = false;
	int32 bSortColumns = true;

	// 정렬을 원본 해상도의 1/N에서 수행하고 결과를 다시 합성함. 정렬 비용이
	// O(n log^2 n)이라 2면 대략 4~5배 싸짐. 1이면 축소 없이 풀 해상도.
	int32 ResolutionDivisor = 2;
};

// 중심으로 빨려 들어가는 느낌의 줌 블러. 각 픽셀에서 화면 중심 방향으로 훑으며
// 평균내므로, 중심에 가까울수록 훑는 구간이 짧아져 자연히 선명하게 남음.
// 벨로시티 기반인 FMotionBlurParameters와는 무관한 별개 효과임.
struct FZoomBlurParameters
{
	int32 bEnabled = false;

	// 해킹 Possess 전환용 암전 알파. 0은 ZoomBlur 화면, 1은 완전한 검정색이다.
	float BlackFlushAlpha = 0.0f;

	// 해킹 전환 중 ZoomBlur 자체 진행도. Strength의 0↔MaximumStrength 보간을 구동한다.
	float Progress = 0.0f;

	// PixelSorting Threshold가 이 값 이하가 되면 BlurToBlack 전환을 시작한다.
	int32 TriggerThreshold = 140;

	// ZoomBlur Progress가 이 지점에 도달하면 BlackFlushAlpha 진행을 시작한다.
	float BlackoutStartProgress = 0.2f;

	// ZoomBlur Progress에 적용되는 EPixelSortingCurve 값이다.
	int32 ZoomBlurCurve = static_cast<int32>(EPixelSortingCurve::Linear);

	// BlackFlushAlpha에 적용되는 EPixelSortingCurve 값이다.
	int32 BlackoutCurve = static_cast<int32>(EPixelSortingCurve::CubicEaseIn);

	// 암전 진입 시 ZoomBlur Progress의 0→1 진행 속도 배율이다.
	float ZoomBlurFadeInTimeScale = 0.5f;

	// Possess 이후 ZoomBlur Progress의 1→0 진행 속도 배율이다.
	float ZoomBlurFadeOutTimeScale = 3.0f;

	// BlackFlushAlpha의 0→1 진행 속도 배율이다.
	float BlackoutFadeInTimeScale = 0.5f;

	// Possess 이후 BlackFlushAlpha의 1→0 진행 속도 배율이다.
	float BlackoutFadeOutTimeScale = 2.0f;

	// 현재 프레임에 적용되는 ZoomBlur 강도. 해킹 전환 중 Progress에 의해 갱신된다.
	float Strength = 0.0f;

	// 해킹 전환이 Progress 1에 도달했을 때 적용할 최대 ZoomBlur 강도.
	float MaximumStrength = 0.3f;

	// 이 반경 안쪽은 원본을 유지함. 화면 중앙 UI 가독성 확보용.
	float StartOffset = 0.0f;

	// 적으면 줄기가 원본이 겹친 유령처럼 끊겨 보임. 디더링을 쓰므로 12~16이면 충분.
	int32 SampleCount = 16;

	// 블러를 1/N 해상도에서 수행하고 원본 위에 합성함. 선명해야 하는 중심은
	// 합성 시 원본을 그대로 쓰므로 축소 손실이 드러나지 않음.
	int32 ResolutionDivisor = 1;
};

struct FMotionBlurParameters
{
	int32 bEnabled = false; //Padding.?
	float BlendWeight = 0.7f;
	float Intensity = 1.0f;
	float VelocityScale = 0.5f;
};

struct FLensFlareParameters
{
	int32 bEnabled = false;
	float BlendWeight = 0.0f;
	float Intensity = 0.0f;
	float Threshold = 1.0f;

	FLinearColor Tint = FLinearColor::White;

};

struct FBloomBlurParameters
{
	int32 bEnabled = false;
	float BlendWeight = 0.0f;
	float Intensity = 0.0f;
	float Threshold = 1.0f;
	float BlurStrength = 0.0f;
	int32 PassCount = 1;
};

struct FDualKawaseBlurParameters
{
	int32 bEnabled = false;
	float BlurRadius = 1.0f;
	float BlendWeight = 1.0f;
	int32 DownsampleCount = 2;
};

struct FADSBlurParameters
{
	int32 bEnabled = false;              // ADS(조준) 블러 관련 패스 전체 on/off. 조준 램프 알파가 0보다 크고 디버그 토글이 켜져 있을 때 1.
	int32 WeaponStencilValue = 3;        
	float FocusDistanceWorld = 12.0f;    // 사이트-마스크 depth-band 판별 기준 거리(cm). 디버거에서 조절함. DOF 자체의 초점 거리는 별개로 ADSBlurSocketDistance(실측값)를 씀.

	// 홀로그램 조준경 유리는 무기 나머지 부분과 같은 WeaponStencilValue를 공유함 —
	// 그래서 스텐실만으로는 구분이 안 되고, FocusDistanceWorld(소켓 거리) 근방의
	// 월드공간 depth 대역 안에 있는지로 구분함. 조준경 링이 실제로 조준점 깊이와
	// 거의 같은 위치에 있는 유일한 무기 부위이기 때문.
	float SightDistanceThreshold = 1.0f; // 위 depth 판별의 허용 오차 폭(cm). 이 값보다 깊이차가 작으면 "조준경 링"으로 인식.

	// 화면공간 사이트-튜브 마스크. DoF 사이트-복원 패스를 하드게이트하는 데 써서,
	// (깊이가 없는 반투명) 조준경 유리 너머로 보이는 것도 선명하게 유지되게 함 —
	// 위의 depth-band 판별은 불투명한 링 부분만 잡아내기 때문.
	float SightMaskDilateRadius = 120.0f;   // 풀해상도 픽셀 단위. 유리 구멍을 다 덮을 만큼 안쪽으로 넓혀야 함.
	float SightMaskSoftness = 6.0f;        // 풀해상도 픽셀 단위, soft variant의 경계 페더링 폭.
	int32 bUseSoftSightMask = true;        // 하드 마스크 vs 소프트 마스크 비교 토글.

	// 디버거 전용 GPU 프로파일러 스코프. 켜면 ADS 관련 RDG 패스들이
	// stat gpu / profilegpu에서 "Outlier ADS ..." 항목으로 표시됨.
	int32 bEnableGpuStatScopes = false;
};

// 사망 연출용 루프 노이즈. UI를 제외한 게임 화면에만 걸리며 SVE Tonemap 이후, Fade/Black 위에서 돈다.
// PopupRetainerBox 머티리얼의 가로 슬라이스 글리치를 진행도 대신 경과 시간으로 굴린다.
// 슬라이스 높이는 글리치 프레임마다 무작위로 다시 나뉘고, 가끔 버스트로 크게 튄다.
struct FDeathNoiseParameters
{
	int32 bEnabled = false;

	// 노이즈가 켜진 뒤 경과 시간(초). 서브시스템 Tick이 0부터 누적한다.
	float Time = 0.0f;

	// 최대 세기. 이동량과 밝기에 같이 곱해진다. 램프가 꺼져 있으면 처음부터 이 값이다.
	float MaxIntensity = 0.09f;

	// 켜면 노이즈가 켜진 순간(Time 0)부터 RampDuration 동안 세기가 0 → MaxIntensity로 올라간다.
	int32 bRampIntensity = false;
	float RampDuration = 1.0f;

	float Seed = 0.0f;

	// 슬라이스 경계가 생길 수 있는 세로 격자 줄 수. 슬라이스 높이의 최소 단위가 된다.
	float SliceRows = 255.0f;
	// 격자 줄마다 새 슬라이스가 시작될 확률. 평균 슬라이스 높이 ≈ 1 / 이 값(줄).
	float SliceSplitChance = 0.2f;

	// 초당 글리치 프레임 수. 프레임마다 슬라이스 분할 / 이동 / 버스트를 전부 다시 뽑는다.
	float GlitchRate = 16.0f;
	// 평소 글리치 슬라이스의 최대 가로 이동량(뷰 폭 대비 비율).
	float GlitchStrength = 0.06f;
	// 평소 이 값 이상 난수를 뽑은 슬라이스만 글리치에 걸린다.
	float GlitchThreshold = 0.6f;
	// 글리치에 걸린 슬라이스에 더해지는 밝기.
	float GlitchGlow = 0.0f;

	// 글리치 프레임마다 버스트가 터질 확률.
	float BurstChance = 0.15f;
	// 버스트 프레임의 이동량 배율.
	float BurstStrength = 10.0f;
	// 버스트 프레임의 글리치 문턱. 평소보다 낮아서 더 많은 슬라이스가 한꺼번에 튄다.
	float BurstThreshold = 0.3f;

	// 글리치 밝기에 곱해지는 색. RGB만 사용한다.
	FLinearColor Tint = FLinearColor::White;
};

// 사망 연출에 바인드되는 텍스처 슬롯. 볼륨이 넣어주고, 서브시스템과 SVE가 이 순서로 들고 있다.
// Background는 lerp 목표(알파 무시), 나머지는 rgb × a를 더한다(straight alpha 기준).
enum class EDeathTransitionTexture : uint8
{
	// Black 1번 레이어.
	BlackBackground,
	// Black 2번 레이어. 이름은 이미지 구분용일 뿐 색상 이미지다.
	BlackNoise,
	// Fade / Black 결과 위에 따로 더해지는 비네트. Fade 시작 후 나와서 리스폰까지 남는다.
	FadeVignette,

	Count
};

constexpr int32 DeathTransitionTextureCount = static_cast<int32>(EDeathTransitionTexture::Count);

// 사망 연출 Fade. 게임 화면 전체를 TargetColor 쪽으로 균등하게 보간한다. UI 제외, Black 앞.
// 끝까지 보간하면 모든 픽셀이 TargetColor 하나로 같아지므로 MaxStrength에서 멈추는 게 핵심이다.
// 비네트 설정도 여기 두지만, 비네트는 Black 다음에 따로 그려서 Fade가 끝난 뒤에도 리스폰까지 남는다.
struct FDeathFadeParameters
{
	int32 bEnabled = false;
	// 비네트 그리기 여부. Fade는 Black 최종 단계에서 꺼지지만 비네트는 리스폰까지 켜져 있다.
	int32 bVignetteEnabled = false;

	// 보간 목표 색(연한 검정). RGB만 사용한다.
	FLinearColor TargetColor = FLinearColor(0.01f, 0.01f, 0.01f, 1.0f);

	// 최대 보간 비율. 실제 보간량은 곡선을 거친 Progress * MaxStrength.
	float MaxStrength = 1.0f;

	// Progress 0 → 1에 걸리는 시간(초).
	float Duration = 1.0f;

	// 끄면 선형. 켜면 Progress^EaseInPower로 처음엔 완만하다가 끝으로 갈수록 가파르게 오른다.
	int32 bEaseIn = true;
	// 클수록 초반이 더 느리고 끝이 더 가파르다. 1이면 선형, 3이면 CubicEaseIn과 같다.
	float EaseInPower = 2.0f;

	// Fade가 끝난 뒤 Black 시작까지 대기 시간(초).
	float Delay = 0.0f;

	// 비네트(RGBA)는 Black 다음에 따로 rgb × a로 더해져서, Fade / Black이 전체 픽셀을 바꿔도 리스폰까지 남는다.
	// Fade 시작 기준 VignetteDelay 뒤에 나와서 VignetteFadeDuration 동안 0 → 1로 오른다. 켜짐 여부는 Fade 패스 플래그를 따른다.
	float VignetteDelay = 0.2f;
	// 0이면 등장 즉시 1.
	float VignetteFadeDuration = 1.3f;

	// 현재 진행도(0~1). 사망 연출 시퀀스가 매 틱 갱신한다. 시간 기준 선형이며 곡선은 보간량 계산에서만 입힌다.
	float Progress = 0.0f;

	// Fade 시작 후 경과 시간(초). 비네트 타이밍용이라 Progress와 달리 Fade가 끝나도 비네트가 다 나올 때까지 흐른다.
	float ElapsedTime = 0.0f;
};

// 현재 Fade 보간량(0 ~ MaxStrength).
inline float GetDeathFadeAmount(const FDeathFadeParameters& Parameters)
{
	float CurvedProgress = FMath::Clamp(Parameters.Progress, 0.0f, 1.0f);
	if (Parameters.bEaseIn)
	{
		CurvedProgress = FMath::Pow(CurvedProgress, FMath::Max(Parameters.EaseInPower, 1.0f));
	}
	return CurvedProgress * FMath::Clamp(Parameters.MaxStrength, 0.0f, 1.0f);
}

// Fade 시작 기준 비네트가 다 나오는 시점(초).
inline float GetDeathFadeVignetteEndTime(const FDeathFadeParameters& Parameters)
{
	return FMath::Max(Parameters.VignetteDelay, 0.0f) + FMath::Max(Parameters.VignetteFadeDuration, 0.0f);
}

// 현재 비네트 세기(0~1). Fade 시작 기준 VignetteDelay부터 VignetteFadeDuration 동안 선형으로 오른다.
inline float GetDeathFadeVignetteAmount(const FDeathFadeParameters& Parameters)
{
	const float VignetteElapsedTime = Parameters.ElapsedTime - FMath::Max(Parameters.VignetteDelay, 0.0f);
	if (Parameters.VignetteFadeDuration <= 0.0f)
	{
		return VignetteElapsedTime >= 0.0f ? 1.0f : 0.0f;
	}
	return FMath::Clamp(VignetteElapsedTime / Parameters.VignetteFadeDuration, 0.0f, 1.0f);
}

// 사망 연출 Black 패스의 레이어. 순서가 곧 등장 순서이자 합성 순서다. 비네트는 Fade 패스로 옮겨 갔다.
enum class EDeathBlackLayer : uint8
{
	// 장면 → 이 텍스처 rgb로 lerp(알파 무시).
	Background,
	// Background 결과 위에 rgb × a를 더한다. 이름은 이미지 구분용일 뿐 색상 이미지다.
	Noise,

	Count
};

constexpr int32 DeathBlackLayerCount = static_cast<int32>(EDeathBlackLayer::Count);

// 사망 연출 Black. UI 제외, Fade 다음. Background 텍스처로 lerp한 뒤 Noise rgb × a를 더한다.
struct FDeathBlackParameters
{
	int32 bEnabled = false;

	// 레이어별 등장 지연(초). EDeathBlackLayer 순서로, 이전 레이어가 나온 시점 기준이다.
	// Background는 Blackout 시작 기준. 예) { 0, N } → Background가 나오고 N초 뒤 Noise.
	float LayerDelays[DeathBlackLayerCount] = { 0.0f, 0.5f };

	// 레이어별 페이드 시간(초). EDeathBlackLayer 순서. 등장 시점부터 이 시간 동안 세기가 0 → 1로 오르고 리스폰까지 유지된다.
	// Background는 lerp 비율, Noise는 더하는 양에 곱해진다. 0이면 등장 즉시 1.
	float LayerFadeDurations[DeathBlackLayerCount] = { 0.0f, 0.25f };

	// Blackout 시작 후 경과 시간(초). 사망 연출 시퀀스가 매 틱 갱신한다.
	float ElapsedTime = 0.0f;
};

// Blackout 시작 기준 레이어 등장 시점(초). 앞 레이어들의 지연을 누적한다.
inline float GetDeathBlackLayerStartTime(const FDeathBlackParameters& Parameters, EDeathBlackLayer Layer)
{
	float StartTime = 0.0f;
	for (int32 Index = 0; Index <= static_cast<int32>(Layer); ++Index)
	{
		StartTime += FMath::Max(Parameters.LayerDelays[Index], 0.0f);
	}
	return StartTime;
}

// Blackout 시작 기준 레이어 페이드가 끝나는 시점(초).
inline float GetDeathBlackLayerEndTime(const FDeathBlackParameters& Parameters, EDeathBlackLayer Layer)
{
	return GetDeathBlackLayerStartTime(Parameters, Layer)
		+ FMath::Max(Parameters.LayerFadeDurations[static_cast<int32>(Layer)], 0.0f);
}

// 현재 레이어 세기(0~1). 등장 시점부터 페이드 시간 동안 선형으로 오른다.
inline float GetDeathBlackLayerAmount(const FDeathBlackParameters& Parameters, EDeathBlackLayer Layer)
{
	const float LayerElapsedTime = Parameters.ElapsedTime - GetDeathBlackLayerStartTime(Parameters, Layer);
	const float FadeDuration = Parameters.LayerFadeDurations[static_cast<int32>(Layer)];
	if (FadeDuration <= 0.0f)
	{
		return LayerElapsedTime >= 0.0f ? 1.0f : 0.0f;
	}
	return FMath::Clamp(LayerElapsedTime / FadeDuration, 0.0f, 1.0f);
}

struct FUIChromaticAberrationParameters
{
	int32 bEnabled = false;
	float StartOffset = 0.2f;
	float Intensity = 0.4f;
	float Padding = 0.0f;
};

// 사망 연출 CA. Slate 이후 backbuffer에 돌아서 UI(PreSetLoadWidget 포함)까지 먹는다.
// 렌즈 좌표계 없이 화면 전체에서 R은 +Offset, B는 -Offset만큼 밀고 G는 제자리에 둔다.
// 시간에 따른 흔들림은 Noise가 맡으므로 여기는 고정 오프셋뿐이다.
struct FDeathChromaticAberrationParameters
{
	int32 bEnabled = false;

	// 화면 폭 / 높이 대비 비율.
	float OffsetX = 0.004f;
	float OffsetY = 0.0f;
};

// Drone Damage Feedback 마스크 슬롯. Enemy HUD의 모서리 텍스처 4장(Middle 제외)과 같은 순서.
enum class EDroneDamageMask : uint8
{
	LeftTop,
	LeftBottom,
	RightTop,
	RightBottom,
	Count
};

constexpr int32 DroneDamageMaskCount = static_cast<int32>(EDroneDamageMask::Count);

// Drone Damage Feedback. Tonemap 이후(UI 제외), 드러난 마스크(.r × .a, 화면 해상도에 맞춘 전체 화면 마스크) 안에만 그린다.
// 아래 세 효과를 플래그로 켜고 끄며, 켜진 것끼리 겹친다. 마지막에 색 × 마스크 × (기본 밝기 + 글리치 밝기)를 더한다.
// 드러난 동안 계속 돈다.
struct FDroneDamageFeedbackParameters
{
	int32 bEnabled = false;

	// 드러난 마스크. EDroneDamageMask 순서로 0 또는 1. 서브시스템이 채운다.
	float MaskWeights[DroneDamageMaskCount] = { 0.0f, 0.0f, 0.0f, 0.0f };

	// 켜진 뒤 경과 시간(초). 서브시스템 Tick이 누적한다.
	float Time = 0.0f;

	// 1. 슬라이스 글리치: 무작위 높이 가로 띠를 좌우로 민다. 사망 Noise와 같은 방식.
	int32 bSliceGlitch = true;
	// 2. 픽셀 소팅: 기존 Pixel Sorting 패스로 정렬한 화면을 마스크 안에 보여준다.
	int32 bPixelSort = false;
	// 3. 밀고 검게: 마스크 안 내용을 ShiftOffset만큼 밀고, 밀려서 빈 자리는 검게. 글리치 프레임마다 UV를 살짝 흔든다.
	int32 bShiftToBlack = false;

	// 슬라이스 글리치. 기본값은 사망 연출 Noise(FDeathNoiseParameters)와 같다.
	// 이동량과 글리치 밝기에 Intensity가 곱해진다(사망 Noise의 MaxIntensity).
	float Intensity = 0.09f;
	float Seed = 0.0f;
	// 슬라이스 경계가 생길 수 있는 세로 격자 줄 수.
	float SliceRows = 255.0f;
	// 격자 줄마다 새 슬라이스가 시작될 확률. 평균 슬라이스 높이 ≈ 1 / 이 값(줄).
	float SliceSplitChance = 0.2f;
	// 초당 글리치 프레임 수. 밀고 검게의 UV 흔들림도 이 박자를 따른다.
	float GlitchRate = 16.0f;
	// 글리치 슬라이스의 최대 가로 이동량(뷰 폭 대비).
	float GlitchStrength = 0.06f;
	// 이 값 이상 난수를 뽑은 슬라이스만 글리치에 걸린다. 밀고 검게의 흔들림도 같은 문턱을 쓴다.
	float GlitchThreshold = 0.6f;
	// 글리치에 걸린 슬라이스에 더해지는 밝기.
	float GlitchGlow = 0.0f;
	float BurstChance = 0.15f;
	float BurstStrength = 10.0f;
	float BurstThreshold = 0.3f;

	// 픽셀 소팅. 기본값은 기존 Pixel Sorting(해킹 전환)과 같고,
	// 문턱은 그 전환이 끝나는 값(MinThreshold 90)으로 고정한다. Rows = x축, Columns = y축 정렬.
	int32 PixelSortMode = static_cast<int32>(EPixelSortingMode::Bright);
	float PixelSortThreshold = 90.0f;
	int32 bPixelSortRows = false;
	int32 bPixelSortColumns = true;
	int32 PixelSortResolutionDivisor = 2;

	// 밀고 검게. 뷰 크기 대비 이동량과, 글리치 프레임마다 더하는 무작위 흔들림 크기.
	float ShiftOffsetX = 0.03f;
	float ShiftOffsetY = 0.0f;
	float ShiftJitter = 0.01f;

	// 마스크 안에 항상 더해지는 밝기.
	float MaskGlow = 0.1f;

	// 더해지는 색. RGB만 사용한다.
	FLinearColor Tint = FLinearColor::White;
};

// Split Prism 튜닝값. RDG Debugger에서 조정한다.
// 초점이 빗나간 양 d(t)가 1에서 0으로 간다. 지나침이 있으면 부호를 바꿔 가며 줄어든다(1 → -A → +A·k → 0).
// Split offset = StartOffset × d, 블러 반경 = MaxBlurRadius × |d|. 둘이 같은 순간에 0이 된다.
struct FSplitPrismSettings
{
	// 초점이 다 맞기까지 걸리는 시간(초). 지나침까지 포함한다.
	float FocusDuration = 2.0f;

	// 첫 접근(시작 → 첫 지나침) 곡선. 1이면 선형, 높을수록 초반에 빨리 붙고 끝에서 천천히.
	float FocusEasePower = 2.0f;

	// 첫 지나침 깊이(시작 대비). 0이면 지나치지 않고 바로 맞는다.
	float OvershootAmount = 0.15f;
	int32 OvershootCount = 1;

	// d = 1일 때 Split offset. 화면 높이 대비 비율.
	float StartOffset = 0.01f;

	// d = 1일 때 블러 원판 반경. 1080p 기준 px.
	float MaxBlurRadius = 4.0f;
	int32 BlurSampleCount = 64;

	// 색 번짐. R/B 반경이 G의 (1 ± FringeAmount)배. 앞초점(d > 0) 보라, 뒤초점(d < 0) 초록 테두리.
	float FringeAmount = 0.0f;

	// 원판 밝기 분포. 앞초점은 테두리가, 뒤초점은 가운데가 밝아진다.
	float BokehRimBias = 1.0f;

	// 깊이 잔차. 실제 갈라짐은 픽셀마다 1/D - 1/F에 비례한다. 링 위치를 1/F(t) = 1/D_subj + d(t)·Δ로 두면
	// 장면 갈라짐 = StartOffset × (d + r),  r = (1/D_subj - 1/D) / Δ 로 쪼개진다.
	// d는 화면 전체(HUD 포함, Slate 이후), r은 장면에만(HDR 디포커스 패스) 얹는다.
	bool bDepthSplit = true;

	// 초점이 다 맞은 뒤 r 처리. false = A(Settle): DepthSettleDuration 동안 r을 0으로 줄이고 끝.
	// true = B(Persist): StopSplitPrism까지 r을 유지(피사체 거리 밖은 계속 갈라짐, 물리 그대로).
	bool bPersistDepthSplit = false;

	// Δ. 시작(d = 1) 때 초점이 피사체보다 얼마나 가까이 있었나(디옵터, 1/m). 작을수록 깊이 차가 과장된다.
	float FocusSwingDiopter = 1.0f;

	// |r| 상한. 렌즈 바로 앞 물체가 끝없이 갈라지지 않게.
	float MaxDepthResidual = 1.5f;

	// A(Settle)에서 r을 0으로 줄이는 시간(초).
	float DepthSettleDuration = 0.4f;

	// 피사체 거리 D_subj는 시작 후 첫 틱에 화면 가운데로 라인 트레이스해서 고정한다. 안 맞으면 이 값(cm).
	float FallbackSubjectDistance = 1000.0f;
	float SubjectTraceDistance = 10000.0f;
};

// Split Prism. Slate 이후 backbuffer를 화면 가운데 세로선 기준으로 반 갈라
// Offset > 0이면 왼쪽 +Y / 오른쪽 -Y, 지나쳐서 Offset < 0이면 반대로 민다. HUD까지 같이 갈라진다.
struct FSplitPrismParameters
{
	int32 bEnabled = false;

	// 현재 offset. 화면 높이 대비 비율, 부호 있음. |Offset|만큼 화면을 확대해서 화면 밖을 읽지 않는다.
	float Offset = 0.0f;
};

// Split Prism 디포커스. Tonemap 앞(선형 HDR)에서 화면 전체를 같은 반경의 원판으로 흐린다(블러는 깊이 안 씀).
// 장면 갈라짐의 깊이 잔차 r도 여기서 UV를 밀어서 얹는다.
struct FSplitPrismDefocusParameters
{
	int32 bEnabled = false;

	// 초점이 빗나간 양 d. 부호 있음(+ 앞초점, - 뒤초점).
	float Defocus = 0.0f;

	// |d| = 1일 때 원판 반경. 1080p 기준 px.
	float MaxRadius = 4.0f;
	int32 SampleCount = 64;
	float FringeAmount = 0.0f;
	float RimBias = 1.0f;

	// 깊이 잔차. ResidualWeight가 0이면 끈다.
	float DepthOffset = 0.0f;          // StartOffset. 화면 높이 대비
	float InvSubjectDistance = 0.0f;   // 1/D_subj (1/m). 0이면 피사체 거리 미정 → 끔
	float InvFocusSwing = 1.0f;        // 1/Δ (m)
	float MaxResidual = 1.5f;
	float ResidualWeight = 0.0f;       // 0~1
};

// Slate가 렌더링을 마친 backbuffer 색상에 적용하는 Overlay 블렌드 효과.
struct FOverlayParameters
{
	int32 bEnabled = false;
	FLinearColor TintColor = FLinearColor::Red;
	float AccumulatedValue = 0.0f;
	float GoalValue = 0.3f;
};

struct FPostProcessStrctureUI
{
	FUIChromaticAberrationParameters ChromaticAberration;
	FDeathChromaticAberrationParameters DeathChromaticAberration;
	FOverlayParameters Overlay;
	FSplitPrismParameters SplitPrism;
};

struct FPostProcessStrcture
{
	FMotionBlurParameters MotionBlur;
	FLensFlareParameters LensFlare;
	FBloomBlurParameters BloomBlur;
	FDualKawaseBlurParameters DualKawaseBlur;
	FDatamoshingParameters Datamoshing;
	FPixelSortingParameters PixelSorting;
	FZoomBlurParameters ZoomBlur;
	FADSBlurParameters ADSBlur;
	FDeathNoiseParameters DeathNoise;
	FDeathFadeParameters DeathFade;
	FDeathBlackParameters DeathBlack;

	// Split Prism의 화면 전체 디포커스 블러.
	FSplitPrismDefocusParameters SplitPrismDefocus;

	FDroneDamageFeedbackParameters DroneDamageFeedback;
};
