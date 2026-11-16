#pragma once

#include "CoreMinimal.h"
#include "DeveloperConvenienceTypes.generated.h"

UENUM(BlueprintType)
enum class EDeveloperConvenienceOption : uint8
{
	Invincible,
	ConsumeCurrency,
	FastCooldown
};

UENUM(BlueprintType)
enum class EDeveloperConvenienceRequestResult : uint8
{
	Accepted,
	InvalidPair,
	StaleContext,
	NoSession,
	NotOperator,
	Transitioning,
	InvalidOption,
	UIUnavailable,
	TargetUnavailable,
	InvalidStage
};

USTRUCT()
struct FDeveloperConvenienceSessionRequest
{
	GENERATED_BODY()
	UPROPERTY()
	FGuid RequestId;
	UPROPERTY()
	FGuid MatchId;
	UPROPERTY()
	FGuid SessionId;
	UPROPERTY()
	int64 GameplayGeneration = 0;
	UPROPERTY()
	int64 Revision = 0;
	UPROPERTY()
	bool bOpen = true;
};

USTRUCT(BlueprintType)
struct FDeveloperConvenienceSessionResult
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	FGuid RequestId;
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	FGuid MatchId;
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	FGuid SessionId;
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	bool bOpen = true;
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	EDeveloperConvenienceRequestResult Result = EDeveloperConvenienceRequestResult::NoSession;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDeveloperConvenienceSessionResult,
	const FDeveloperConvenienceSessionResult&, Result);

USTRUCT(BlueprintType)
struct FDeveloperConvenienceOptions
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	bool bInvincible = false;
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	bool bConsumeCurrency = true;
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	bool bFastCooldown = false;

	bool operator==(const FDeveloperConvenienceOptions& Other) const
	{
		return bInvincible == Other.bInvincible && bConsumeCurrency == Other.bConsumeCurrency
			&& bFastCooldown == Other.bFastCooldown;
	}
};

// 서버 원본의 읽기 전용 사본이다. WBP는 표시 시 이 값을 읽고 클릭 시 같은 문맥을 요청에 넘긴다.
USTRUCT(BlueprintType)
struct FDeveloperConvenienceSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	FDeveloperConvenienceOptions Options;
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	int32 PairId = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	FGuid MatchId;
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	FGuid SessionId;
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	int64 GameplayGeneration = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	int64 Revision = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	bool bIsOperator = false;
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	bool bTransitioning = false;

	bool operator==(const FDeveloperConvenienceSnapshot& Other) const
	{
		return Options == Other.Options && PairId == Other.PairId && MatchId == Other.MatchId
			&& SessionId == Other.SessionId && GameplayGeneration == Other.GameplayGeneration
			&& Revision == Other.Revision && bIsOperator == Other.bIsOperator
			&& bTransitioning == Other.bTransitioning;
	}
};

USTRUCT()
struct FDeveloperConvenienceOptionRequest
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid RequestId;
	UPROPERTY()
	FGuid MatchId;
	UPROPERTY()
	FGuid SessionId;
	UPROPERTY()
	int64 GameplayGeneration = 0;
	UPROPERTY()
	EDeveloperConvenienceOption Option = EDeveloperConvenienceOption::Invincible;
	UPROPERTY()
	bool bEnabled = false;
};

// 결과는 상태 변경 명령이 아니다. 지연 응답도 원래 요청/세션을 식별할 수 있게 그대로 전달한다.
USTRUCT(BlueprintType)
struct FDeveloperConvenienceOptionResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	FGuid RequestId;
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	FGuid MatchId;
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	FGuid SessionId;
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	EDeveloperConvenienceOption Option = EDeveloperConvenienceOption::Invincible;
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	bool bRequestedValue = false;
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	EDeveloperConvenienceRequestResult Result = EDeveloperConvenienceRequestResult::NoSession;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDeveloperConvenienceStateChanged,
	const FDeveloperConvenienceSnapshot&, Snapshot);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDeveloperConvenienceOptionResult,
	const FDeveloperConvenienceOptionResult&, Result);

USTRUCT()
struct FDeveloperConvenienceRecoveryRequest
{
	GENERATED_BODY()
	UPROPERTY()
	FGuid RequestId;
	UPROPERTY()
	FGuid MatchId;
	UPROPERTY()
	FGuid SessionId;
	UPROPERTY()
	int64 GameplayGeneration = 0;
};

USTRUCT(BlueprintType)
struct FDeveloperConvenienceRecoveryResult
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	FGuid RequestId;
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	FGuid MatchId;
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	FGuid SessionId;
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	int64 GameplayGeneration = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	EDeveloperConvenienceRequestResult Result = EDeveloperConvenienceRequestResult::NoSession;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDeveloperConvenienceRecoveryResult,
	const FDeveloperConvenienceRecoveryResult&, Result);

UENUM(BlueprintType)
enum class EDeveloperConveniencePage : uint8
{
	MainMenu,
	LevelSelection
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDeveloperConveniencePageChanged,
	EDeveloperConveniencePage, Page);

USTRUCT()
struct FDeveloperConveniencePresetRequest
{
	GENERATED_BODY()
	UPROPERTY()
	FGuid RequestId;
	UPROPERTY()
	FGuid MatchId;
	UPROPERTY()
	FGuid SessionId;
	UPROPERTY()
	int64 GameplayGeneration = 0;
	UPROPERTY()
	FName StageId;
};

USTRUCT(BlueprintType)
struct FDeveloperConveniencePresetResult
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	FGuid RequestId;
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	FGuid MatchId;
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	FGuid SessionId;
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	int64 GameplayGeneration = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	FName StageId;
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	EDeveloperConvenienceRequestResult Result = EDeveloperConvenienceRequestResult::NoSession;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDeveloperConveniencePresetResult,
	const FDeveloperConveniencePresetResult&, Result);

USTRUCT()
struct FDeveloperConvenienceTitleRequest
{
	GENERATED_BODY()
	UPROPERTY()
	FGuid RequestId;
	UPROPERTY()
	FGuid MatchId;
	UPROPERTY()
	FGuid SessionId;
	UPROPERTY()
	int64 GameplayGeneration = 0;
};

USTRUCT(BlueprintType)
struct FDeveloperConvenienceTitleResult
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	FGuid RequestId;
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	FGuid MatchId;
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	FGuid SessionId;
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	int64 GameplayGeneration = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Developer Convenience")
	EDeveloperConvenienceRequestResult Result = EDeveloperConvenienceRequestResult::NoSession;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDeveloperConvenienceTitleResult,
	const FDeveloperConvenienceTitleResult&, Result);
