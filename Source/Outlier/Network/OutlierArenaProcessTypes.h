#pragma once

#include "CoreMinimal.h"

enum class EOutlierArenaSlotState : uint8
{
	Starting,
	Ready,
	Allocated,
	InMatch,
	Releasing,
	Failed
};

enum class EOutlierArenaControlMessageType : uint8
{
	Ready,
	Allocate,
	Release,
	InMatch,
	Releasing,
	// Worker가 주기적으로 보내고 Lobby가 그대로 되돌려준다. Lobby가 사라진 것을
	// 감지하는 유일한 수단이므로 MatchId를 요구하지 않는다.
	Heartbeat
};

struct FOutlierArenaControlMessage
{
	EOutlierArenaControlMessageType Type = EOutlierArenaControlMessageType::Ready;
	int32 SlotId = INDEX_NONE;
	uint32 ProcessId = 0;
	FGuid MatchId;
};

enum class EOutlierArenaFrameDecodeResult : uint8
{
	NeedMoreData,
	Message,
	Invalid
};

namespace OutlierArenaControl
{
	inline constexpr int32 MaxMessageBytes = 16 * 1024;

	OUTLIER_API bool EncodeFrame(
		const FOutlierArenaControlMessage& Message,
		TArray<uint8>& OutFrame);
	OUTLIER_API EOutlierArenaFrameDecodeResult TryDecodeFrame(
		TArray<uint8>& InOutBuffer,
		FOutlierArenaControlMessage& OutMessage,
		FString& OutError);
}

struct FOutlierArenaSlotRecord
{
	int32 SlotId = INDEX_NONE;
	FString Address;
	EOutlierArenaSlotState State = EOutlierArenaSlotState::Failed;
	uint32 ProcessId = 0;
	FGuid MatchId;
};

class OUTLIER_API FOutlierArenaSlotRegistry
{
public:
	void Initialize(const FString& Host, int32 BasePort, int32 SlotCount);

	bool MarkStarting(int32 SlotId, uint32 ProcessId);
	bool MarkReady(int32 SlotId, uint32 ProcessId);
	bool TryAllocate(const FGuid& MatchId, FOutlierArenaSlotRecord& OutSlot);
	bool MarkInMatch(int32 SlotId, const FGuid& MatchId);
	bool MarkReleasing(int32 SlotId, const FGuid& MatchId);
	bool ReleaseAllocation(const FGuid& MatchId);
	bool MarkFailed(int32 SlotId);

	FOutlierArenaSlotRecord* FindSlot(int32 SlotId);
	const FOutlierArenaSlotRecord* FindSlot(int32 SlotId) const;
	int32 Num() const { return Slots.Num(); }
	const TArray<FOutlierArenaSlotRecord>& GetSlots() const { return Slots; }

private:
	TArray<FOutlierArenaSlotRecord> Slots;
};
