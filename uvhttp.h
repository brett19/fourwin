#pragma once

#include <vector>
#include "http_parser.h"
#include "uvpp.h"

namespace http {
	struct Response {
		uint32_t statusCode;
		std::vector<std::pair<std::string, std::string>> headers;
		std::vector<uint8_t> body;
	};

	class ResponseParser {
	public:
		ResponseParser() {
			http_parser_init(&_parser, HTTP_RESPONSE);
			_parser.data = this;
			_settings.on_message_begin = &_parserCb < &ResponseParser::_onMessageBegin > ;
			_settings.on_url = &_parserCb < &ResponseParser::_onUrl > ;
			_settings.on_status = &_parserCb < &ResponseParser::_onStatus > ;
			_settings.on_header_field = &_parserCb < &ResponseParser::_onHeaderField > ;
			_settings.on_header_value = &_parserCb < &ResponseParser::_onHeaderValue > ;
			_settings.on_headers_complete = &_parserCb < &ResponseParser::_onHeadersComplete > ;
			_settings.on_body = &_parserCb < &ResponseParser::_onBody > ;
			_settings.on_message_complete = &_parserCb < &ResponseParser::_onMessageComplete > ;
		}

		void parse(const char *data, size_t len) {
			http_parser_execute(&_parser, &_settings, data, len);
		}

		void finish() {
			http_parser_execute(&_parser, &_settings, nullptr, 0);
		}

		virtual void onComplete(const Response& response) {
			printf("HttpResponseParser::onComplete\n");
		}

		virtual void onError(uint32_t code) {
			printf("HttpResponseParser::onError(%d)\n", code);
		}

	private:
		enum class HeaderState : uint32_t {
			Name,
			Value
		};

		template<int(ResponseParser::*F)()>
		static int _parserCb(http_parser *parser) {
			return (((ResponseParser*)parser->data)->*F)();
		}
		template<int(ResponseParser::*F)(const char*, size_t)>
		static int _parserCb(http_parser *parser, const char *at, size_t length) {
			return (((ResponseParser*)parser->data)->*F)(at, length);
		}

		int _onMessageBegin() {
			return 0;
		}
		int _onUrl(const char *at, size_t len) {
			return 0;
		}
		int _onStatus(const char *at, size_t len) {
			_response.statusCode = _parser.status_code;
			return 0;
		}
		int _onHeaderField(const char *at, size_t len) {
			if (_headerState == HeaderState::Value) {
				_response.headers.emplace_back(_headerName, _headerValue);
				_headerName.resize(0);
				_headerValue.resize(0);
			}
			_headerName += std::string(at, len);
			_headerState = HeaderState::Name;
			return 0;
		}
		int _onHeaderValue(const char *at, size_t len) {
			_headerValue += std::string(at, len);
			_headerState = HeaderState::Value;
			return 0;
		}
		int _onHeadersComplete() {
			if (_headerState == HeaderState::Value) {
				_response.headers.emplace_back(_headerName, _headerValue);
				_headerName.resize(0);
				_headerValue.resize(0);
			}

			return 0;
		}
		int _onBody(const char *at, size_t len) {
			size_t offset = _response.body.size();
			_response.body.resize(offset + len);
			memcpy(&_response.body[offset], at, len);
			return 0;
		}
		int _onMessageComplete() {
			this->onComplete(_response);
			_response = Response();
			return 0;
		}

		http_parser _parser;
		http_parser_settings _settings;
		Response _response;
		HeaderState _headerState;
		std::string _headerName;
		std::string _headerValue;

	};

	class Socket : public uvpp::TcpSocket {
	public:
		Socket(uvpp::EventLoop& loop)
			: TcpSocket(loop), _parser(*this) {
		}

		void request(const std::string& host, const std::string& path) {
			std::string out;
			out += "GET " + path + " HTTP/1.1\r\n";
			out += "Host: " + host + "\r\n";
			out += "\r\n";
			send(out.c_str(), out.size());
		}

		virtual void onConnect() override {
			printf("HttpSocket::onConnect\n");
		}

		virtual void onClose() override {
			printf("HttpSocket::onClose\n");
			_parser.finish();
		}

		virtual void onResponse(const Response& response) {
			printf("HttpSocket::onResponse()\n");
		}

		virtual void onError(uint32_t code) override {
			printf("HttpSocket::onError(%d)\n", code);
		}

	private:
		virtual void onRecv(const char *data, size_t len) override {
			printf("HttpSocket::onRecv(%p, %d)\n", data, len);
			_parser.parse(data, len);
		}

		class _ResponseParser : public ResponseParser {
		public:
			_ResponseParser(Socket& owner)
				: _owner(owner) {
			}
			void onComplete(const Response& response) override {
				_owner.onResponse(response);
			}
			void onError(uint32_t code) override {
				_owner.onError(code);
			}
		private:
			Socket& _owner;
		};

		std::string _uri;
		_ResponseParser _parser;

	};
}