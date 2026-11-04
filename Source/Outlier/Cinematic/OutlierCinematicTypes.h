#pragma once

#include "CoreMinimal.h"
#include "OutlierCinematicTypes.generated.h"

UENUM()
enum class EOutlierCinematicClientEvent : uint8
{
	Prepared,
	Ready,
	Failed,
	CompleteRequested
};

// 서버는 RPC를 받은 PlayerController로 발신자를 식별하고 인트로 ID와 단계를 검증한다.
USTRUCT()
struct OUTLIER_API FOutlierCinematicContext
{
	GENERATED_BODY()

	FOutlierCinematicContext() = default;
	FOutlierCinematicContext(const FGuid& InIntroId, EOutlierCinematicClientEvent InEvent)
		: IntroId(InIntroId), Event(InEvent) {}

	UPROPERTY()
	FGuid IntroId;

	UPROPERTY()
	EOutlierCinematicClientEvent Event = EOutlierCinematicClientEvent::Prepared;
};
