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
	float FocusDistanceWorld = 11.6f;    // 사이트-마스크 depth-band 판별 기준 거리(cm). 디버거에서 조절함. DOF 자체의 초점 거리는 별개로 ADSBlurSocketDistance(실측값)를 씀.

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

struct FUIChromaticAberrationParameters
{
	int32 bEnabled = false;
	float StartOffset = 0.2f;
	float Intensity = 0.4f;
	float Padding = 0.0f;
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
	FOverlayParameters Overlay;
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
};
