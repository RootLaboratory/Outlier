#include "Network/OutlierMatchRequest.h"

#include "Kismet/GameplayStatics.h"

namespace
{
constexpr TCHAR PartyCodeAlphabet[] = TEXT("23456789ABCDEFGHJKMNPQRSTUVWXYZ");
}

bool FOutlierPendingParty::IsValid() const
{
	return OutlierPartyCode::IsValid(PartyCode)
		&& LeaderPlayerId.IsValid()
		&& LeaderController;
}

FString OutlierPartyCode::Generate()
{
	FString PartyCode;
	PartyCode.Reserve(Length);
	const int32 AlphabetLength = UE_ARRAY_COUNT(PartyCodeAlphabet) - 1;

	for (int32 Index = 0; Index < Length; ++Index)
	{
		PartyCode.AppendChar(PartyCodeAlphabet[FMath::RandHelper(AlphabetLength)]);
	}

	return PartyCode;
}

FString OutlierPartyCode::Normalize(const FString& PartyCode)
{
	FString Normalized;
	Normalized.Reserve(PartyCode.Len());

	for (const TCHAR Character : PartyCode)
	{
		if (FChar::IsWhitespace(Character) || Character == TEXT('-'))
		{
			continue;
		}

		Normalized.AppendChar(FChar::ToUpper(Character));
	}

	return Normalized;
}

bool OutlierPartyCode::IsValid(const FString& PartyCode)
{
	if (PartyCode.Len() != Length)
	{
		return false;
	}

	for (const TCHAR Character : PartyCode)
	{
		if (!FCString::Strchr(PartyCodeAlphabet, Character))
		{
			return false;
		}
	}

	return true;
}

namespace
{
FString RoleToOption(EOutlierPlayerRole Role)
{
	switch (Role)
	{
	case EOutlierPlayerRole::Shooter:
		return TEXT("Shooter");
	case EOutlierPlayerRole::Partner:
		return TEXT("Partner");
	default:
		return FString();
	}
}

bool TryParseRole(const FString& Value, EOutlierPlayerRole& OutRole)
{
	if (Value == TEXT("Shooter"))
	{
		OutRole = EOutlierPlayerRole::Shooter;
		return true;
	}

	if (Value == TEXT("Partner"))
	{
		OutRole = EOutlierPlayerRole::Partner;
		return true;
	}

	OutRole = EOutlierPlayerRole::None;
	return false;
}
}

bool FOutlierArenaAdmissionState::CanAccept(
	const FOutlierArenaHandoffRequest& Request,
	FString& OutError) const
{
	OutError.Reset();

	if (!Request.IsValid())
	{
		OutError = TEXT("Invalid arena handoff options");
		return false;
	}

	if (bPairStarted)
	{
		OutError = TEXT("Arena match already started");
		return false;
	}

	if (MatchId.IsValid() && MatchId != Request.MatchId)
	{
		OutError = TEXT("Arena is reserved for another match");
		return false;
	}

	if (ShooterPlayerId == Request.PlayerId || PartnerPlayerId == Request.PlayerId)
	{
		OutError = TEXT("Player already joined this arena");
		return false;
	}

	const FGuid& RolePlayerId = Request.Role == EOutlierPlayerRole::Shooter
		? ShooterPlayerId
		: PartnerPlayerId;
	if (RolePlayerId.IsValid())
	{
		OutError = TEXT("Requested arena role is already occupied");
		return false;
	}

	return true;
}

bool FOutlierArenaAdmissionState::Commit(
	const FOutlierArenaHandoffRequest& Request,
	FString& OutError)
{
	if (!CanAccept(Request, OutError))
	{
		return false;
	}

	if (!MatchId.IsValid())
	{
		MatchId = Request.MatchId;
	}

	FGuid& RolePlayerId = Request.Role == EOutlierPlayerRole::Shooter
		? ShooterPlayerId
		: PartnerPlayerId;
	RolePlayerId = Request.PlayerId;
	return true;
}

void FOutlierArenaAdmissionState::Release(const FGuid& PlayerId)
{
	if (ShooterPlayerId == PlayerId)
	{
		ShooterPlayerId.Invalidate();
	}

	if (PartnerPlayerId == PlayerId)
	{
		PartnerPlayerId.Invalidate();
	}

	if (!ShooterPlayerId.IsValid() && !PartnerPlayerId.IsValid() && !bPairStarted)
	{
		MatchId.Invalidate();
	}
}

bool FOutlierArenaAdmissionState::IsReady() const
{
	return MatchId.IsValid()
		&& ShooterPlayerId.IsValid()
		&& PartnerPlayerId.IsValid();
}

FString OutlierArenaHandoff::BuildTravelUrl(
	const FString& ArenaAddress,
	const FOutlierArenaHandoffRequest& Request)
{
	const FString RoleOption = RoleToOption(Request.Role);
	const FString NormalizedAddress = ArenaAddress.TrimStartAndEnd();
	if (NormalizedAddress.IsEmpty() || !Request.IsValid() || RoleOption.IsEmpty())
	{
		return FString();
	}

	return FString::Printf(
		TEXT("%s?MatchId=%s?PlayerId=%s?Role=%s"),
		*NormalizedAddress,
		*Request.MatchId.ToString(EGuidFormats::DigitsWithHyphens),
		*Request.PlayerId.ToString(EGuidFormats::DigitsWithHyphens),
		*RoleOption);
}

bool OutlierArenaHandoff::TryParseOptions(
	const FString& Options,
	FOutlierArenaHandoffRequest& OutRequest,
	FString& OutError)
{
	OutRequest = FOutlierArenaHandoffRequest();
	OutError.Reset();

	const FString MatchIdOption = UGameplayStatics::ParseOption(Options, TEXT("MatchId"));
	const FString PlayerIdOption = UGameplayStatics::ParseOption(Options, TEXT("PlayerId"));
	const FString RoleOption = UGameplayStatics::ParseOption(Options, TEXT("Role"));

	if (!FGuid::Parse(MatchIdOption, OutRequest.MatchId))
	{
		OutError = TEXT("Missing or invalid MatchId");
		return false;
	}

	if (!FGuid::Parse(PlayerIdOption, OutRequest.PlayerId))
	{
		OutError = TEXT("Missing or invalid PlayerId");
		return false;
	}

	if (!TryParseRole(RoleOption, OutRequest.Role))
	{
		OutError = TEXT("Missing or invalid Role");
		return false;
	}

	return true;
}
