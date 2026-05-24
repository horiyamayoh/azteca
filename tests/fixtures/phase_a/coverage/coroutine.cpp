// Phase A coverage gap-filler fixture: coroutine body must emit
// the coroutine not_yet_implemented diagnostic without crashing.
//
// Phase A does not lower coroutines; this file exists so the
// frontend's VisitCoroutineBodyStmt path is exercised end-to-end
// against `azteca inspect --format json` from the coverage test.

#include <coroutine>

struct CoroTask
{
	struct promise_type
	{
		CoroTask get_return_object()
		{
			return {};
		}
		std::suspend_never initial_suspend() noexcept
		{
			return {};
		}
		std::suspend_never final_suspend() noexcept
		{
			return {};
		}
		void return_void() {}
		void unhandled_exception() {}
	};
};

class CoroHost
{
   public:
	CoroTask run(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			co_await std::suspend_always{};
		}
	}

   private:
	int counter_ = 0;
};
