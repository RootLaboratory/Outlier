#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Network/OutlierArenaProcessTypes.h"

#if WITH_EDITOR
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "OutlierArenaSettings.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierArenaCountSettingTest,
	"Outlier.Network.ArenaProcess.CountSetting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierArenaCountSettingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FIntProperty* CountProperty = FindFProperty<FIntProperty>(
		UOutlierArenaSettings::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UOutlierArenaSettings, StaticArenaSlots));
	if (!TestNotNull(TEXT("The existing StaticArenaSlots config key is retained"), CountProperty))
	{
		return false;
	}
	TestTrue(TEXT("Arena Count is backed by Game config"), CountProperty->HasAnyPropertyFlags(CPF_Config));
	TestEqual(TEXT("Project Settings shows Arena Count"), CountProperty->GetDisplayNameText().ToString(), FString(TEXT("Arena Count")));
	TestEqual(TEXT("Minimum count is one"), CountProperty->GetMetaData(TEXT("ClampMin")), FString(TEXT("1")));
	TestEqual(TEXT("Maximum count is sixteen"), CountProperty->GetMetaData(TEXT("ClampMax")), FString(TEXT("16")));

	// Project Settings와 같은 방식으로 속성 하나를 임시 파일에 저장한다.
	const FString ConfigPath = FPaths::CreateTempFilename(*FPaths::ProjectSavedDir(), TEXT("ArenaCount"), TEXT(".ini"));
	UOutlierArenaSettings* Settings = NewObject<UOutlierArenaSettings>();
	for (const int32 Count : {6, 2})
	{
		CountProperty->SetPropertyValue_InContainer(Settings, Count);
		Settings->UpdateSinglePropertyInConfigFile(CountProperty, ConfigPath);
		FConfigFile SavedConfig;
		SavedConfig.Read(ConfigPath);
		int32 SavedCount = 0;
		TestTrue(TEXT("Project Settings saves the existing StaticArenaSlots key"),
			SavedConfig.GetInt(TEXT("/Script/Outlier.OutlierArenaSettings"), TEXT("StaticArenaSlots"), SavedCount));
		TestEqual(TEXT("The saved key contains the edited count"), SavedCount, Count);
	}
	IFileManager::Get().Delete(*ConfigPath);
	return true;
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierArenaSlotRegistryTest,
	"Outlier.Network.ArenaProcess.SlotRegistry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierArenaSlotRegistryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FOutlierArenaSlotRegistry Registry;
	constexpr int32 BasePort = 7780;
	const struct
	{
		int32 RequestedCount;
		int32 ExpectedCount;
	} Cases[] = {{1, 1}, {2, 2}, {4, 4}, {6, 6}, {16, 16}, {0, 1}, {17, 16}};

	for (const auto& Case : Cases)
	{
		Registry.Initialize(TEXT("127.0.0.1"), BasePort, Case.RequestedCount);
		const FString Context = FString::Printf(TEXT("Requested count %d"), Case.RequestedCount);
		TestEqual(Context + TEXT(" registers the expected number of Slots"), Registry.Num(), Case.ExpectedCount);
		TestNull(Context + TEXT(" has no Slot beyond the configured capacity"), Registry.FindSlot(Case.ExpectedCount));

		for (int32 SlotId = 0; SlotId < Case.ExpectedCount; ++SlotId)
		{
			const FOutlierArenaSlotRecord* Slot = Registry.FindSlot(SlotId);
			if (TestNotNull(Context + TEXT(" contains the expected Slot"), Slot))
			{
				TestEqual(Context + TEXT(" stores the assigned game port"), Slot->Port, BasePort + SlotId);
				TestEqual(Context + TEXT(" maps the Slot to its port"), Slot->Address,
					FString::Printf(TEXT("127.0.0.1:%d"), BasePort + SlotId));
			}
			TestTrue(TEXT("A launched Worker enters Starting"), Registry.MarkStarting(SlotId, 100u + SlotId));
			TestTrue(TEXT("The matching Worker PID can mark its Slot Ready"), Registry.MarkReady(SlotId, 100u + SlotId));
		}

		const FGuid FirstMatchId = FGuid::NewGuid();
		FOutlierArenaSlotRecord AllocatedSlot;
		for (int32 SlotId = 0; SlotId < Case.ExpectedCount; ++SlotId)
		{
			const FGuid MatchId = SlotId == 0 ? FirstMatchId : FGuid::NewGuid();
			TestTrue(TEXT("A Ready Slot can be allocated"), Registry.TryAllocate(MatchId, AllocatedSlot));
			TestEqual(TEXT("Slots are allocated in stable order"), AllocatedSlot.SlotId, SlotId);
		}
		TestFalse(Context + TEXT(" leaves the next Match waiting"), Registry.TryAllocate(FGuid::NewGuid(), AllocatedSlot));

		TestTrue(TEXT("An allocated Slot can enter InMatch"), Registry.MarkInMatch(0, FirstMatchId));
		TestTrue(TEXT("An active Match can enter Releasing"), Registry.MarkReleasing(0, FirstMatchId));
		TestTrue(TEXT("A stopped Worker enters Failed"), Registry.MarkFailed(0));
		TestTrue(TEXT("A replacement Worker enters Starting"), Registry.MarkStarting(0, 200));
		TestFalse(TEXT("A Starting replacement cannot be allocated"), Registry.TryAllocate(FGuid::NewGuid(), AllocatedSlot));
		TestTrue(TEXT("A replacement Worker becomes Ready"), Registry.MarkReady(0, 200));
		TestTrue(TEXT("The replacement Slot serves the waiting Match"), Registry.TryAllocate(FGuid::NewGuid(), AllocatedSlot));
		TestEqual(TEXT("The same Slot index is reused"), AllocatedSlot.SlotId, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierArenaControlFrameTest,
	"Outlier.Network.ArenaProcess.ControlFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierArenaControlFrameTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FOutlierArenaControlMessage Original;
	Original.Type = EOutlierArenaControlMessageType::Allocate;
	Original.SlotId = 2;
	Original.ProcessId = 1234;
	Original.MatchId = FGuid::NewGuid();
	Original.SaveContext.OwnerId = FGuid::NewGuid();
	Original.SaveContext.SaveId = FGuid::NewGuid();
	Original.SaveContext.KeyVerifier = FString::ChrN(40, TEXT('A'));
	Original.SaveContext.bContinue = true;

	TArray<uint8> Frame;
	TestTrue(TEXT("A valid control message is framed"), OutlierArenaControl::EncodeFrame(Original, Frame));

	TArray<uint8> PartialFrame;
	PartialFrame.Append(Frame.GetData(), 3);
	FOutlierArenaControlMessage Decoded;
	FString Error;
	TestEqual(
		TEXT("A partial length prefix waits for more data"),
		OutlierArenaControl::TryDecodeFrame(PartialFrame, Decoded, Error),
		EOutlierArenaFrameDecodeResult::NeedMoreData);

	PartialFrame.Append(Frame.GetData() + 3, Frame.Num() - 3);
	TestEqual(
		TEXT("The completed frame decodes once"),
		OutlierArenaControl::TryDecodeFrame(PartialFrame, Decoded, Error),
		EOutlierArenaFrameDecodeResult::Message);
	TestEqual(TEXT("Message type round trips"), Decoded.Type, Original.Type);
	TestEqual(TEXT("Slot ID round trips"), Decoded.SlotId, Original.SlotId);
	TestEqual(TEXT("Process ID round trips"), Decoded.ProcessId, Original.ProcessId);
	TestEqual(TEXT("Match ID round trips"), Decoded.MatchId, Original.MatchId);
	TestEqual(TEXT("Save owner round trips"), Decoded.SaveContext.OwnerId,
		Original.SaveContext.OwnerId);
	TestEqual(TEXT("Save ID round trips"), Decoded.SaveContext.SaveId,
		Original.SaveContext.SaveId);
	TestEqual(TEXT("Verifier round trips"), Decoded.SaveContext.KeyVerifier,
		Original.SaveContext.KeyVerifier);
	TestTrue(TEXT("Continue flag round trips"), Decoded.SaveContext.bContinue);
	TestEqual(TEXT("The decoded frame is consumed"), PartialFrame.Num(), 0);

	FOutlierArenaControlMessage Starting;
	Starting.Type = EOutlierArenaControlMessageType::Starting;
	Starting.SlotId = 1;
	Starting.ProcessId = 4321;
	TArray<uint8> StartingFrame;
	TestTrue(
		TEXT("A pre-ready Worker can frame Starting without a Match ID"),
		OutlierArenaControl::EncodeFrame(Starting, StartingFrame));
	TestEqual(
		TEXT("The Starting frame decodes"),
		OutlierArenaControl::TryDecodeFrame(StartingFrame, Decoded, Error),
		EOutlierArenaFrameDecodeResult::Message);
	TestEqual(TEXT("Starting message type round trips"), Decoded.Type, Starting.Type);
	TestEqual(TEXT("Starting frame is consumed"), StartingFrame.Num(), 0);

	TArray<uint8> OversizedFrame = { 0x00, 0x01, 0x00, 0x01 };
	TestEqual(
		TEXT("An oversized frame is rejected before allocation"),
		OutlierArenaControl::TryDecodeFrame(OversizedFrame, Decoded, Error),
		EOutlierArenaFrameDecodeResult::Invalid);
	return true;
}

#endif
