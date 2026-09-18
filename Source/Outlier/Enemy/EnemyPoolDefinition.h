#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EnemyPoolDefinition.generated.h"

class AEnemyBase;

USTRUCT(BlueprintType)
struct OUTLIER_API FEnemyPoolEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Pool")
	TSoftClassPtr<AEnemyBase> EnemyClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Pool", meta = (ClampMin = "0", UIMin = "0"))
	int32 PrewarmCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Pool", meta = (ClampMin = "1", UIMin = "1"))
	int32 MaxCount = 1;
};

UCLASS(BlueprintType)
class OUTLIER_API UEnemyPoolDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Pool", meta = (TitleProperty = "EnemyClass"))
	TArray<FEnemyPoolEntry> Entries;
};
