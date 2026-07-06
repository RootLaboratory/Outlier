#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

namespace EMPGameplayTags
{
	namespace Target
	{
		inline FGameplayTag EMPable()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("EMP.Target.EMPable")));
			return Tag;
		}
	}

}
