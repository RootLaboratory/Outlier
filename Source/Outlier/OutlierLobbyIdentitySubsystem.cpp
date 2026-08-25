// Fill out your copyright notice in the Description page of Project Settings.


#include "OutlierLobbyIdentitySubsystem.h"
#include "OutlierPlayerState.h"

FGuid UOutlierLobbyIdentitySubsystem::RegisterPlayer(APlayerController* PlayerController)
{
	// Client World에서는 발급하지 않는다.
	if (!PlayerController
		|| !GetWorld()
		|| GetWorld()->GetNetMode() == NM_Client)
	{
		return FGuid();
	}

	// 같은 Controller가 이미 등록됐다면 기존 ID를 반환한다.
	FGuid PlayerId;
	if (TryGetPlayerId(PlayerController, PlayerId))
	{
		// PlayerState에는 있지만 Registry에서 빠진 경우도 복구한다.
		ActivePlayers.FindOrAdd(PlayerId) = PlayerController;
		return PlayerId;
	}

	do
	{
		PlayerId = FGuid::NewGuid();
	} while (ActivePlayers.Contains(PlayerId));

	AOutlierPlayerState* PlayerState =
		PlayerController->GetPlayerState<AOutlierPlayerState>();

	if (!PlayerState)
	{
		return FGuid();
	}

	// GUID 생성
	PlayerState->SetTemporaryPlayerId(PlayerId);
	ActivePlayers.Add(PlayerId, PlayerController);

	return PlayerId;
}

void UOutlierLobbyIdentitySubsystem::UnregisterPlayer(AController* Controller)
{
	if (!Controller)
	{
		return;
	}

	FGuid PlayerId;
	if (TryGetPlayerId(Controller, PlayerId))
	{
		const TWeakObjectPtr<APlayerController>* RegisteredPlayer =
			ActivePlayers.Find(PlayerId);

		// Registry가 아직 이 Controller를 가리킬 때만 삭제한다.
		if (RegisteredPlayer && RegisteredPlayer->Get() == Controller)
		{
			ActivePlayers.Remove(PlayerId);
		}
	}

	// 이미 PlayerState가 사라졌거나 Map이 불일치한 경우도 정리
	for (auto It = ActivePlayers.CreateIterator(); It; ++It)
	{
		APlayerController* RegisteredController = It.Value().Get();

		if (!RegisteredController ||
			RegisteredController == Controller)
		{
			It.RemoveCurrent();
		}
	}
}

bool UOutlierLobbyIdentitySubsystem::TryGetPlayerId(const AController* Controller, FGuid& OutPlayerId) const
{
	OutPlayerId = FGuid();

	if (!Controller)
	{
		return false;
	}

	const AOutlierPlayerState* PlayerState = Controller->GetPlayerState<AOutlierPlayerState>();

	if (!PlayerState || !PlayerState->HasValidTemporaryPlayerId())
	{
		return false;
	}

	OutPlayerId = PlayerState->GetTemporaryPlayerId();
	return true;
}

APlayerController* UOutlierLobbyIdentitySubsystem::FindPlayer(const FGuid& PlayerId) const
{
	const TWeakObjectPtr<APlayerController>* FoundPlayer =
		ActivePlayers.Find(PlayerId);

	return FoundPlayer ? FoundPlayer->Get() : nullptr;
}

bool UOutlierLobbyIdentitySubsystem::RebindPlayer(const FGuid& PlayerId, APlayerController* PlayerController)
{
	if (!PlayerId.IsValid()
		|| !PlayerController
		|| !GetWorld()
		|| GetWorld()->GetNetMode() == NM_Client)
	{
		return false;
	}

	AOutlierPlayerState* PlayerState = PlayerController->GetPlayerState<AOutlierPlayerState>();
	if (!PlayerState)
	{
		return false;
	}

	PlayerState->SetTemporaryPlayerId(PlayerId);

	if (PlayerState->GetTemporaryPlayerId() != PlayerId)
	{
		return false;
	}

	ActivePlayers.FindOrAdd(PlayerId) = PlayerController;
	return true;
}
