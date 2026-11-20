#pragma once

#include "CoreMinimal.h"

struct FGameOverStreamingStatus
{
	TSet<FName> RequiredContent;
	TArray<FString> PendingContent;
	bool bFailed = false;

	bool IsReady() const { return !bFailed && PendingContent.IsEmpty(); }
};

// RPC나 정지 직전 재조회가 같은 프레임에 들어와도 안정 프레임 수는 한 번만 센다.
struct FGameOverStreamingReadiness
{
	bool Observe(uint32 InGeneration, const FGameOverStreamingStatus& Status, uint64 Frame)
	{
		const bool bSameScope = HasSameScope(InGeneration, Status);
		const bool bSkippedFrame = LastFrame != MAX_uint64 && Frame != LastFrame && Frame != LastFrame + 1;
		if (!bSameScope || !Status.IsReady() || bSkippedFrame)
		{
			ConsecutiveFrames = 0;
		}
		if (!bSameScope)
		{
			Generation = InGeneration;
			RequiredContent = Status.RequiredContent;
		}
		if (Status.IsReady() && Frame != LastFrame)
		{
			ConsecutiveFrames = FMath::Min(ConsecutiveFrames + 1, RequiredFrames);
		}
		LastFrame = Frame;
		return IsReady();
	}

	bool IsReady() const { return ConsecutiveFrames == RequiredFrames; }
	bool HasSameScope(uint32 InGeneration, const FGameOverStreamingStatus& Status) const
	{
		return Generation == InGeneration && RequiredContent.Num() == Status.RequiredContent.Num()
			&& RequiredContent.Includes(Status.RequiredContent);
	}

private:
	static constexpr int32 RequiredFrames = 3;
	TSet<FName> RequiredContent;
	uint64 LastFrame = MAX_uint64;
	uint32 Generation = 0;
	int32 ConsecutiveFrames = 0;
};

struct FGameOverStreamingWait
{
	explicit FGameOverStreamingWait(double StartTime = 0.0) : LastSampleTime(StartTime) {}

	void Observe(double Now, bool bStreamingReady)
	{
		if (bWaiting)
		{
			WaitSeconds += Now - LastSampleTime;
		}
		LastSampleTime = Now;
		bWaiting = !bStreamingReady;
	}

	double GetWaitSeconds() const { return WaitSeconds; }
	bool ShouldWarn() const { return WaitSeconds >= 10.0; }
	bool HasExpired() const { return WaitSeconds >= 30.0; }

private:
	double LastSampleTime;
	double WaitSeconds = 0.0;
	bool bWaiting = true;
};
