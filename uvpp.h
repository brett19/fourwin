#pragma once
#include <string>
#include <functional>
#include <uv.h>

namespace uvpp {
	namespace internal {
		struct uvpp_write_t {
			uv_write_t req;
			uv_buf_t buf;
		};

		char * alloc(size_t size) {
			return new char[size];
		}

		void dealloc(char *ptr) {
			delete[] ptr;
		}

		void uvAllocCb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
			buf->base = alloc(suggested_size);
			buf->len = suggested_size;
		}
	}
	namespace i = uvpp::internal;

	class Mutex {
	public:
		Mutex() {
			uv_mutex_init(&_mutex);
		}

		void lock() {
			uv_mutex_lock(&_mutex);
		}

		void unlock() {
			uv_mutex_unlock(&_mutex);
		}

	private:
		uv_mutex_t _mutex;

	};

	class ScopedLock {
	public:
		ScopedLock(Mutex& mutex)
			: _mutex(mutex) {
			_mutex.lock();
		}

		~ScopedLock() {
			_mutex.unlock();
		}

	private:
		Mutex& _mutex;

	};

	class Thread {
	public:
		Thread() {
		}

		void start() {
			uv_thread_create(&_thread, _uvThreadExec, this);
		}

		void join() {
			uv_thread_join(&_thread);
		}

		virtual void threadExec() {
			printf("Thread::threadExec\n");
		}

	private:
		static void _uvThreadExec(void *self) {
			((Thread*)self)->threadExec();
		}

		uv_thread_t _thread;

	};

	class EventLoop {
		friend class TcpSocket; 
		friend class Event;

	public:
		EventLoop() {
			uv_loop_init(&_loop);
		}

		~EventLoop() {
			uv_loop_close(&_loop);
		}

		void run() {
			uv_run(&_loop, UV_RUN_DEFAULT);
		}

		void runOnce() {
			uv_run(&_loop, UV_RUN_ONCE);
		}

	protected:
		uv_loop_t _loop;

	};

	class Event {
	public:
		Event(EventLoop& loop) {
			uv_async_init(&loop._loop, &_async, &_uvOnSignal);
			_async.data = this;
		}

		void signal() {
			uv_async_send(&_async);
		}

		virtual void onSignal() {
			printf("Event::onSignal\n");
		}

	private:
		static void _uvOnSignal(uv_async_t* handle) {
			((Event*)handle->data)->_onSignal(handle);
		}

		void _onSignal(uv_async_t* handle) {
			this->onSignal();
		}

		uv_async_t _async;

	};

	class TcpSocket {
	public:
		TcpSocket(EventLoop& loop) {
			uv_tcp_init(&loop._loop, &_socket);
			_socket.data = this;
		}

		~TcpSocket() {
		}

		bool connect(const std::string& host, uint16_t port) {
			uv_connect_t *conn = new uv_connect_t;
			conn->data = this;

			struct sockaddr_in bindAddr;
			uv_ip4_addr(host.c_str(), port, &bindAddr);

			uv_tcp_connect(conn, &_socket, reinterpret_cast<const sockaddr*>(&bindAddr), _uvOnConnect);
			return true;
		}

		void send(const char *data, size_t len) {
			char * newmem = internal::alloc(len);
			memcpy(newmem, data, len);

			i::uvpp_write_t *wreq = new i::uvpp_write_t;
			wreq->req.data = this;
			wreq->buf.base = newmem;
			wreq->buf.len = len;

			uv_write(&wreq->req, reinterpret_cast<uv_stream_t*>(&_socket), &wreq->buf, 1, _uvOnWrite);
		}

		virtual void onConnect() {
			printf("TcpSocket::onConnect\n");
		}
		virtual void onClose() {
			printf("TcpSocket::onClose\n");
		}
		virtual void onRecv(const char *data, size_t len) {
			printf("TcpSocket::onRecv(%p, %d)\n", data, len);
		}
		virtual void onError(uint32_t code) {
			printf("TcpSocket::onError(%d)\n", code);
		}

	private:
		static void _uvOnConnect(uv_connect_t *req, int status) {
			((TcpSocket*)req->data)->_onConnect(req, status);
		}
		static void _uvOnWrite(uv_write_t *req, int status) {
			((TcpSocket*)req->data)->_onWrite(req, status);
		}
		static void _uvOnRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
			((TcpSocket*)stream->data)->_onRead(stream, nread, buf);
		}

		void _onConnect(uv_connect_t *req, int status) {
			if (status < 0) {
				return this->onError(status);
			}
			this->onConnect();
			delete req;

			uv_read_start(reinterpret_cast<uv_stream_t*>(&_socket), i::uvAllocCb, _uvOnRead);
		}

		void _onWrite(uv_write_t *req, int status) {
			i::uvpp_write_t *wreq = reinterpret_cast<i::uvpp_write_t*>(req);
			if (status < 0) {
				this->onError(status);
			}
			i::dealloc(wreq->buf.base);
			delete wreq;
		}

		void _onRead(uv_stream_t *stream, ssize_t nread, const uv_buf_t* buf) {
			if (nread < 0) {
				return this->onError(nread);
			} else if (nread == 0) {
				return this->onClose();
			}
			this->onRecv(buf->base, nread);
			i::dealloc(buf->base);
		}


		uv_tcp_t _socket;

	};
}
