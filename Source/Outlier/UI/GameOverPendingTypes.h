#pragma once

#include "CoreMinimal.h"
#include "GameOverPendingTypes.generated.h"

UENUM(BlueprintType)
enum class EGameOverPendingChoice : uint8
{
	Continue,
	PresetLevel,
	MainMenu,
	// 리슨 GameOver에서만 사용한다. 전용 서버의 기존 선택 목록은 유지한다.
	QuitGame
};

/** GameOver 선택과 그 선택에 필요한 값만 전달한다. PresetLevel일 때 LevelIndex는 1~4. */
USTRUCT(BlueprintType)
struct FGameOverPendingRequest
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid RoundId;

	// 서버가 제안을 만들 때 발급한다. 같은 사망 흐름에서 이전 제안의 응답도 구분한다.
	UPROPERTY()
	FGuid ProposalId;

	bool MatchesProposal(const FGuid& InRoundId, const FGuid& InProposalId) const
	{
		return RoundId.IsValid() && ProposalId.IsValid()
			&& RoundId == InRoundId && ProposalId == InProposalId;
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game Over|Pending")
	EGameOverPendingChoice Choice = EGameOverPendingChoice::Continue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game Over|Pending", meta = (ClampMin = "0", ClampMax = "4"))
	int32 LevelIndex = 0;
};
