#pragma once

namespace MyUtils {
	class TLTask {
	public:
		TLTask() = default;
		virtual ~TLTask() = default;

		virtual void Process() = 0;

		//이 객체가 처리된 후 정리작업을 잊지 않고 수행하겠다는 것을 알리는 함수
		//이 객체는 Pool에서 관리되므로 반드시 구현해야 함
		virtual void ClearAfterProcess() = 0;
	};
}
