#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Templates/SubclassOf.h"
#include "Upgrade/OutlierUpgradeTypes.h"
#include "OutlierUpgradeSetData.generated.h"

class UDataTable;
class UGameplayEffect;

UCLASS(BlueprintType)
class OUTLIER_API UOutlierUpgradeSetData : public UDataAsset
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Upgrade|Texture")
	void RefreshUnlockedNodeTextureBindings();

	// FOutlierUpgradeEffectRow::EffectClassKey 의 에디터 드롭다운 옵션 소스.
	// 모든 UOutlierUpgradeSetData 의 ApplyEffectClasses 키를 모아 반환한다 ( 에디터 전용 ).
	UFUNCTION()
	static TArray<FName> GetApplyEffectKeyOptions();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade")
	EOutlierUpgradeRole UpgradeRole = EOutlierUpgradeRole::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade")
	TObjectPtr<UDataTable> UpgradeDataTable;

	// 효과 테이블 ( FOutlierUpgradeEffectRow ). 노드 테이블과 NodeRowName 으로 조인된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade")
	TObjectPtr<UDataTable> UpgradeEffectDataTable;

	// ApplyEffect 효과의 EffectClassKey -> 실제 GameplayEffect 클래스 매핑.
	// CSV 에 경로를 하드코딩하는 대신 여기서 에디터로 BP GE 를 지정한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade")
	TMap<FName, TSubclassOf<UGameplayEffect>> ApplyEffectClasses;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade|Texture", meta = (TitleProperty = "NodeRowName"))
	TArray<FUpgradeNodeTextureBinding> UnlockedNodeTextures;

protected:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
