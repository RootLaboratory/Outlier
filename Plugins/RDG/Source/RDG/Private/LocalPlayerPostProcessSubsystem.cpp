// Fill out your copyright notice in the Description page of Project Settings.


#include "LocalPlayerPostProcessSubsystem.h"
#include "SceneViewExtension.h"
#include "OutlierPostProcessSceneViewExtension.h"

void ULocalPlayerPostProcessSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	//UE_LOG(LogTemp, Error, TEXT("EXT Initalized"));

	if (ULocalPlayer* LP = GetLocalPlayer())
	{
		//UE_LOG(LogTemp, Error, TEXT("EXT Initalized"));
		ViewExtension = FSceneViewExtensions::NewExtension<FOutlierPostProcessSceneViewExtension>(LP);
	}
	else
	{
		//UE_LOG(LogTemp, Error, TEXT("LP NONE Initalized"));
	}
}

void ULocalPlayerPostProcessSubsystem::Deinitialize()
{
	Super::Deinitialize();
	ViewExtension.Reset();
}

void ULocalPlayerPostProcessSubsystem::ActivateSlideState()
{
	if (PlayerState.bIsSliding)
	{
		return;
	}

	//UE_LOG(LogTemp, Error, TEXT("ActivateSlideState"));

	PlayerState.bIsSliding = true;
	PostProcessParameters.MotionBlur.bEnabled = true;
	bDirty = true;
}

void ULocalPlayerPostProcessSubsystem::DeActivateSlideState()
{
	PlayerState.bIsSliding = false;
	PostProcessParameters.MotionBlur.bEnabled = false;
	bDirty = true;
}

void ULocalPlayerPostProcessSubsystem::TickFrame()
{
	if (!bDirty)
	{
		return;
	}

	CachedPostProcessParameters = PostProcessParameters;

	if (ViewExtension.IsValid())
	{
		ViewExtension->UpdateCachedParameters(CachedPostProcessParameters);
	}

	bDirty = false;
}

const FPostProcessStrcture& ULocalPlayerPostProcessSubsystem::GetPostProcessStrcture()
{
	return CachedPostProcessParameters;
}

bool ULocalPlayerPostProcessSubsystem::IsDirty()
{
	return bDirty;
}
