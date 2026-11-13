#pragma once

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
class APlayerController;

namespace OutlierGenerationTesting
{
	// 검증 프로세스가 지정한 원격 연결의 ACK만 보류한다. 실제 복구 시계/재전송은 유지한다.
	bool ShouldHoldAck(const APlayerController* Controller, bool bUnloadAck);
}
#endif
