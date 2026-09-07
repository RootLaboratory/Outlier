#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "AttributeSet.h"
#include "OutlierUpgradeProjectionSettings.generated.h"

// Effect DT 의 Attribute 효과( TargetTag = Attribute.<Set>.<Stat> )가 가리키는
// 실제 AttributeSet 클래스 + 프로퍼티명 매핑 1개.
// UOutlierUpgradeComponent::ResolveAttribute 가 여길 순회해서 FGameplayAttribute 를 만든다.
USTRUCT()
struct FOutlierUpgradeAttributeMapping
{
	GENERATED_BODY()

	// Effect DT TargetTag 와 정확히 일치해야 한다 ( 예: Attribute.Shield.Max ).
	UPROPERTY(EditAnywhere, Config, Category = "Upgrade")
	FGameplayTag Tag;

	// Tag 가 가리키는 AttributeSet 클래스.
	UPROPERTY(EditAnywhere, Config, Category = "Upgrade")
	TSubclassOf<UAttributeSet> AttributeSetClass;

	// AttributeSetClass 안의 FGameplayAttributeData 멤버 이름 ( 예: MaxShield ).
	UPROPERTY(EditAnywhere, Config, Category = "Upgrade")
	FName AttributeName = NAME_None;
};

// AbilityConfig / GrantAbility 효과의 TargetTag( Ability.<Role>.<Ability> )가
// FOutlierShooterSuitConfig 의 어느 서브 필드( QuantumLeap / BulletReflection / ... )에
// 대응하는지 매핑 1개.
// UOutlierUpgradeComponent 의 ResolveSuitRow 가 여길 순회해서 리플렉션으로 필드를 찾는다.
USTRUCT()
struct FOutlierUpgradeSuitRoleMapping
{
	GENERATED_BODY()

	// Effect DT TargetTag 와 정확히 일치해야 한다 ( 예: Ability.Shooter.QuantumLeap ).
	UPROPERTY(EditAnywhere, Config, Category = "Upgrade")
	FGameplayTag AbilityTag;

	// FOutlierShooterSuitConfig 안의 FOutlierShooterSuitAbilityDataRow 서브 필드 이름.
	UPROPERTY(EditAnywhere, Config, Category = "Upgrade")
	FName SuitConfigFieldName = NAME_None;
};

// Upgrade -> GAS 투영 단계에서 쓰는 Tag <-> C++ 필드 매핑을 코드 하드코딩 대신
// ini( DefaultGame.ini, [/Script/Outlier.OutlierUpgradeProjectionSettings] )로 뺀 설정.
// 새 Attribute 나 새 Role 을 추가할 때 재컴파일 없이 Project Settings 에서 행만 추가하면 된다.
// ( ConfigField -> FOutlierShooterSuitAbilityDataRow 필드는 이름이 항상 1:1로 같다는 기존 컨벤션을
//   그대로 이용해 리플렉션으로 직접 찾으므로, 이쪽은 별도 매핑 테이블이 필요 없다. )
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Outlier Upgrade Projection"))
class OUTLIER_API UOutlierUpgradeProjectionSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Config, Category = "Attribute")
	TArray<FOutlierUpgradeAttributeMapping> AttributeMappings;

	UPROPERTY(EditAnywhere, Config, Category = "AbilityConfig")
	TArray<FOutlierUpgradeSuitRoleMapping> SuitRoleMappings;
};
