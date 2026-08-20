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
enum class EOutlierUpgradeEffectDomain : uint8
{
	Stat,
	NonStat // 기획에는 이 NonStat이 두 개로 나눠져 있는데 이분적으로 처리
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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Upgrade|Texture")
	FName NodeRowName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|Texture")
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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade")
	EOutlierUpgradeEffectDomain EffectDomain = EOutlierUpgradeEffectDomain::Stat;

	//실질적 사용은 안 하고 있음. Upcast된 OutlierComponent Runtime tag에 주입하는 구조라 구체적인 자식 클래스가
	// 판단해야 한다면 해당 AbilityTag로 쿼리해서 처리해도 되긴 하는데 구조만 열어 놓음.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade", meta = (Categories = "Ability"))
	FGameplayTag AbilityTag;

	//런타임에 AbilityTag에 해당하는 Component가 해당 Tag를 읽어서 유효한 경우 수치 및 함수를 업데이트 하는 형식을 구상.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade", meta = (Categories = "Upgrade"))
	FGameplayTag UpgradeTag;

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
