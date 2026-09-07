#include "Network/OutlierArenaProcessTypes.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
FString MessageTypeToString(EOutlierArenaControlMessageType Type)
{
	switch (Type)
	{
	case EOutlierArenaControlMessageType::Ready:
		return TEXT("Ready");
	case EOutlierArenaControlMessageType::Allocate:
		return TEXT("Allocate");
	case EOutlierArenaControlMessageType::Release:
		return TEXT("Release");
	case EOutlierArenaControlMessageType::InMatch:
		return TEXT("InMatch");
	case EOutlierArenaControlMessageType::Releasing:
		return TEXT("Releasing");
	default:
		return FString();
	}
}

bool TryParseMessageType(const FString& Value, EOutlierArenaControlMessageType& OutType)
{
	if (Value == TEXT("Ready"))
	{
		OutType = EOutlierArenaControlMessageType::Ready;
		return true;
	}
	if (Value == TEXT("Allocate"))
	{
		OutType = EOutlierArenaControlMessageType::Allocate;
		return true;
	}
	if (Value == TEXT("Release"))
	{
		OutType = EOutlierArenaControlMessageType::Release;
		return true;
	}
	if (Value == TEXT("InMatch"))
	{
		OutType = EOutlierArenaControlMessageType::InMatch;
		return true;
	}
	if (Value == TEXT("Releasing"))
	{
		OutType = EOutlierArenaControlMessageType::Releasing;
		return true;
	}

	return false;
}

uint32 ReadBigEndianLength(const uint8* Bytes)
{
	return (static_cast<uint32>(Bytes[0]) << 24)
		| (static_cast<uint32>(Bytes[1]) << 16)
		| (static_cast<uint32>(Bytes[2]) << 8)
		| static_cast<uint32>(Bytes[3]);
}
}

bool OutlierArenaControl::EncodeFrame(
	const FOutlierArenaControlMessage& Message,
	TArray<uint8>& OutFrame)
{
	OutFrame.Reset();
	const FString TypeString = MessageTypeToString(Message.Type);
	if (TypeString.IsEmpty() || Message.SlotId < 0)
	{
		return false;
	}

	const bool bRequiresMatch = Message.Type != EOutlierArenaControlMessageType::Ready;
	if (bRequiresMatch && !Message.MatchId.IsValid())
	{
		return false;
	}

	TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	JsonObject->SetStringField(TEXT("type"), TypeString);
	JsonObject->SetNumberField(TEXT("slotId"), Message.SlotId);
	JsonObject->SetNumberField(TEXT("processId"), Message.ProcessId);
	if (Message.MatchId.IsValid())
	{
		JsonObject->SetStringField(
			TEXT("matchId"),
			Message.MatchId.ToString(EGuidFormats::DigitsWithHyphens));
	}

	FString JsonText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	if (!FJsonSerializer::Serialize(JsonObject, Writer))
	{
		return false;
	}

	FTCHARToUTF8 Utf8(*JsonText);
	const int32 PayloadSize = Utf8.Length();
	if (PayloadSize <= 0 || PayloadSize > MaxMessageBytes)
	{
		return false;
	}

	OutFrame.Reserve(sizeof(uint32) + PayloadSize);
	OutFrame.Add(static_cast<uint8>((PayloadSize >> 24) & 0xff));
	OutFrame.Add(static_cast<uint8>((PayloadSize >> 16) & 0xff));
	OutFrame.Add(static_cast<uint8>((PayloadSize >> 8) & 0xff));
	OutFrame.Add(static_cast<uint8>(PayloadSize & 0xff));
	OutFrame.Append(reinterpret_cast<const uint8*>(Utf8.Get()), PayloadSize);
	return true;
}

EOutlierArenaFrameDecodeResult OutlierArenaControl::TryDecodeFrame(
	TArray<uint8>& InOutBuffer,
	FOutlierArenaControlMessage& OutMessage,
	FString& OutError)
{
	OutMessage = FOutlierArenaControlMessage();
	OutError.Reset();
	if (InOutBuffer.Num() < static_cast<int32>(sizeof(uint32)))
	{
		return EOutlierArenaFrameDecodeResult::NeedMoreData;
	}

	const uint32 PayloadSize = ReadBigEndianLength(InOutBuffer.GetData());
	if (PayloadSize == 0 || PayloadSize > MaxMessageBytes)
	{
		OutError = TEXT("Invalid arena control message length");
		return EOutlierArenaFrameDecodeResult::Invalid;
	}

	const int32 FrameSize = sizeof(uint32) + static_cast<int32>(PayloadSize);
	if (InOutBuffer.Num() < FrameSize)
	{
		return EOutlierArenaFrameDecodeResult::NeedMoreData;
	}

	const ANSICHAR* Payload = reinterpret_cast<const ANSICHAR*>(
		InOutBuffer.GetData() + sizeof(uint32));
	FUTF8ToTCHAR Converter(Payload, PayloadSize);
	const FString JsonText(Converter.Length(), Converter.Get());

	TSharedPtr<FJsonObject> JsonObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		OutError = TEXT("Invalid arena control JSON");
		return EOutlierArenaFrameDecodeResult::Invalid;
	}

	FString TypeString;
	double SlotIdNumber = INDEX_NONE;
	double ProcessIdNumber = 0;
	if (!JsonObject->TryGetStringField(TEXT("type"), TypeString)
		|| !TryParseMessageType(TypeString, OutMessage.Type)
		|| !JsonObject->TryGetNumberField(TEXT("slotId"), SlotIdNumber)
		|| SlotIdNumber < 0
		|| SlotIdNumber > MAX_int32
		|| !JsonObject->TryGetNumberField(TEXT("processId"), ProcessIdNumber)
		|| ProcessIdNumber < 0
		|| ProcessIdNumber > MAX_uint32)
	{
		OutError = TEXT("Invalid arena control fields");
		return EOutlierArenaFrameDecodeResult::Invalid;
	}

	OutMessage.SlotId = static_cast<int32>(SlotIdNumber);
	OutMessage.ProcessId = static_cast<uint32>(ProcessIdNumber);
	if (OutMessage.Type != EOutlierArenaControlMessageType::Ready)
	{
		FString MatchIdString;
		if (!JsonObject->TryGetStringField(TEXT("matchId"), MatchIdString)
			|| !FGuid::Parse(MatchIdString, OutMessage.MatchId))
		{
			OutError = TEXT("Missing or invalid arena control MatchId");
			return EOutlierArenaFrameDecodeResult::Invalid;
		}
	}

	InOutBuffer.RemoveAt(0, FrameSize, EAllowShrinking::No);
	return EOutlierArenaFrameDecodeResult::Message;
}

void FOutlierArenaSlotRegistry::Initialize(
	const FString& Host,
	int32 BasePort,
	int32 SlotCount)
{
	Slots.Reset();
	const FString NormalizedHost = Host.TrimStartAndEnd();
	const int32 SafeCount = FMath::Clamp(SlotCount, 1, 16);
	Slots.Reserve(SafeCount);
	for (int32 SlotId = 0; SlotId < SafeCount; ++SlotId)
	{
		FOutlierArenaSlotRecord& Slot = Slots.AddDefaulted_GetRef();
		Slot.SlotId = SlotId;
		Slot.Address = FString::Printf(TEXT("%s:%d"), *NormalizedHost, BasePort + SlotId);
	}
}

bool FOutlierArenaSlotRegistry::MarkStarting(int32 SlotId, uint32 ProcessId)
{
	FOutlierArenaSlotRecord* Slot = FindSlot(SlotId);
	if (!Slot || ProcessId == 0)
	{
		return false;
	}

	Slot->State = EOutlierArenaSlotState::Starting;
	Slot->ProcessId = ProcessId;
	Slot->MatchId.Invalidate();
	return true;
}

bool FOutlierArenaSlotRegistry::MarkReady(int32 SlotId, uint32 ProcessId)
{
	FOutlierArenaSlotRecord* Slot = FindSlot(SlotId);
	if (!Slot || Slot->State != EOutlierArenaSlotState::Starting
		|| Slot->ProcessId != ProcessId)
	{
		return false;
	}

	Slot->State = EOutlierArenaSlotState::Ready;
	Slot->MatchId.Invalidate();
	return true;
}

bool FOutlierArenaSlotRegistry::TryAllocate(
	const FGuid& MatchId,
	FOutlierArenaSlotRecord& OutSlot)
{
	OutSlot = FOutlierArenaSlotRecord();
	if (!MatchId.IsValid())
	{
		return false;
	}

	for (FOutlierArenaSlotRecord& Slot : Slots)
	{
		if (Slot.State != EOutlierArenaSlotState::Ready)
		{
			continue;
		}

		Slot.State = EOutlierArenaSlotState::Allocated;
		Slot.MatchId = MatchId;
		OutSlot = Slot;
		return true;
	}

	return false;
}

bool FOutlierArenaSlotRegistry::MarkInMatch(int32 SlotId, const FGuid& MatchId)
{
	FOutlierArenaSlotRecord* Slot = FindSlot(SlotId);
	if (!Slot || Slot->State != EOutlierArenaSlotState::Allocated
		|| Slot->MatchId != MatchId)
	{
		return false;
	}

	Slot->State = EOutlierArenaSlotState::InMatch;
	return true;
}

bool FOutlierArenaSlotRegistry::MarkReleasing(int32 SlotId, const FGuid& MatchId)
{
	FOutlierArenaSlotRecord* Slot = FindSlot(SlotId);
	if (!Slot
		|| (Slot->State != EOutlierArenaSlotState::Allocated
			&& Slot->State != EOutlierArenaSlotState::InMatch)
		|| Slot->MatchId != MatchId)
	{
		return false;
	}

	Slot->State = EOutlierArenaSlotState::Releasing;
	return true;
}

bool FOutlierArenaSlotRegistry::ReleaseAllocation(const FGuid& MatchId)
{
	for (FOutlierArenaSlotRecord& Slot : Slots)
	{
		if (Slot.MatchId != MatchId || Slot.State != EOutlierArenaSlotState::Allocated)
		{
			continue;
		}

		Slot.State = EOutlierArenaSlotState::Ready;
		Slot.MatchId.Invalidate();
		return true;
	}

	return false;
}

bool FOutlierArenaSlotRegistry::MarkFailed(int32 SlotId)
{
	FOutlierArenaSlotRecord* Slot = FindSlot(SlotId);
	if (!Slot)
	{
		return false;
	}

	Slot->State = EOutlierArenaSlotState::Failed;
	Slot->ProcessId = 0;
	Slot->MatchId.Invalidate();
	return true;
}

FOutlierArenaSlotRecord* FOutlierArenaSlotRegistry::FindSlot(int32 SlotId)
{
	return Slots.IsValidIndex(SlotId) && Slots[SlotId].SlotId == SlotId
		? &Slots[SlotId]
		: nullptr;
}

const FOutlierArenaSlotRecord* FOutlierArenaSlotRegistry::FindSlot(int32 SlotId) const
{
	return Slots.IsValidIndex(SlotId) && Slots[SlotId].SlotId == SlotId
		? &Slots[SlotId]
		: nullptr;
}
