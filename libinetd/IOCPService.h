#pragma once
/* -*- mode: c; indent-width: 8; -*- */
/*
 * IOCP Support
 * windows inetd service.
 *
 * Copyright (c) 2020 - 2022, Adam Young.
 * All rights reserved.
 *
 * The applications are free software: you can redistribute it
 * and/or modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 3.
 *
 * Redistributions of source code must retain the above copyright
 * notice, and must be distributed with the license document above.
 *
 * Redistributions in binary form must reproduce the above copyright
 * notice, and must include the license document above in
 * the documentation and/or other materials provided with the
 * distribution.
 *
 * This project is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * license for more details.
 * ==end==
 */

#undef   bind           // sys/socket.h, WIN32
#include <functional>
#include <memory>
#include <cassert>

#include "WindowStd.h"
#include <process.h>    // _beginthread, _endthread

#include "../service/syslog.h"

namespace inetd {
class IOCPService {
	IOCPService(const IOCPService &) = delete;
	IOCPService& operator=(const IOCPService &) = delete;

public:
	static const int MAX_WORKERS = 64;	// linked: wait for multiple event limit.

	typedef std::function<void(bool success)> AcceptCallback;
	typedef std::function<void(unsigned count, bool success)> IOCallback;

	class Listener {
		Listener(const Listener &) = delete;
		Listener& operator=(const Listener &) = delete;

	public:
		Listener() : fd_(0), acceptex_(nullptr), acceptexaddrs_(nullptr) {
		}
		bool is_open() {
			return (fd_ != -1);
		}
		operator SOCKET () {
			return (SOCKET)fd_;
		}
		operator HANDLE () {
			return (HANDLE)fd_;
		}
	private:
		friend class IOCPService;
		int fd_;
		LPFN_ACCEPTEX acceptex_;	// async AcceptEx() implementation.
		LPFN_GETACCEPTEXSOCKADDRS acceptexaddrs_; // async GetAcceptExSockaddrs() implementation.
	};

	class Socket {
		Socket(const Socket &) = delete;
		Socket& operator=(const Socket &) = delete;

	private:
		typedef LONG (NTAPI *NtSetInformationFile_t)(HANDLE, ULONG_PTR*, void*, ULONG, ULONG);

		static NtSetInformationFile_t getNtSetInformationFile()
		{
			static NtSetInformationFile_t volatile *function = nullptr;
			void* t_function = ::InterlockedCompareExchangePointer((void **) &function, 0, 0);
			if (t_function == nullptr) {
				if (HMODULE h = ::GetModuleHandleA("NTDLL.DLL")) {
					t_function = reinterpret_cast<void*>(GetProcAddress(h, "NtSetInformationFile"));
				}
				::InterlockedExchangePointer((void **) &function, t_function ? t_function : (void *)-1);
						// oneshot, set to -1 if unable to resolve.
			}
			return reinterpret_cast<NtSetInformationFile_t>(t_function == (void *)-1 ? nullptr : t_function);
		}

	public:
		enum State { Closed, Accept, Connected, Read, Write };
		struct OVERLAPPEDEX {
			OVERLAPPEDEX(Socket *self) : self_(self) {
				reset();
			}
			void reset() {
				memset(&ovlp_, 0, sizeof(ovlp_));
			}
			operator OVERLAPPED *() {
				return &ovlp_;
			}
			OVERLAPPED ovlp_;	// overlapped control.
			Socket *self_;		// self reference.
		};

	public:
		Socket(int fd = INVALID_SOCKET) : state_(fd >= 0 ? State::Connected : State::Closed),
				fd_(fd), iocp_(INVALID_HANDLE_VALUE), ovlpex_(this)
                {
			(void) memset(&accept_buffer_, 0, sizeof(accept_buffer_));
		}

		~Socket()
		{
			close();
		}

		// Returns a handle to the managed socket if any.
		int fd() const
		{
			return fd_;
		}

		// Releases the ownership of the managed socket if any. fd() returns -1 after the call.
		int release()
		{
			int t_fd = fd_;
			fd_ = INVALID_SOCKET;
			close();
			return t_fd;
		}

		bool getendpoints(struct sockaddr &local, struct sockaddr &remote) const
		{
			assert(Socket::Connected == state_);
			if (Socket::Connected == state_ && acceptexaddrs_) {
				// decode accept result buffer
				SOCKADDR *LocalAddr = NULL, *RemoteAddr = NULL;
				int LocalLen = 0, RemoteLen = 0;

				acceptexaddrs_((void *)accept_buffer_, 0,
					sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
					&LocalAddr, &LocalLen, &RemoteAddr, &RemoteLen);
				if (LocalAddr && RemoteAddr) {
					local = *LocalAddr;
					remote = *RemoteAddr;
					return true;
				}
			}
			return false;
		}

//		bool read(void *buffer, size_t buflen) {
//		}

//		bool write(const void *buffer, size_t buflen) {
//		}

		bool async_read(void *buffer, size_t buflen, IOCallback callback)
		{
			assert (callback);

			if (INVALID_HANDLE_VALUE == iocp_ || INVALID_SOCKET == fd_ ||
					nullptr == buffer || 0 == buflen || Connected != state_) {
				callback(0U, false);
				return false;
			}

			DWORD dwBytes = 0, dwFlags = 0;
			WSABUF wsabuf;

			wsabuf.len = buflen;
			wsabuf.buf = static_cast<char *>(buffer);
			ovlpex_.reset();

			io_callback_ = std::move(callback);
			state_ = Read;

			int ret = ::WSARecv(fd_, &wsabuf, 1, &dwBytes, &dwFlags, ovlpex_, NULL);
			if (0 == ret) {
				callback((unsigned)dwBytes, true);
				io_callback_ = nullptr;
				state_ = Connected;
			} else {
				assert(SOCKET_ERROR == ret);
				if (SOCKET_ERROR != ret || ERROR_IO_PENDING != WSAGetLastError()) {
					callback(0U, false);
					io_callback_ = nullptr;
					state_ = Connected;
					return false;
				}
			}
			return true;
		}

		bool async_write(const void *buffer, size_t buflen, IOCallback callback)
		{
			assert(callback);

			if (INVALID_HANDLE_VALUE == iocp_ || INVALID_SOCKET == fd_ ||
					nullptr == buffer || 0 == buflen || Connected != state_) {
				callback(0U, false);
				return false;
			}

			DWORD dwBytes = 0;
			WSABUF wsabuf;

			wsabuf.len = buflen;
			wsabuf.buf = (char *)buffer;
			ovlpex_.reset();

			io_callback_ = std::move(callback);
			state_ = Write;

			int ret = ::WSASend(fd_, &wsabuf, 1, &dwBytes, 0, ovlpex_, NULL);
			if (0 == ret) {
				callback((unsigned)dwBytes, true);
				io_callback_ = nullptr;
				state_ = Connected;
			} else {
				assert(SOCKET_ERROR == ret);
				if (SOCKET_ERROR != ret || ERROR_IO_PENDING != WSAGetLastError()) {
					callback(0U, false);
					io_callback_ = nullptr;
					state_ = Connected;
					return false;
				}
			}
			return true;
		}

		bool iocp_associate(HANDLE iocp)
		{
			HANDLE handle = reinterpret_cast<HANDLE>(fd_);
			HANDLE t_iocp = ::CreateIoCompletionPort(handle, iocp, 0, 0);
			if (NULL == t_iocp || iocp != t_iocp) {
				WSASyslogx(LOG_ERR, "AssociateIoCompletionPort");
				return false;
			}
			iocp_ = iocp;
			return true;
		}

		bool iocp_release()
		{
			if (INVALID_SOCKET != fd_) {
				NtSetInformationFile_t SetInformationFile = getNtSetInformationFile();
				HANDLE handle = reinterpret_cast<HANDLE>(fd_);
				ULONG_PTR iosb[2] = { 0, 0 };
				void* info[2] = { 0, 0 };

				// disassociated with completion port; if any
				iocp_ = INVALID_HANDLE_VALUE;
				if (SetInformationFile &&
					    0 == SetInformationFile(handle, iosb, &info, sizeof(info), 61 /*FileReplaceCompletionInformation*/)) {
					return true;
				}
			}
			return false;
		}

		void close()
		{
			state_ = Socket::Closed;
			if (INVALID_SOCKET != fd_) {
				::closesocket(fd_);
				fd_ = INVALID_SOCKET;
			}
			accept_callback_ = nullptr;
			io_callback_ = nullptr;
			iocp_ = INVALID_HANDLE_VALUE;
		}

	private:
		friend class IOCPService;
		State state_;			// execution status.
		int fd_;			// active aocket descriptor
		HANDLE iocp_;			// associated io completion port; if any.
		OVERLAPPEDEX ovlpex_;		// extended overlapped interface.
		char accept_buffer_[(sizeof(sockaddr_in6) + 16) * 2];
		LPFN_GETACCEPTEXSOCKADDRS acceptexaddrs_; // async GetAcceptExSockaddrs() implementation.
		AcceptCallback accept_callback_;// accept operation callback.
		IOCallback io_callback_;	// read/write operation callback.
	};

public:
	IOCPService() : numthreads_(0), iocp_global_(INVALID_HANDLE_VALUE)
	{
		for (unsigned i = 0; i < _countof(threads_); ++i) {
			threads_[i] = INVALID_HANDLE_VALUE;
		}
	}

	~IOCPService()
	{
		Terminate();
		Close();
		if (iocp_global_ != INVALID_HANDLE_VALUE) {
			::CloseHandle(iocp_global_);
			iocp_global_ = INVALID_HANDLE_VALUE;
		}
	}

	bool Initialise(int threads)
	{
		if (threads < 1) {
			threads = 1;
		}

		iocp_global_ = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
		if (NULL == iocp_global_) {
			syslog(LOG_ERR, "CreateIoCompletionPort: %M");
			return false;
		}

		for (int i = 0; i < threads; ++i) {
			HANDLE hThread;

			// Win32 API CreateThread() does not initialize the C Runtime.
			hThread = (HANDLE)::_beginthreadex(NULL, 0, Worker, (void *)iocp_global_, 0, NULL);
			if (NULL == hThread) {
				syslog(LOG_ERR, "beginthreadex: %M");
				terminate();
				return false;
			}
			threads_[i] = hThread;
			hThread = INVALID_HANDLE_VALUE;
			++numthreads_;
		}
		return true;
	}

	bool Enabled() const
	{
		return (numthreads_ > 0);
	}

	void Terminate()
	{
		if (numthreads_ > 0) {		// signal workers; if any
			for (int i = 0; i < numthreads_; ++i) {
				::PostQueuedCompletionStatus(iocp_global_, 0, 0, NULL);
			}

			if (WAIT_OBJECT_0 != ::WaitForMultipleObjects(numthreads_, threads_, TRUE, 5*1000 /*5-seconds*/)) {
				syslog(LOG_ERR, "WaitForThreads : %M");
			} else {
				for (unsigned i = 0; i < _countof(threads_); ++i) {
					if (threads_[i] != INVALID_HANDLE_VALUE) {
						::CloseHandle(threads_[i]);
					}
					threads_[i] = INVALID_HANDLE_VALUE;
				}
			}
			numthreads_ = 0;
		}
	}

	void Close()
	{
		if (iocp_global_ != INVALID_HANDLE_VALUE) {
			::CloseHandle(iocp_global_);
			iocp_global_ = INVALID_HANDLE_VALUE;
		}
	}

	bool Listen(Listener &listener, int fd)
	{
		HANDLE iocp;

		if (INVALID_HANDLE_VALUE == (iocp = iocp_global_) ||
				INVALID_SOCKET == fd) {
			return false;		// preconditions.
		}

		if (fd != listener.fd_) {	// associate new listener.
			GUID GUIDAcceptEx = WSAID_ACCEPTEX,
			    GUIDGetSockaddrs = WSAID_GETACCEPTEXSOCKADDRS;
			DWORD dwBytes;

			listener.fd_ = INVALID_SOCKET;
			listener.acceptex_ = 0;
			listener.acceptexaddrs_ = 0;

			// resolve async accept interface.
			dwBytes = 0;
			if (::WSAIoctl((SOCKET) fd, SIO_GET_EXTENSION_FUNCTION_POINTER,
				&GUIDAcceptEx, sizeof(GUIDAcceptEx), &listener.acceptex_, sizeof(listener.acceptex_),
					&dwBytes, NULL, NULL) == SOCKET_ERROR) {
				WSASyslogx(LOG_ERR, "WSAIoct(getacceptex)");
				return false;
			}

			dwBytes = 0;
			if (::WSAIoctl((SOCKET) fd, SIO_GET_EXTENSION_FUNCTION_POINTER,
				&GUIDGetSockaddrs, sizeof(GUIDGetSockaddrs), &listener.acceptexaddrs_, sizeof(listener.acceptexaddrs_),
					&dwBytes, NULL, NULL) == SOCKET_ERROR) {
				WSASyslogx(LOG_ERR, "WSAIoct(getacceptexsockaddrs)");
				return false;
			}

			// associate the listener.
			HANDLE t_iocp = ::CreateIoCompletionPort((HANDLE) fd, iocp,
						reinterpret_cast<LONG_PTR>(&listener), numthreads_);
			if (NULL == t_iocp || iocp != t_iocp) {
				WSASyslogx(LOG_ERR, "AssociateIoCompletionPort");
				return false;
			}

			listener.fd_ = fd;	// bound
		}
		return true;
	}

	bool Cancel(Listener &listener)
	{
		if (listener.fd_ != INVALID_SOCKET) {
			(void) ::CancelIoEx(listener, NULL);
			return true;
		}
		return false;
	}

	bool Shutdown(Listener &listener)
	{
		if (listener.fd_ != INVALID_SOCKET) {
			(void) ::CancelIoEx(listener, NULL);
			listener.fd_ = -1;
			return true;
		}
		return false;
	}

	bool Accept(Listener &listener, Socket &cxt, AcceptCallback callback)
	{
		HANDLE iocp;
		DWORD dwBytes;

		// pre-conditions.
		assert(listener.is_open());
		assert(callback);
		if (! listener.is_open() || ! callback) {
			return false;
		}

		assert(Socket::Closed == cxt.state_);
		if (INVALID_HANDLE_VALUE == (iocp = iocp_global_) ||
				Socket::Closed != cxt.state_ ) {
			return false;
		}

		// create an accepting socket.
		cxt.fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (INVALID_SOCKET == cxt.fd_) {
			WSASyslogx(LOG_ERR, "Accept socket");
			return false;
		}

		// associate completion point.
		cxt.state_ = Socket::Accept;
		cxt.accept_callback_ = std::move(callback);
		cxt.acceptexaddrs_ = listener.acceptexaddrs_;

		// create accept request.
	    retry:;
		dwBytes = 0;
		cxt.ovlpex_.reset();
		(void) memset(&cxt.accept_buffer_, 0, sizeof(cxt.accept_buffer_));
		if (FALSE == listener.acceptex_(listener, cxt.fd(), cxt.accept_buffer_,
				0 /*dont wait for data*/, sizeof(SOCKADDR_IN6) + 16, sizeof(SOCKADDR_IN6) + 16,
				    &dwBytes, cxt.ovlpex_)) {

			// async completion.

			const DWORD err = WSAGetLastError();
			if (ERROR_IO_PENDING != err) {
				WSASyslogx(LOG_ERR, "AcceptEx");
				if (WSAECONNRESET == err) {
					// an incoming connection was indicated,
					// but was subsequently terminated by the remote peer prior to accepting the call.
					goto retry;
				}
				cxt.close();
				return false;
			}

		} else {
			// sync completion, post completion

			if (FALSE == ::PostQueuedCompletionStatus(iocp, 0 /*bytes*/,
						reinterpret_cast<LONG_PTR>(&listener), cxt.ovlpex_)) {
				WSASyslogx(LOG_ERR, "PostQueuedCompletionStatus");
				cxt.close();
				return false;
			}
		}
		return true;
	}

private:
	static unsigned __stdcall Worker(void *void_context)
	{
		HANDLE iocp = (HANDLE)void_context;

		//
		//  Service completion port events.
		for (;;) {
			Socket::OVERLAPPEDEX *ovlpex = NULL;
			void *key = nullptr;
			DWORD dwIoSize = 0;
			BOOL bSuccess;

			bSuccess = ::GetQueuedCompletionStatus(iocp,
				&dwIoSize, (PDWORD_PTR)&key, (OVERLAPPED **)&ovlpex, INFINITE);
			if (! bSuccess) {
				WSASyslogx(LOG_ERR, "getqueuedcompletionstatus");
			}

			if ((void *)-1 == key) {
				break;		// termination event, see Terminate().
			}

			Socket *cxt = ovlpex->self_;
			switch (cxt->state_) {
			case Socket::Accept: {
					AcceptCallback callback(std::move(cxt->accept_callback_));
					Listener *listener = (Listener *)key;

					assert(0 == dwIoSize);
					cxt->state_ = Socket::Connected;
					if (bSuccess) {
						// Update socket with the context of the listening socket,
						// allowing getsockname() and getpeername() to function.
						int t_listenerfd = listener->fd_;
						if (setsockopt(cxt->fd(), SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
								(char *)&t_listenerfd, sizeof(t_listenerfd))) {
							WSASyslogx(LOG_ERR, "setsockopt(SO_UPDATE_ACCEPT_CONTEXT)");
							bSuccess = FALSE;
						}
					} else {
						::CancelIoEx(listener, *ovlpex);
					}
					callback(bSuccess == TRUE);
				}
				break;
			case Socket::Read: {
					IOCallback callback(std::move(cxt->io_callback_));

					cxt->state_ = Socket::Connected;
					callback(dwIoSize, bSuccess == TRUE);
				}
				break;
			case Socket::Write: {
					IOCallback callback(std::move(cxt->io_callback_));

					cxt->state_ = Socket::Connected;
					callback(dwIoSize, bSuccess == TRUE);
				}
				break;
			default:
				assert(false);
				break;
			}
		}
		_endthreadex(0);		// wont close the thread handle.
		return 0;
	}

private:
	int numthreads_;
	HANDLE iocp_global_;
	HANDLE threads_[MAX_WORKERS];
};

}; //namespace inetd

//end
