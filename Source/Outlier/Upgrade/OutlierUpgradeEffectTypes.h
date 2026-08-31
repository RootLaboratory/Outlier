#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "OutlierUpgradeEffectTypes.generated.h"

class UGameplayEffect;

// 하나의 업그레이드 노드가 만들어내는 효과의 "형태".
// 컴포넌트는 이 타입에 따라 담당 경로로 효과를 디스패치한다.
UENUM(BlueprintType)
enum class EOutlierUpgradeEffectType : uint8
{
	// AttributeSet 수치 변경 (캐릭터/무기 스탯). 컴포넌트가 런타임에 무한 GE를 합성해 적용한다.
	Attribute,
	// 능력 config 스칼라 변경 (쿨다운/지속/거리/드레인/연사 등 SuitConfig 필드).
	// SuitConfig 재계산 경로로 반영된다.
	AbilityConfig,
	// 새 능력 부여 / 레벨업. TargetTag(Ability.*)로 ASC가 grant/게이트한다.
	GrantAbility,
	// 미리 만든 전용 GameplayEffect(EffectClass)를 상시 적용한다.
	ApplyEffect,
	// Ability 본문이 읽는 동작 플래그(loose tag). TargetTag(Upgrade.*)를 ASC에 올린다.
	FunctionOverride
};

// 수치 적용 방식. Attribute / Cooldown / Duration 에서 주로 쓰인다.
UENUM(BlueprintType)
enum class EOutlierUpgradeModOp : uint8
{
	Additive,       // 합산 (예: -5, +50)
	Multiplicative, // 곱연산 (예: 0.9, 1.2)
	Override        // 덮어쓰기 (예: = 15)
};

// DataTable 행 = 효과 1개.  NodeRowName 으로 노드 토폴로지 테이블에 조인한다.
// 한 노드가 효과 여러 개면 NodeRowName 이 같은 행이 여러 개 존재한다.
USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierUpgradeEffectRow : public FTableRowBase
{
	GENERATED_BODY()

	// 이 효과가 속한 노드( DT_UpgradeNode 의 RowName )
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade|Effect")
	FName NodeRowName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade|Effect")
	EOutlierUpgradeEffectType EffectType = EOutlierUpgradeEffectType::Attribute;

	// 효과 대상 태그. 타입별 네임스페이스:
	//   Attribute        -> Attribute.<Set>.<Stat>
	//   AbilityConfig    -> Ability.<Role>.<Ability>  (수정할 필드는 ConfigField 로 지정)
	//   GrantAbility     -> Ability.<Role>.<Ability>
	//   ApplyEffect      -> State.<...>  (GE 의 Ongoing Tag Requirement 게이트)
	//   FunctionOverride -> Upgrade.<Role>.<Tree>.<Feature>
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade|Effect")
	FGameplayTag TargetTag;

	// AbilityConfig 전용: 수정할 SuitConfig 필드명 ( FOutlierShooterSuitAbilityDataRow 의 멤버명과 동일 ).
	// 예: CooldownSeconds, DurationSeconds, MaxPartnerDistance, ShieldDrainPerSecond, FireRateMultiplier.
	// 그 외 타입에서는 비워둔다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade|Effect")
	FName ConfigField = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade|Effect")
	EOutlierUpgradeModOp Op = EOutlierUpgradeModOp::Additive;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade|Effect")
	float Magnitude = 0.0f;

	// ApplyEffect 전용: 적용할 GameplayEffect 를 가리키는 키.
	// 실제 GE 클래스는 UOutlierUpgradeSetData::ApplyEffectClasses 에서 이 키로 조회한다.
	// ( CSV 에 에셋 경로를 하드코딩하지 않기 위함 ) 그 외 타입에서는 비워둔다.
	// 에디터에서는 DataAsset 들에 등록된 키 목록에서 드롭다운으로 고른다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade|Effect",
		meta = (GetOptions = "/Script/Outlier.OutlierUpgradeSetData.GetApplyEffectKeyOptions"))
	FName EffectClassKey = NAME_None;
};
