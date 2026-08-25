#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Network/OutlierMatchRequest.h"
#include "OutlierArenaSettings.h"
#include "OutlierGameInstance.h"

namespace
{
FOutlierArenaHandoffRequest MakeHandoffRequest(
	const FGuid& MatchId,
	const FGuid& PlayerId,
	EOutlierPlayerRole Role)
{
	FOutlierArenaHandoffRequest Request;
	Request.MatchId = MatchId;
	Request.PlayerId = PlayerId;
	Request.Role = Role;
	return Request;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierArenaMapContractTest,
	"Outlier.Network.ArenaWorker.MapContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierArenaMapContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UOutlierArenaSettings* Settings = NewObject<UOutlierArenaSettings>();
	if (!TestNotNull(TEXT("Arena settings can be created for the contract test"), Settings))
	{
		return false;
	}

	Settings->ArenaLevel = TSoftObjectPtr<UWorld>(
		FSoftObjectPath(TEXT("/Game/Maps/TestArena.TestArena")));

	TestEqual(
		TEXT("The configured soft object resolves to a map package name"),
		Settings->GetArenaPackageName(),
		FString(TEXT("/Game/Maps/TestArena")));
	TestTrue(
		TEXT("The configured persistent map matches the arena"),
		Settings->MatchesArenaPackageName(TEXT("/Game/Maps/TestArena")));
	TestTrue(
		TEXT("A PIE-prefixed package still matches the configured arena"),
		Settings->MatchesArenaPackageName(TEXT("/Game/Maps/UEDPIE_3_TestArena")));
	TestFalse(
		TEXT("A different persistent map is not the configured arena"),
		Settings->MatchesArenaPackageName(TEXT("/Game/Maps/OtherArena")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierArenaHandoffUrlContractTest,
	"Outlier.Network.ArenaWorker.HandoffUrl",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierArenaHandoffUrlContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FGuid MatchId(1, 2, 3, 4);
	const FGuid PlayerId(5, 6, 7, 8);
	const FOutlierArenaHandoffRequest Original = MakeHandoffRequest(
		MatchId,
		PlayerId,
		EOutlierPlayerRole::Shooter);

	const FString Url = OutlierArenaHandoff::BuildTravelUrl(
		TEXT("127.0.0.1:7780"),
		Original);
	TestTrue(TEXT("A valid handoff produces a travel URL"), !Url.IsEmpty());
	TestTrue(
		TEXT("Whitespace around the configured address is ignored"),
		OutlierArenaHandoff::BuildTravelUrl(TEXT(" 127.0.0.1:7780 "), Original)
			== Url);
	TestTrue(
		TEXT("An empty arena address cannot produce a travel URL"),
		OutlierArenaHandoff::BuildTravelUrl(TEXT(" "), Original).IsEmpty());

	const int32 OptionsStart = Url.Find(TEXT("?"));
	if (!TestTrue(TEXT("The travel URL contains options"), OptionsStart != INDEX_NONE))
	{
		return false;
	}

	FOutlierArenaHandoffRequest Parsed;
	FString Error;
	TestTrue(
		TEXT("The generated URL options can be parsed"),
		OutlierArenaHandoff::TryParseOptions(Url.Mid(OptionsStart), Parsed, Error));
	TestTrue(TEXT("The parsed MatchId is preserved"), Parsed.MatchId == MatchId);
	TestTrue(TEXT("The parsed PlayerId is preserved"), Parsed.PlayerId == PlayerId);
	TestEqual(
		TEXT("The parsed role is preserved"),
		Parsed.Role,
		EOutlierPlayerRole::Shooter);

	TestFalse(
		TEXT("A missing MatchId is rejected"),
		OutlierArenaHandoff::TryParseOptions(
			FString::Printf(
				TEXT("?PlayerId=%s?Role=Shooter"),
				*PlayerId.ToString(EGuidFormats::DigitsWithHyphens)),
			Parsed,
			Error));
	TestFalse(
		TEXT("An unknown role is rejected"),
		OutlierArenaHandoff::TryParseOptions(
			FString::Printf(
				TEXT("?MatchId=%s?PlayerId=%s?Role=Unknown"),
				*MatchId.ToString(EGuidFormats::DigitsWithHyphens),
				*PlayerId.ToString(EGuidFormats::DigitsWithHyphens)),
			Parsed,
			Error));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierArenaAdmissionContractTest,
	"Outlier.Network.ArenaWorker.Admission",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierArenaAdmissionContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FGuid MatchId(1, 2, 3, 4);
	const FGuid OtherMatchId(9, 10, 11, 12);
	const FGuid ShooterId(5, 6, 7, 8);
	const FGuid PartnerId(13, 14, 15, 16);
	const FOutlierArenaHandoffRequest Shooter = MakeHandoffRequest(
		MatchId,
		ShooterId,
		EOutlierPlayerRole::Shooter);
	const FOutlierArenaHandoffRequest Partner = MakeHandoffRequest(
		MatchId,
		PartnerId,
		EOutlierPlayerRole::Partner);

	FOutlierArenaAdmissionState Admission;
	FString Error;
	TestTrue(TEXT("The first Shooter can reserve an empty worker"), Admission.Commit(Shooter, Error));
	TestFalse(TEXT("One participant is not a complete pair"), Admission.IsReady());

	FOutlierArenaHandoffRequest DifferentMatch = Partner;
	DifferentMatch.MatchId = OtherMatchId;
	TestFalse(TEXT("A different MatchId is rejected"), Admission.CanAccept(DifferentMatch, Error));

	FOutlierArenaHandoffRequest DuplicatePlayer = Partner;
	DuplicatePlayer.PlayerId = ShooterId;
	TestFalse(TEXT("A duplicate PlayerId is rejected"), Admission.CanAccept(DuplicatePlayer, Error));

	FOutlierArenaHandoffRequest DuplicateRole = Partner;
	DuplicateRole.Role = EOutlierPlayerRole::Shooter;
	TestFalse(TEXT("An occupied role is rejected"), Admission.CanAccept(DuplicateRole, Error));

	TestTrue(TEXT("The complementary Partner can join"), Admission.Commit(Partner, Error));
	TestTrue(TEXT("Shooter and Partner complete the worker pair"), Admission.IsReady());

	Admission.bPairStarted = true;
	FOutlierArenaHandoffRequest ThirdPlayer = MakeHandoffRequest(
		MatchId,
		FGuid(17, 18, 19, 20),
		EOutlierPlayerRole::Partner);
	TestFalse(TEXT("A started worker rejects additional participants"), Admission.CanAccept(ThirdPlayer, Error));
	TestEqual(
		TEXT("The started-worker rejection wins over slot details"),
		Error,
		FString(TEXT("Arena match already started")));

	FOutlierArenaAdmissionState ReleasedAdmission;
	TestTrue(TEXT("A participant can be committed before PostLogin"), ReleasedAdmission.Commit(Shooter, Error));
	ReleasedAdmission.Release(ShooterId);
	TestFalse(TEXT("Releasing the only participant clears the worker match"), ReleasedAdmission.MatchId.IsValid());
	TestTrue(TEXT("The released role can be admitted again"), ReleasedAdmission.CanAccept(Shooter, Error));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierArenaReturnLifecycleTest,
	"Outlier.Network.ArenaWorker.ReturnLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierArenaReturnLifecycleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UOutlierArenaSettings* Settings = NewObject<UOutlierArenaSettings>();
	if (!TestNotNull(TEXT("Arena settings can be created for the return contract test"), Settings))
	{
		return false;
	}

	TestEqual(
		TEXT("The default Lobby address targets the local Lobby server"),
		Settings->LobbyAddress,
		FString(TEXT("127.0.0.1:7777")));
	TestTrue(
		TEXT("Arena workers return players to the Lobby by default"),
		Settings->bReturnToLobbyOnMatchEnd);
	TestTrue(
		TEXT("Static cross-process Handoff is the default Network MVP path"),
		Settings->bUseStaticArenaHandoff);
	TestEqual(
		TEXT("Arena workers have a disconnect fallback timeout"),
		Settings->ArenaWorkerExitTimeoutSeconds,
		5.0f);

	UOutlierGameInstance* GameInstance = NewObject<UOutlierGameInstance>();
	if (!TestNotNull(TEXT("The game instance can be created for the return lifecycle test"), GameInstance))
	{
		return false;
	}

	GameInstance->NotifyArenaHandoffStarted();
	TestTrue(TEXT("Arena handoff activates Lobby recovery"), GameInstance->bArenaHandoffActive);
	TestFalse(TEXT("Lobby recovery is initially not queued"), GameInstance->bLobbyRecoveryQueued);
	TestFalse(TEXT("Lobby recovery is initially not attempted"), GameInstance->bLobbyRecoveryAttempted);

	TestTrue(TEXT("The first network failure queues Lobby recovery"), GameInstance->TryQueueLobbyRecovery());
	TestTrue(TEXT("Lobby recovery is queued after the first failure"), GameInstance->bLobbyRecoveryQueued);
	TestTrue(TEXT("The first recovery attempt is recorded"), GameInstance->bLobbyRecoveryAttempted);
	TestFalse(TEXT("A second network failure cannot queue another recovery"), GameInstance->TryQueueLobbyRecovery());

	GameInstance->ResetArenaHandoffState();
	TestFalse(TEXT("Completing Lobby travel clears the handoff state"), GameInstance->bArenaHandoffActive);
	TestFalse(TEXT("Completing Lobby travel clears queued recovery"), GameInstance->bLobbyRecoveryQueued);
	TestFalse(TEXT("Completing Lobby travel clears the recovery attempt"), GameInstance->bLobbyRecoveryAttempted);

	return true;
}

#endif
