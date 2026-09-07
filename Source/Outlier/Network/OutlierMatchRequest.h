#pragma once

#include "CoreMinimal.h"
#include "OutlierPlayerState.h"
#include "OutlierMatchRequest.generated.h"

class AController;

UENUM(BlueprintType)
enum class EOutlierPartyRequestResult : uint8
{
	Created,
	MemberJoined,
	Joined,
	Left,
	Disbanded,
	InvalidCode,
	PartyNotFound,
	RateLimited,
	Failed
};

enum class EOutlierPendingMatchSource : uint8
{
	SoloQueue,
	PremadeParty
};

USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierMatchRequest
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<AController> Controller;

	UPROPERTY()
	EOutlierPlayerRole DesiredRole = EOutlierPlayerRole::None;

	UPROPERTY()
	int32 PendingLobbySlotIndex = INDEX_NONE;

	UPROPERTY()
	double RequestTime = 0.0;
};

USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierPendingRolePickMatch
{
	GENERATED_BODY()

	UPROPERTY()
	int32 PendingMatchId = INDEX_NONE;

	UPROPERTY()
	TObjectPtr<AController> FirstController;

	UPROPERTY()
	TObjectPtr<AController> SecondController;

	UPROPERTY()
	TObjectPtr<AController> ShooterController;

	UPROPERTY()
	TObjectPtr<AController> PartnerController;

	UPROPERTY()
	double CreatedTime = 0.0;

	EOutlierPendingMatchSource Source = EOutlierPendingMatchSource::SoloQueue;

	bool Contains(AController* Controller) const
	{
		return FirstController == Controller || SecondController == Controller;
	}

	bool IsRoleTaken(EOutlierPlayerRole Role) const
	{
		return (Role == EOutlierPlayerRole::Shooter && ShooterController) ||
			(Role == EOutlierPlayerRole::Partner && PartnerController);
	}

	bool IsReady() const
	{
		return ShooterController && PartnerController;
	}

	bool ShouldRequeueRemainingPlayer() const
	{
		return Source == EOutlierPendingMatchSource::SoloQueue;
	}
};

USTRUCT()
struct OUTLIER_API FOutlierPendingParty
{
	GENERATED_BODY()

	UPROPERTY()
	FString PartyCode;

	UPROPERTY()
	FGuid LeaderPlayerId;

	UPROPERTY()
	TObjectPtr<AController> LeaderController;

	bool IsValid() const;
};

namespace OutlierPartyCode
{
	inline constexpr int32 Length = 6;

	OUTLIER_API FString Generate();
	OUTLIER_API FString Normalize(const FString& PartyCode);
	OUTLIER_API bool IsValid(const FString& PartyCode);
}

USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierMatchParticipant
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FGuid PlayerId;

	UPROPERTY(BlueprintReadOnly)
	EOutlierPlayerRole Role = EOutlierPlayerRole::None;

	bool IsValid() const
	{
		return PlayerId.IsValid()
			&& Role != EOutlierPlayerRole::None;
	}
};

USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierMatchAssignment
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FGuid MatchId;

	UPROPERTY(BlueprintReadOnly)
	FOutlierMatchParticipant Shooter;

	UPROPERTY(BlueprintReadOnly)
	FOutlierMatchParticipant Partner;

	bool IsValid() const
	{
		return MatchId.IsValid()
			&& Shooter.PlayerId.IsValid()
			&& Partner.PlayerId.IsValid()
			&& Shooter.Role == EOutlierPlayerRole::Shooter
			&& Partner.Role == EOutlierPlayerRole::Partner
			&& Shooter.PlayerId != Partner.PlayerId;
	}
};

USTRUCT()
struct OUTLIER_API FOutlierArenaHandoffRequest
{
	GENERATED_BODY()

	FGuid MatchId;
	FGuid PlayerId;
	EOutlierPlayerRole Role = EOutlierPlayerRole::None;

	bool IsValid() const
	{
		return MatchId.IsValid()
			&& PlayerId.IsValid()
			&& (Role == EOutlierPlayerRole::Shooter
				|| Role == EOutlierPlayerRole::Partner);
	}
};

struct OUTLIER_API FOutlierArenaAdmissionState
{
	FGuid MatchId;
	FGuid ShooterPlayerId;
	FGuid PartnerPlayerId;
	bool bPairStarted = false;

	bool CanAccept(
		const FOutlierArenaHandoffRequest& Request,
		FString& OutError) const;
	bool Commit(
		const FOutlierArenaHandoffRequest& Request,
		FString& OutError);
	void Release(const FGuid& PlayerId);
	bool IsReady() const;
};

namespace OutlierArenaHandoff
{
	OUTLIER_API FString BuildTravelUrl(
		const FString& ArenaAddress,
		const FOutlierArenaHandoffRequest& Request);

	OUTLIER_API bool TryParseOptions(
		const FString& Options,
		FOutlierArenaHandoffRequest& OutRequest,
		FString& OutError);
}
