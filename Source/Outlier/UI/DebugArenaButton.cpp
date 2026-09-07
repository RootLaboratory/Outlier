// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/DebugArenaButton.h"
#include "Components/Button.h"
#include "FirstPerson/FirstPersonPlayerController.h"

UDebugArenaButton::UDebugArenaButton(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UDebugArenaButton::NativeConstruct()
{
	Super::NativeConstruct();

	if (DebugReloadButton)
	{
		DebugReloadButton->OnClicked.AddUniqueDynamic(this, &UDebugArenaButton::OnClicked_DebugReload);
	}
}

void UDebugArenaButton::OnClicked_DebugReload()
{
	APlayerController* PC = GetOwningPlayer();
	UE_LOG(LogTemp, Warning, TEXT("[DebugReload] Button clicked. OwningPC=%s"), *GetNameSafe(PC));
	if (!PC)
	{
		return;
	}

	// 요청한 페어(자기 ArenaId)만 대상 → 서버 권위로 위임. ArenaId는 서버가 PlayerState에서 도출.
	// APartnerPlayerController도 AFirstPersonPlayerController 파생이라 base 캐스팅 하나로 둘 다 처리됨.
	if (AFirstPersonPlayerController* FPC = Cast<AFirstPersonPlayerController>(PC))
	{
		UE_LOG(LogTemp, Warning, TEXT("[DebugReload] Calling Server_RequestArenaReload PC=%s"), *GetNameSafe(FPC));
		FPC->Server_RequestArenaReload();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[DebugReload] OwningPC is not AFirstPersonPlayerController: %s"), *GetNameSafe(PC));
	}
}
