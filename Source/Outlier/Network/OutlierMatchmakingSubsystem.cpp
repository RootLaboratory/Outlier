// Fill out your copyright notice in the Description page of Project Settings.


#include "Network/OutlierMatchmakingSubsystem.h"
#include "Network/OutlierArenaPoolSubsystem.h"
#include "OutlierGameMode.h"
#include "OutlierPlayerState.h"

void UOutlierMatchmakingSubsystem::SetMatchmakingMode(EOutlierMatchmakingMode NewMode)
{
	MatchmakingMode = NewMode;
}

void UOutlierMatchmakingSubsystem::EnqueueForPairThenRolePick(AController* Controller)
{
	if (!Controller)
	{
		return;
	}

	Cancel(Controller);

	FOutlierMatchRequest Request;
	Request.Controller = Controller;
	Request.RequestTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	WaitingPlayers.Add(Request);
	TryCreateMatch();
}

void UOutlierMatchmakingSubsystem::EnqueueByRole(AController* Controller, EOutlierPlayerRole DesiredRole)
{
	if (!Controller || DesiredRole == EOutlierPlayerRole::None)
	{
		return;
	}

	Cancel(Controller);

	FOutlierMatchRequest Request;
	Request.Controller = Controller;
	Request.DesiredRole = DesiredRole;
	Request.RequestTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	if (DesiredRole == EOutlierPlayerRole::Shooter)
	{
		WaitingShooters.Add(Request);
	}
	else if (DesiredRole == EOutlierPlayerRole::Partner)
	{
		WaitingPartners.Add(Request);
	}

	TryCreateMatch();
}

bool UOutlierMatchmakingSubsystem::SelectRoleInPendingMatch(AController* Controller, EOutlierPlayerRole DesiredRole)
{
	if (!Controller ||
		(DesiredRole != EOutlierPlayerRole::Shooter && DesiredRole != EOutlierPlayerRole::Partner))
	{
		return false;
	}

	for (int32 MatchIndex = 0; MatchIndex < PendingRolePickMatches.Num(); ++MatchIndex)
	{
		FOutlierPendingRolePickMatch& Match = PendingRolePickMatches[MatchIndex];
		if (!Match.Contains(Controller) || Match.IsRoleTaken(DesiredRole))
		{
			continue;
		}

		if (Match.ShooterController == Controller)
		{
			Match.ShooterController = nullptr;
		}

		if (Match.PartnerController == Controller)
		{
			Match.PartnerController = nullptr;
		}

		if (DesiredRole == EOutlierPlayerRole::Shooter)
		{
			Match.ShooterController = Controller;
		}
		else
		{
			Match.PartnerController = Controller;
		}

		if (Match.IsReady())
		{
			AController* ShooterController = Match.ShooterController;
			AController* PartnerController = Match.PartnerController;
			PendingRolePickMatches.RemoveAt(MatchIndex);

			CreateMatch(
				ShooterController,
				PartnerController,
				EOutlierPlayerRole::Shooter,
				EOutlierPlayerRole::Partner
			);
		}

		return true;
	}

	return false;
}

void UOutlierMatchmakingSubsystem::Cancel(AController* Controller)
{
	if (!Controller)
	{
		return;
	}

	auto RemoveController = [Controller](TArray<FOutlierMatchRequest>& Queue)
	{
		Queue.RemoveAll([Controller](const FOutlierMatchRequest& Request)
		{
			return Request.Controller == Controller;
		});
	};

	RemoveController(WaitingPlayers);
	RemoveController(WaitingShooters);
	RemoveController(WaitingPartners);

	for (int32 MatchIndex = PendingRolePickMatches.Num() - 1; MatchIndex >= 0; --MatchIndex)
	{
		FOutlierPendingRolePickMatch& Match = PendingRolePickMatches[MatchIndex];
		if (!Match.Contains(Controller))
		{
			continue;
		}

		AController* OtherController = Match.FirstController == Controller
			? Match.SecondController.Get()
			: Match.FirstController.Get();

		PendingRolePickMatches.RemoveAt(MatchIndex);

		if (OtherController)
		{
			FOutlierMatchRequest Request;
			Request.Controller = OtherController;
			Request.RequestTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
			WaitingPlayers.Add(Request);
		}
	}

	TryCreateMatch();
}

void UOutlierMatchmakingSubsystem::ReleaseMatch(int32 PairId)
{
	if (PairId == INDEX_NONE)
	{
		return;
	}

	if (const int32* ArenaId = ActivePairArenaIds.Find(PairId))
	{
		if (UWorld* World = GetWorld())
		{
			if (UOutlierArenaPoolSubsystem* ArenaPool = World->GetSubsystem<UOutlierArenaPoolSubsystem>())
			{
				ArenaPool->ReleaseArena(*ArenaId);
			}
		}

		ActivePairArenaIds.Remove(PairId);
	}
}

void UOutlierMatchmakingSubsystem::TryCreateMatch()
{
	switch (MatchmakingMode)
	{
	case EOutlierMatchmakingMode::PairThenRolePick:
		TryCreatePairThenRolePickMatch();
		break;

	case EOutlierMatchmakingMode::RoleQueue:
		TryCreateRoleQueueMatch();
		break;

	default:
		break;
	}
}

void UOutlierMatchmakingSubsystem::TryCreatePairThenRolePickMatch()
{
	while (WaitingPlayers.Num() >= 2)
	{
		FOutlierMatchRequest First = WaitingPlayers[0];
		FOutlierMatchRequest Second = WaitingPlayers[1];

		WaitingPlayers.RemoveAt(0, 2);

		if (!First.Controller || !Second.Controller)
		{
			continue;
		}

		FOutlierPendingRolePickMatch PendingMatch;
		PendingMatch.PendingMatchId = NextPendingMatchId++;
		PendingMatch.FirstController = First.Controller;
		PendingMatch.SecondController = Second.Controller;
		PendingMatch.CreatedTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

		PendingRolePickMatches.Add(PendingMatch);
	}
}

void UOutlierMatchmakingSubsystem::TryCreateRoleQueueMatch()
{
	if (WaitingShooters.Num() <= 0 || WaitingPartners.Num() <= 0)
	{
		return;
	}

	FOutlierMatchRequest Shooter = WaitingShooters[0];
	FOutlierMatchRequest Partner = WaitingPartners[0];

	WaitingShooters.RemoveAt(0);
	WaitingPartners.RemoveAt(0);

	CreateMatch(
		Shooter.Controller,
		Partner.Controller,
		EOutlierPlayerRole::Shooter,
		EOutlierPlayerRole::Partner
	);
}

void UOutlierMatchmakingSubsystem::CreateMatch(
	AController* FirstController,
	AController* SecondController,
	EOutlierPlayerRole FirstRole,
	EOutlierPlayerRole SecondRole)
{
	if (!FirstController || !SecondController)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UOutlierArenaPoolSubsystem* ArenaPool =
		World->GetSubsystem<UOutlierArenaPoolSubsystem>();

	if (!ArenaPool)
	{
		return;
	}

	FOutlierArenaInstance* Arena = ArenaPool->AcquireArena();
	if (!Arena)
	{
		return;
	}

	const int32 PairId = NextPairId++;

	AOutlierGameMode* GameMode = World->GetAuthGameMode<AOutlierGameMode>();
	if (!GameMode)
	{
		ArenaPool->ReleaseArena(Arena->ArenaId);
		return;
	}

	Arena->PairId = PairId;
	ActivePairArenaIds.Add(PairId, Arena->ArenaId);

	GameMode->StartMatchedPair(
		FirstController,
		SecondController,
		PairId,
		Arena->ArenaId,
		FirstRole,
		SecondRole
	);
}
