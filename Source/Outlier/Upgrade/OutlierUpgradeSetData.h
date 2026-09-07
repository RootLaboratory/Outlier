#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Upgrade/OutlierUpgradeTypes.h"
#include "OutlierUpgradeSetData.generated.h"

class UDataTable;

UCLASS(BlueprintType)
class OUTLIER_API UOutlierUpgradeSetData : public UDataAsset
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Upgrade|Node Texture", meta = (DisplayName = "Refresh Node Texture Bindings"))
	void RefreshUnlockedNodeTextureBindings();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade")
	EOutlierUpgradeRole UpgradeRole = EOutlierUpgradeRole::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade")
	TObjectPtr<UDataTable> UpgradeDataTable;

	// 효과 테이블 ( FOutlierUpgradeEffectRow ). 노드 테이블과 NodeRowName 으로 조인된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade")
	TObjectPtr<UDataTable> UpgradeEffectDataTable;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade|Node Texture", meta = (TitleProperty = "NodeRowName", DisplayName = "Node Textures"))
	TArray<FUpgradeNodeTextureBinding> UnlockedNodeTextures;

protected:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
