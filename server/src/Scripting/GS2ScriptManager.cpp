#include "GS2ScriptManager.h"

const uint32_t THREADPOOL_WORKERS = 4;

GS2ScriptManager::GS2ScriptManager()
	: _compilerThreadPool(THREADPOOL_WORKERS)
{

}

void GS2ScriptManager::compileScript(const std::string& script, user_callback_type finishedCb)
{
	// Check to see if we already compiled this code before
	auto cacheSearch = _bytecodeCache.find(script);
	if (cacheSearch != _bytecodeCache.end())
	{
		finishedCb(cacheSearch->second);
		return;
	}

	// Disabling any async functionality for now, npcs should be compiled during level-loading
	// and level should not be sent until all the npcs are finished compiling. Can't really
	// enforce this since TLevel is loaded synchronously, but if it does turn into a problem
	// we can migrate to using the threadpool for script compilations and delay sending levels
	// until we finish loading the level. We could also switch to some eager-level-loading method,
	// preloading any levels that are links from other levels or listed in a loaded map etc..

	// Queue a job to compile this script
	// queueCompileJob(script, finishedCb);

	// Synchronously compile script
	syncCompileJob(script, finishedCb);
}

void GS2ScriptManager::syncCompileJob(const std::string& script, user_callback_type& finishedCb)
{
	// Compile code
	auto result = _context.compile(script); // , "weapon", "TestCode", true);

	// Insert into bytecode cache
	auto ret = _bytecodeCache.insert({ script, std::move(result) });

	// Call the user-defined callback after we insert the bytecode into the cache
	finishedCb(ret.first->second);
}

void GS2ScriptManager::queueCompileJob(const std::string& script, user_callback_type& finishedCb)
{
	// Worker job
	auto threadFunction = [script, finishedCb, this](CallbackThreadJob::thread_context &context, auto &promise)
	{
		// Compile code
		auto result = context.gs2context.compile(script); // , "weapon", "TestCode", true);

		// Call the user-defined callback after we insert the bytecode into the cache
		auto completedFunc = [this, script, finishedCb](CompilerResponse &response)
		{
			auto ret = _bytecodeCache.insert({ script, std::move(response) });
			finishedCb(ret.first->second);
		};

		// Create a tuple with the callback, and arguments
		auto fnData = std::make_pair(std::move(completedFunc), std::move(result));

		std::scoped_lock lock(_cbQueueLock);
		_cbQueue.push(std::move(fnData));
	};

	// Queue function into threadpool
	_compilerThreadPool.queue(CallbackThreadJob{ std::move(threadFunction) });
}

void GS2ScriptManager::runQueue()
{
	std::queue<queue_item_type> tmpQueue;

	{
		std::scoped_lock lock(_cbQueueLock);
		if (!_cbQueue.empty())
			tmpQueue.swap(_cbQueue);
	}

	while (!tmpQueue.empty())
	{
		auto& queueItem = tmpQueue.front();

		internal_callback_type& func = queueItem.first;
		CompilerResponse& response = queueItem.second;
		func(response);

		tmpQueue.pop();
	}
}
