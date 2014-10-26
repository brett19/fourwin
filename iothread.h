#pragma once

#include "uvpp.h"
#include "uvhttp.h"

namespace iothread {
	class WorkerRequest {
	public:
		virtual void execute() = 0;
		virtual void onComplete() = 0;

	};

	class _WorkerThread : public uvpp::Thread {
		friend class WorkerRequest;

	public:
		_WorkerThread()
			: _signalEvent(*this, _eventLoop) {
		}

		void dispatch(WorkerRequest *request) {
				{
					uvpp::ScopedLock lock(_inMutex);
					_requestsIn.push_back(request);
				}
			_signalEvent.signal();
		}

		void poll() {
			std::vector<WorkerRequest*> requests;
			{
				uvpp::ScopedLock lock(_outMutex);
				requests = std::move(_requestsOut);
			}

			for (auto& i : requests) {
				i->onComplete();
			}
		}

		uvpp::EventLoop& loop() {
			return _eventLoop;
		}

		void _dispatchCompletion(WorkerRequest *request) {
			uvpp::ScopedLock lock(_outMutex);
			_requestsOut.push_back(request);
		}

	private:
		void threadExec() override {
			_eventLoop.run();
		}

		void _grabRequests() {
			std::vector<WorkerRequest*> requests;
			{
				uvpp::ScopedLock lock(_inMutex);
				requests = std::move(_requestsIn);
			}
			for (auto& i : requests) {
				i->execute();
			}
		}

		class _NewRequestEvent : public uvpp::Event {
		public:
			_NewRequestEvent(_WorkerThread& owner, uvpp::EventLoop& loop)
				: uvpp::Event(loop), _owner(owner) {
			}
			virtual void onSignal() {
				_owner._grabRequests();
			}
		private:
			_WorkerThread& _owner;
		};

		uvpp::EventLoop _eventLoop;
		uvpp::Mutex _inMutex;
		uvpp::Mutex _outMutex;
		_NewRequestEvent _signalEvent;
		std::vector<class WorkerRequest*> _requestsIn;
		std::vector<class WorkerRequest*> _requestsOut;

	};
	_WorkerThread *_thread = nullptr;

	class KillRequest : public WorkerRequest {
	private:
		void execute() {
			_thread->loop().stop();
		}
		void onComplete() { }
	};

	void Init() {
		_thread = new _WorkerThread();
		_thread->start();
	}

	void Shutdown() {
		if (_thread) {
			_thread->dispatch(new KillRequest());
			_thread->join();
			delete _thread;
			_thread = nullptr;
		}
	}

	void dispatch(WorkerRequest *request) {
		_thread->dispatch(request);
	}

	void poll() {
		_thread->poll();
	}

	class UriRequest : public WorkerRequest {
	public:
		UriRequest(const std::string& uri)
			: _proc(nullptr) {
		}

	protected:
		int32_t _errorCode;
		http::Response _response;

	private:
		class HttpProc : http::Socket {
		public:
			HttpProc(UriRequest& owner, const std::string& host, uint16_t port, const std::string& path)
				: http::Socket(_thread->loop()), _owner(owner), _host(host), _port(port), _path(path) {
			}
			void start() {
				connect(_host, _port);
			}
			virtual void onConnect() override {
				request(_host, _path);
			}
			virtual void onResponse(const http::Response& response) {
				_owner.onComplete(response);
			}
			virtual void onError(uint32_t code) override {
				_owner.onError(code);
			}
		private:
			UriRequest& _owner;
			std::string _host;
			uint16_t _port;
			std::string _path;
		};
		HttpProc *_proc;

		void execute() override {
			printf("HttpProc execute\n");
			_proc = new HttpProc(*this, "127.0.0.1", 80, "/");
			_proc->start();
		}

		virtual void onComplete(const http::Response& response) {
			_errorCode = 0;
			_response = response;
			_thread->_dispatchCompletion(this);
		}

		virtual void onError(int32_t code) {
			_errorCode = code;
			_thread->_dispatchCompletion(this);
		}

	};
}