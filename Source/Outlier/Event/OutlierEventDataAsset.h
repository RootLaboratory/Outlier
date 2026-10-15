#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "OutlierEventDataAsset.generated.h"

class UDataTable;

/** 한 역할의 데이터 소스. Goal/TTS 참조는 두 역할 에셋이 공유할 수 있다. */
UCLASS(BlueprintType)
class OUTLIER_API UOutlierEventDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Data Tables", meta = (RequiredAssetDataTags = "RowStructure=/Script/Outlier.OutlierEventDataRow"))
	TSoftObjectPtr<UDataTable> EventTable;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Data Tables", meta = (RequiredAssetDataTags = "RowStructure=/Script/Outlier.OutlierInputDataRow"))
	TSoftObjectPtr<UDataTable> InputTable;

	/** IA 표현. InputTable의 튜토리얼 텍스트/수명과 별개이다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Data Tables", meta = (RequiredAssetDataTags = "RowStructure=/Script/Outlier.OutlierInputDisplayDataRow"))
	TSoftObjectPtr<UDataTable> InputDisplayTable;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Data Tables", meta = (RequiredAssetDataTags = "RowStructure=/Script/Outlier.OutlierGoalDataRow"))
	TSoftObjectPtr<UDataTable> GoalTable;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Data Tables", meta = (RequiredAssetDataTags = "RowStructure=/Script/Outlier.OutlierGifDataRow"))
	TSoftObjectPtr<UDataTable> GifTable;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Data Tables", meta = (RequiredAssetDataTags = "RowStructure=/Script/Outlier.OutlierTTSDataRow"))
	TSoftObjectPtr<UDataTable> TTSTable;

	/** 공유 월드 공간 가이드 정의. 두 역할 에셋이 같은 테이블을 참조해야 한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Data Tables", meta = (RequiredAssetDataTags = "RowStructure=/Script/Outlier.OutlierWorldGuideDataRow"))
	TSoftObjectPtr<UDataTable> WorldGuideTable;
};
