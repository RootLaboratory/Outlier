#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "OutlierUpgradeTypes.generated.h"

class UTexture2D;

UENUM(BlueprintType)
enum class EOutlierUpgradeRole : uint8
{
	None,
	Shooter,
	Partner
};

UENUM(BlueprintType)
enum class EOutlierUpgradeNodeState : uint8
{
	Locked, //부모가 Acitvated가 아니라 못 찍는 것들
	Unlocked, //부모가 Activated라서 찍을 기회는 있음
	Activated //활성화
};

//나중에 RowName으로 Texture Mapping

USTRUCT(BlueprintType)
struct OUTLIER_API FUpgradeNodeTextureBinding
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Upgrade|Node Texture")
	FName NodeRowName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|Node Texture")
	TObjectPtr<UTexture2D> Texture;
};

USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierUpgradeNodeRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade")
	EOutlierUpgradeRole Role = EOutlierUpgradeRole::None;

	//TreeId 는 Combat, Shield 처럼 분류.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade")
	FName TreeId = NAME_None;

	// 1 , 2 , 3A 같은 것들 Key임.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade")
	FName NodeId = NAME_None;

	// Parent Node의 Row Name임.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade")
	FName ParentId = NAME_None;

	// 이 노드가 어떤 Ability에 관한 것인지 (UI 분류/컨텍스트용).
	// 실제 효과는 DT_UpgradeEffect 에서 NodeRowName 으로 조인해 가져온다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade", meta = (Categories = "Ability"))
	FGameplayTag AbilityTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade", meta = (ClampMin = "0"))
	int32 Cost = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade", meta = (MultiLine = "true"))
	FText Desc;
};

USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierUpgradeNodeViewData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade")
	FName RowName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade")
	FOutlierUpgradeNodeRow NodeRow;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade")
	EOutlierUpgradeNodeState State = EOutlierUpgradeNodeState::Locked;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade")
	bool bCanAfford = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade")
	int32 CurrentNodeCount = 0;
};
