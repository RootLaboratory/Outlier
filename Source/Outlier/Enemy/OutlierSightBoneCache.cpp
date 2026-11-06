#include "Enemy/OutlierSightBoneCache.h"

#include "Algo/Compare.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"

DEFINE_LOG_CATEGORY_STATIC(LogOutlierSightBones, Log, All);

void FOutlierSightBoneCache::Invalidate()
{
	CachedMesh.Reset();
	CachedBoneNames.Reset();
	BoneIndices.Reset();
	Error.Reset();
	bResolved = false;
}

bool FOutlierSightBoneCache::Resolve(USkeletalMesh* Mesh, TConstArrayView<FName> BoneNames)
{
	// 실패도 캐시하여 같은 잘못된 설정을 반복 검색하거나 경고하지 않는다.
	// 같은 Skeleton을 공유해도 본 인덱스가 다를 수 있으므로 메시 에셋 단위로 비교한다.
	if (bResolved && !CachedMesh.IsStale() && CachedMesh.Get() == Mesh
		&& Algo::Compare(CachedBoneNames, BoneNames))
	{
		return IsValid();
	}

	Invalidate();
	CachedMesh = Mesh;
	CachedBoneNames.Append(BoneNames.GetData(), BoneNames.Num());
	bResolved = true;
	if (!Mesh)
	{
		Error = TEXT("Missing world skeletal mesh");
	}
	else if (BoneNames.IsEmpty())
	{
		Error = TEXT("Empty bone list");
	}
	else
	{
		const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
		BoneIndices.Reserve(BoneNames.Num());
		for (const FName BoneName : BoneNames)
		{
			if (BoneName.IsNone())
			{
				Error = TEXT("Empty bone name");
				break;
			}
			// 소켓 조회 경로를 사용하지 않는다. 목록의 의미는 실제 Bone Name으로 고정한다.
			const int32 BoneIndex = Skeleton.FindBoneIndex(BoneName);
			if (BoneIndex == INDEX_NONE)
			{
				Error = FString::Printf(TEXT("Missing bone: %s"), *BoneName.ToString());
				break;
			}
			if (BoneIndices.Contains(BoneIndex))
			{
				Error = FString::Printf(TEXT("Duplicate bone: %s"), *BoneName.ToString());
				break;
			}
			BoneIndices.Add(BoneIndex);
		}
	}

	if (!Error.IsEmpty())
	{
		// 일부만 남기면 분모가 줄어 발견 기준이 낮아진다. 실패한 목록은 전체를 무효화한다.
		BoneIndices.Reset();
		UE_LOG(LogOutlierSightBones, Warning, TEXT("Invalid sight bone registration Mesh=%s: %s"),
			*GetNameSafe(Mesh), *Error);
		return false;
	}
	return true;
}
