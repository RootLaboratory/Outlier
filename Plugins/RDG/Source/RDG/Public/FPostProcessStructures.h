// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

struct FMotionBlurParameters
{
	int32 bEnabled = false; //Padding.?
	float BlendWeight = 0.7f;
	float Intensity = 0.3f;
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

// UI까지 합성된 최종 텍스처에 적용할 chromatic aberration 입력 파라미터.
struct FUIChromaticAberrationParameters
{
	int32 bEnabled = false;
	float StartOffset = 0.9f;
	float Intensity = 0.7f;
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
};
