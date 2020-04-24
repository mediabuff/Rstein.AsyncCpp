#pragma once
#include "../../AsyncPrimitives/OperationCanceledException.h"
#include "../../Collections/ThreadSafeMinimalisticVector.h"
#include "../../Schedulers/Scheduler.h"
#include "../../Utils/FinallyBlock.h"
#include "../TaskState.h"
#include "../../Functional/F.h"

#include <any>
#include <cassert>
#include <vector>
#include <mutex>
#include <condition_variable>

namespace RStein::AsyncCpp::Tasks::Detail
{


  template <typename TResult>
  struct TaskSharedState : public std::enable_shared_from_this<TaskSharedState<TResult>>
  {
  public:
    using ContinuationFunc = std::function<void()>;
    using Function_Ret_Type = TResult;

    TaskSharedState(std::function<TResult()> func, bool isTaskReturnFunc, AsyncPrimitives::CancellationToken::CancellationTokenPtr cancellationToken) :
      std::enable_shared_from_this<TaskSharedState<TResult>>(),
      _func(std::move(func)),
      _isTaskReturnFunc(isTaskReturnFunc),
      _cancellationToken(cancellationToken),
      _lockObject{},
      _waitTaskCv{},
      _scheduler{ Schedulers::Scheduler::DefaultScheduler() },
      _taskId{ _idGenerator++ },
      _state{ TaskState::Created },
      _continuations{ std::vector<ContinuationFunc>{} },
      _exceptionPtr{ nullptr }
    {
      if constexpr(!std::is_same<TResult, void>::value)
      {
        if (_func != nullptr)
        {
          _func = Functional::Memoize0(std::move(_func));
        }
      }
    }

    TaskSharedState() : TaskSharedState(nullptr, false, AsyncPrimitives::CancellationToken::CancellationToken::None())
    {

    }

    TaskSharedState(const TaskSharedState& other) = delete;

    TaskSharedState(TaskSharedState&& other) noexcept = delete;

    TaskSharedState& operator=(const TaskSharedState& other) = delete;

    TaskSharedState& operator=(TaskSharedState&& other) noexcept = delete;

    unsigned long Id() const
    {
      return _taskId;
    }

    TaskState State() const
    {
      std::lock_guard lock{ _lockObject };
      return _state;
    }

    bool HasException() const
    {
      return _exceptionPtr != nullptr;
    }

    std::exception_ptr Exception() const
    {
      return _exceptionPtr;
    }

    bool IsTaskBasedReturnFunc() const
    {
      return _isTaskReturnFunc;
    }

    AsyncPrimitives::CancellationToken::CancellationTokenPtr CancellationToken() const
    {
      return _cancellationToken;
    }
    template <typename TResultCopy = TResult>
    typename std::enable_if<!std::is_same<TResultCopy, void>::value, TResult>::type
    GetResult() const
    {
      Wait();
      return _func();
    }

    bool IsCtCanceled() const
    {
      return _cancellationToken->IsCancellationRequested();
    }


    void RunTaskFunc()
    {
      assert(_func != nullptr);
      auto isCtCanceled = IsCtCanceled();
      if (isCtCanceled)
      {
        {
          std::lock_guard lock{ _lockObject };
          _state = TaskState::Canceled;
        }

        _waitTaskCv.notify_all();
        runContinuations();

        return;
      }

      {
        std::lock_guard lock{ _lockObject };

        if (_state != TaskState::Created)
        {
          throw std::logic_error("Task already started.");
        }


        _state = TaskState::Scheduled;
      }

      _scheduler->EnqueueItem([this, sharedThis = this->shared_from_this()]
        {
          Utils::FinallyBlock finally
          {

              [this]
              {
                _waitTaskCv.notify_all();
                runContinuations();
              }
          };
          try
          {
            CancellationToken()->ThrowIfCancellationRequested();

            {
              std::lock_guard lock{_lockObject};
              assert(_state == TaskState::Scheduled);
              _state = TaskState::Running;
            }

            DoRunTaskNow();
            _state = TaskState::RunToCompletion;
          }
          catch (const AsyncPrimitives::OperationCanceledException&)
          {
            _exceptionPtr = std::current_exception();
            std::lock_guard lock{_lockObject};
            assert(_state == TaskState::Scheduled || _state == TaskState::Running);
            _state = TaskState::Canceled;
          }
          catch (...)
          {
            _exceptionPtr = std::current_exception();
            std::lock_guard lock{_lockObject};
            assert(_state == TaskState::Running);
            _state = TaskState::Faulted;
          }
        });
    }

    void Wait() const
    {
      std::unique_lock lock{ _lockObject };
      while (_state != TaskState::RunToCompletion &&
        _state != TaskState::Faulted &&
        _state != TaskState::Canceled)
      {
        _waitTaskCv.wait(lock);
      }

      if (_state != TaskState::RunToCompletion)
      {
        auto exceptionPtr = Exception();
        if (exceptionPtr == nullptr && _state == TaskState::Canceled)
        {
          exceptionPtr = make_exception_ptr(AsyncPrimitives::OperationCanceledException());
        }

        rethrow_exception(exceptionPtr);
      }
    }

    void AddContinuation(ContinuationFunc&& continuationFunc)
    {
      //TODO inline task if possible
      std::lock_guard lock{ _lockObject };
      if (_state == TaskState::RunToCompletion ||
        _state == TaskState::Faulted ||
        _state == TaskState::Canceled)
      {
        continuationFunc();
        return;
      }

      _continuations.Add(continuationFunc);
    }

    void SetException(std::exception_ptr exception)
    {
      {
        std::lock_guard lock{ _lockObject };
        throwIfTaskCompleted();        
        _exceptionPtr = exception;
        _state = TaskState::Faulted;
      }
      _waitTaskCv.notify_all();
      runContinuations();
    }

    bool TrySetException(std::exception_ptr exception)
    {
      {
        std::lock_guard lock{ _lockObject };
        if (_state != TaskState::Created)
        {
          return false;
        }
        _exceptionPtr = exception;
        _state = TaskState::Faulted;
      }
      _waitTaskCv.notify_all();
      runContinuations();
      return true;
    }


    void SetCanceled()
    {
      {
        std::lock_guard lock{ _lockObject };
        throwIfTaskCompleted();
        _state = TaskState::Canceled;
      }

      _waitTaskCv.notify_all();
      runContinuations();
    }

    bool TrySetCanceled()
    {
      {
        std::lock_guard lock{ _lockObject };
        if (_state != TaskState::Created)
        {
          return false;
        }

        _state = TaskState::Canceled;
      }

      _waitTaskCv.notify_all();
      runContinuations();
      return true;
    }

    template <typename TUResult, typename TResultCopy = TResult>    
    void SetResult(typename std::enable_if<!std::is_same<TResultCopy, void>::value, TUResult>::type&& result)
    {
      {
        std::lock_guard lock{ _lockObject };
        throwIfTaskCompleted();
        _func = [result = std::forward<TUResult>(result)]{ return result; };
        _state = TaskState::RunToCompletion;
      }

      _waitTaskCv.notify_all();
      runContinuations();
    }

    
    template <typename TUResult, typename TResultCopy = TResult>
    bool TrySetResult(typename std::enable_if<!std::is_same<TResultCopy, void>::value, TUResult>::type&& result)
    {
      {
        std::lock_guard lock{ _lockObject };
        if (_state != TaskState::Created)
        {
          return false;
        }
        _func = [result = std::forward<TUResult>(result)]{ return result; };
        _state = TaskState::RunToCompletion;
      }

      _waitTaskCv.notify_all();
      runContinuations();
      return true;
    }

    
    template <typename TResultCopy = TResult>
    typename std::enable_if<std::is_same<TResultCopy, void>::value, void>::type
    SetResult()
    {
      {
        std::lock_guard lock{ _lockObject };
        throwIfTaskCompleted();
        _state = TaskState::RunToCompletion;
      }

      _waitTaskCv.notify_all();
      runContinuations();
    }

    template <typename TResultCopy = TResult>
    typename std::enable_if<!std::is_same<TResultCopy, void>::value, bool>::type
    TrySetResult()
    {
      {
        std::lock_guard lock{ _lockObject };
        if (_state != TaskState::Created)
        {
          return false;
        }

        _state = TaskState::RunToCompletion;
      }

      _waitTaskCv.notify_all();
      runContinuations();
      return true;
    }

    ~TaskSharedState()
    {
      std::lock_guard lock{ _lockObject };
      if (_state != TaskState::RunToCompletion &&
        _state != TaskState::Faulted &&
        _state != TaskState::Canceled)
      {
        assert(false);
      }
    }


  private:
    std::function<TResult()> _func;
    bool _isTaskReturnFunc;
    AsyncPrimitives::CancellationToken::CancellationTokenPtr _cancellationToken;
    mutable std::mutex _lockObject;
    mutable std::condition_variable _waitTaskCv;
    Schedulers::Scheduler::SchedulerPtr _scheduler;
    static std::atomic<unsigned long> _idGenerator;
    unsigned long _taskId;
    TaskState _state;
    Collections::ThreadSafeMinimalisticVector<ContinuationFunc> _continuations;
    std::exception_ptr _exceptionPtr;

    void DoRunTaskNow()
    {
      _func();
    }

    void runContinuations()
    {
      std::vector<ContinuationFunc> continuations;
      {
        std::lock_guard lock{ _lockObject };
        assert(_state == TaskState::RunToCompletion ||
          _state == TaskState::Faulted ||
          _state == TaskState::Canceled);

        continuations = _continuations.GetSnapshot();
        _continuations.Clear();
      }

      for (auto& continuation : continuations)
      {
        continuation();
      }
    }

    void throwIfTaskCompleted() const
    {
      if (_state != TaskState::Created)
      {
        throw std::logic_error("Task already completed.");
      }
    }
  };

  template <typename TResult>
  std::atomic<unsigned long> TaskSharedState<TResult>::_idGenerator{};
  
}
