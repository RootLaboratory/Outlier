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
	int32 MinThreshold = 80;
	float Scale = 100.0f;
	int32 bSortRows = true;
	int32 bSortColumns = true;
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

struct FPostProcessStrctureUI
{
	FUIChromaticAberrationParameters ChromaticAberration;
};

struct FPostProcessStrcture
{
	FMotionBlurParameters MotionBlur;
	FLensFlareParameters LensFlare;
	FBloomBlurParameters BloomBlur;
	FDualKawaseBlurParameters DualKawaseBlur;
	FDatamoshingParameters Datamoshing;
	FPixelSortingParameters PixelSorting;
	FADSBlurParameters ADSBlur;
};
