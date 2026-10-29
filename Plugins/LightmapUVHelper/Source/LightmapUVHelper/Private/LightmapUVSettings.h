#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "LightmapUVSettings.generated.h"

UENUM()
enum class ELightmapUVLOD : uint8
{
	LOD0 UMETA(DisplayName = "LOD 0"),
	All UMETA(DisplayName = "모든 LOD"),
	Specified UMETA(DisplayName = "지정 LOD")
};

/** Only explicitly enabled fields are changed; all other mesh build settings are preserved. */
UCLASS()
class ULightmapUVSettings : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "적용 범위", meta = (DisplayName = "대상 LOD"))
	ELightmapUVLOD LODScope = ELightmapUVLOD::LOD0;

	UPROPERTY(EditAnywhere, Category = "적용 범위", meta = (DisplayName = "LOD 번호", ClampMin = "0", EditCondition = "LODScope == ELightmapUVLOD::Specified", EditConditionHides))
	int32 LODIndex = 0;

	UPROPERTY(EditAnywhere, Category = "UV 생성", meta = (InlineEditConditionToggle))
	bool bApplyGenerate = true;

	UPROPERTY(EditAnywhere, Category = "UV 생성", meta = (DisplayName = "Generate Lightmap UVs", EditCondition = "bApplyGenerate", ToolTip = "왼쪽 체크: 이 항목을 변경합니다. 오른쪽 체크: UV 자동 생성의 목표 값입니다."))
	bool bGenerate = true;

	UPROPERTY(EditAnywhere, Category = "UV 생성", meta = (InlineEditConditionToggle))
	bool bApplySource = false;

	UPROPERTY(EditAnywhere, Category = "UV 생성", meta = (DisplayName = "Source Lightmap Index", ClampMin = "0", ClampMax = "7", EditCondition = "bApplySource", ToolTip = "재배치할 기존 UV 채널. 원본 메시의 채널 수를 검사합니다."))
	int32 SourceIndex = 0;

	UPROPERTY(EditAnywhere, Category = "UV 생성", meta = (InlineEditConditionToggle))
	bool bApplyDestination = false;

	UPROPERTY(EditAnywhere, Category = "UV 생성", meta = (DisplayName = "Destination Lightmap Index", ClampMin = "0", ClampMax = "7", EditCondition = "bApplyDestination", ToolTip = "생성 결과를 저장할 UV 채널. 기존 채널이면 덮어쓰기 경고가 표시됩니다."))
	int32 DestinationIndex = 1;

	UPROPERTY(EditAnywhere, Category = "UV 생성", meta = (InlineEditConditionToggle))
	bool bApplyMinResolution = false;

	UPROPERTY(EditAnywhere, Category = "UV 생성", meta = (DisplayName = "Min Lightmap Resolution", ClampMin = "4", ClampMax = "4096", EditCondition = "bApplyMinResolution", ToolTip = "UV 조각 사이의 여백을 계산하는 기준 해상도. 실제 라이트맵 해상도와 다릅니다."))
	int32 MinResolution = 64;

	UPROPERTY(EditAnywhere, Category = "라이트맵 사용", meta = (InlineEditConditionToggle))
	bool bApplyCoordinate = false;

	UPROPERTY(EditAnywhere, Category = "라이트맵 사용", meta = (DisplayName = "Lightmap Coordinate Index", ClampMin = "0", ClampMax = "7", EditCondition = "bApplyCoordinate", ToolTip = "실제 조명 베이크에서 사용할 UV 채널. 메시 전체에 적용됩니다."))
	int32 CoordinateIndex = 1;

	UPROPERTY(EditAnywhere, Category = "라이트맵 사용", meta = (InlineEditConditionToggle))
	bool bApplyResolution = false;

	UPROPERTY(EditAnywhere, Category = "라이트맵 사용", meta = (DisplayName = "Lightmap Resolution", ClampMin = "4", ClampMax = "4096", EditCondition = "bApplyResolution", ToolTip = "배치된 컴포넌트가 재정의하지 않을 때 사용할 기본 해상도입니다."))
	int32 Resolution = 128;

	UPROPERTY(EditAnywhere, Category = "기존 UV 보호", meta = (DisplayName = "기존 목적 UV 채널 덮어쓰기 허용", ToolTip = "원본에 존재하는 채널에 UV를 생성하면 수작업 UV도 재배치될 수 있습니다. 미리보기에서 확인한 뒤 허용하세요."))
	bool bAllowOverwrite = false;
};
