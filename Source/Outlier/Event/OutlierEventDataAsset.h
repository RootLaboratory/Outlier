#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "OutlierEventDataAsset.generated.h"

class UDataTable;

/** One role's data sources. Goal and TTS references may be shared by both role assets. */
UCLASS(BlueprintType)
class OUTLIER_API UOutlierEventDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Data Tables", meta = (RequiredAssetDataTags = "RowStructure=/Script/Outlier.OutlierEventDataRow"))
	TSoftObjectPtr<UDataTable> EventTable;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Data Tables", meta = (RequiredAssetDataTags = "RowStructure=/Script/Outlier.OutlierInputDataRow"))
	TSoftObjectPtr<UDataTable> InputTable;

	/** IA expressions, separate from the InputTable's tutorial text and lifetime. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Data Tables", meta = (RequiredAssetDataTags = "RowStructure=/Script/Outlier.OutlierInputDisplayDataRow"))
	TSoftObjectPtr<UDataTable> InputDisplayTable;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Data Tables", meta = (RequiredAssetDataTags = "RowStructure=/Script/Outlier.OutlierGoalDataRow"))
	TSoftObjectPtr<UDataTable> GoalTable;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Data Tables", meta = (RequiredAssetDataTags = "RowStructure=/Script/Outlier.OutlierGifDataRow"))
	TSoftObjectPtr<UDataTable> GifTable;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Data Tables", meta = (RequiredAssetDataTags = "RowStructure=/Script/Outlier.OutlierTTSDataRow"))
	TSoftObjectPtr<UDataTable> TTSTable;

	/** Shared world-space guide definitions; both role assets should reference the same table. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Data Tables", meta = (RequiredAssetDataTags = "RowStructure=/Script/Outlier.OutlierWorldGuideDataRow"))
	TSoftObjectPtr<UDataTable> WorldGuideTable;
};
