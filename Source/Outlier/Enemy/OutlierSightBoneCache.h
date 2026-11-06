#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class USkeletalMesh;

// 대상의 월드 메시와 등록 목록에 종속된 캐시다. 가시성이나 발견 수치는 보관하지 않는다.
struct OUTLIER_API FOutlierSightBoneCache
{
	bool Resolve(USkeletalMesh* Mesh, TConstArrayView<FName> BoneNames);
	void Invalidate();

	bool IsValid() const { return bResolved && CachedMesh.IsValid() && Error.IsEmpty(); }
	TConstArrayView<int32> GetBoneIndices() const { return BoneIndices; }
	const FString& GetError() const { return Error; }

private:
	TWeakObjectPtr<USkeletalMesh> CachedMesh;
	TArray<FName> CachedBoneNames;
	TArray<int32> BoneIndices;
	FString Error;
	bool bResolved = false;
};
