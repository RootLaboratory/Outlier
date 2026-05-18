// Fill out your copyright notice in the Description page of Project Settings.


#include "OutlierGameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "UI/LoadingWidget.h"
#include "Misc/Parse.h"

void UOutlierGameInstance::Init()
{
	Super::Init();

	FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &UOutlierGameInstance::HandlePreLoadMap);
	FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UOutlierGameInstance::HandlePostLoadMap);
}

void UOutlierGameInstance::HandlePostLoadMap(UWorld* LoadedWorld)
{
	if (bTriedConnect || !LoadedWorld)
	{
		return;
	}

	if (LoadedWorld->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	FString ConnectAddress;
	if (!FParse::Value(FCommandLine::Get(), TEXT("Connect="), ConnectAddress))
	{
		return;
	}

	if (ConnectAddress.IsEmpty())
	{
		return;
	}

	bTriedConnect = true;

	if (APlayerController* PC = LoadedWorld->GetFirstPlayerController())
	{
		PC->ClientTravel(ConnectAddress, TRAVEL_Absolute);
	}
}

void UOutlierGameInstance::HandlePreLoadMap(const FString& MapName)
{
	if (IsRunningDedicatedServer()) return;
	// Loading Widget 표시
}
