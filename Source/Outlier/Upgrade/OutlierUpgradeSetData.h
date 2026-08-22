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
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Upgrade|Texture")
	void RefreshUnlockedNodeTextureBindings();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade")
	EOutlierUpgradeRole UpgradeRole = EOutlierUpgradeRole::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade")
	TObjectPtr<UDataTable> UpgradeDataTable;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade|Texture", meta = (TitleProperty = "NodeRowName"))
	TArray<FUpgradeNodeTextureBinding> UnlockedNodeTextures;

protected:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
