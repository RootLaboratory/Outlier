#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Network/OutlierArenaProcessTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierArenaSlotRegistryTest,
	"Outlier.Network.ArenaProcess.SlotRegistry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierArenaSlotRegistryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FOutlierArenaSlotRegistry Registry;
	Registry.Initialize(TEXT("127.0.0.1"), 7780, 4);
	TestEqual(TEXT("Four Arena Slots are configured"), Registry.Num(), 4);
	TestEqual(
		TEXT("The last Slot uses the fourth port"),
		Registry.FindSlot(3)->Address,
		FString(TEXT("127.0.0.1:7783")));

	for (int32 SlotId = 0; SlotId < 4; ++SlotId)
	{
		TestTrue(
			TEXT("A launched Worker enters Starting"),
			Registry.MarkStarting(SlotId, 100u + SlotId));
		TestTrue(
			TEXT("The matching Worker PID can mark its Slot Ready"),
			Registry.MarkReady(SlotId, 100u + SlotId));
	}

	TArray<FGuid> MatchIds;
	for (int32 SlotId = 0; SlotId < 4; ++SlotId)
	{
		const FGuid MatchId = FGuid::NewGuid();
		MatchIds.Add(MatchId);
		FOutlierArenaSlotRecord AllocatedSlot;
		TestTrue(TEXT("A Ready Slot can be allocated"), Registry.TryAllocate(MatchId, AllocatedSlot));
		TestEqual(TEXT("Slots are allocated in stable order"), AllocatedSlot.SlotId, SlotId);
	}

	FOutlierArenaSlotRecord FifthSlot;
	TestFalse(
		TEXT("A fifth Match waits while all four Slots are occupied"),
		Registry.TryAllocate(FGuid::NewGuid(), FifthSlot));

	TestTrue(
		TEXT("An allocated Slot can enter InMatch"),
		Registry.MarkInMatch(0, MatchIds[0]));
	TestTrue(
		TEXT("An active Match can enter Releasing"),
		Registry.MarkReleasing(0, MatchIds[0]));
	TestTrue(TEXT("A stopped Worker enters Failed"), Registry.MarkFailed(0));
	TestTrue(TEXT("A replacement Worker enters Starting"), Registry.MarkStarting(0, 200));
	TestTrue(TEXT("A replacement Worker becomes Ready"), Registry.MarkReady(0, 200));

	FOutlierArenaSlotRecord ReusedSlot;
	TestTrue(
		TEXT("The replacement Slot can serve the waiting Match"),
		Registry.TryAllocate(FGuid::NewGuid(), ReusedSlot));
	TestEqual(TEXT("The same Slot index is reused"), ReusedSlot.SlotId, 0);
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
	TestEqual(TEXT("The decoded frame is consumed"), PartialFrame.Num(), 0);

	TArray<uint8> OversizedFrame = { 0x00, 0x01, 0x00, 0x01 };
	TestEqual(
		TEXT("An oversized frame is rejected before allocation"),
		OutlierArenaControl::TryDecodeFrame(OversizedFrame, Decoded, Error),
		EOutlierArenaFrameDecodeResult::Invalid);
	return true;
}

#endif
