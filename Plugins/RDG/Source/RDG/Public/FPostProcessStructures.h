// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

struct FDatamoshingParameters
{
	int32 bEnabled = false;
	float Progress = 1.0f;
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

// UI까지 합성된 최종 텍스처에 적용할 chromatic aberration 입력 파라미터.
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
};
