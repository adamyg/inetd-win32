#pragma once
/* -*- mode: c; indent-width: 8; -*- */
/*
 * Process group management
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

#include <cstdio>
#include <cassert>
#include <memory>
#include <unordered_map>
#include <set>

#include "WindowStd.h"
#include "ScopedProcessId.h"
#include "SimpleLock.h"

namespace inetd {

class ProcessGroup 
{
	ProcessGroup(const ProcessGroup &) = delete;
	ProcessGroup& operator=(const ProcessGroup &) = delete;

private:
	struct Process 
	{
		Process(const Process &) = delete;
		Process& operator=(const Process &) = delete;

		Process() : exitcode_(0), attempts(0) { }
		HANDLE take_process_handle() {
			return pid_.take_process_handle();
		}
		HANDLE process_handle() const {
			return pid_.process_handle();
		}
		DWORD process_id() const {
			return pid_.process_id();
		}
		ScopedProcessId pid_;
		DWORD exitcode_;
		unsigned attempts;
	};

private:
	enum {
		THREAD_CTRL_TRACK = 1,
		THREAD_CTRL_QUIT,
		THREAD_CTRL_LAST
	};

public:
	ProcessGroup() : unmanaged_(0)
	{
	}

	~ProcessGroup()
	{
		close();
	}

	bool open(void (*sigchld)() = nullptr, int signal_event = -1)
	{
		sigchld_ = sigchld;

		if (! job_.IsValid()) {
			SECURITY_ATTRIBUTES attr;
			memset(&attr, 0, sizeof attr);
			job_.Set(::CreateJobObjectA(&attr, NULL));

			JOBOBJECT_EXTENDED_LIMIT_INFORMATION info;
			memset(&info, 0, sizeof info);
			info.BasicLimitInformation.LimitFlags =
				  JOB_OBJECT_LIMIT_BREAKAWAY_OK 		// Children aren't associated with the job.
				| JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK		// Allows any process associated with the job to create child processes that are not associated with the job.
				| JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION	// Enable debugger dialog.
			      /*| JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE*/;		// Causes all processes associated with the job to terminate when the last handle to the job is closed.

			if (! ::SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &info, sizeof info)) {
				return false;
			}
		}

		if (! port_.IsValid()) {
			port_.Set(::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0));

			JOBOBJECT_ASSOCIATE_COMPLETION_PORT info = {(void *)this, port_.Get()};
			if (! port_.IsValid() ||
				    ! ::SetInformationJobObject(job_, JobObjectAssociateCompletionPortInformation, &info, sizeof(info))) {
				return false;
			}
		}

		if (/*auto*/ -1 == signal_event && sigchld)
			signal_event = 0;
		if (signal_event) {
			if (! waitevent_.IsValid()) { // optional
				waitevent_.Set(::CreateEventA(NULL,
				    (2 == signal_event ? TRUE /*auto*/: FALSE /*manual*/), FALSE /*off*/, NULL));
			}
		}

		if (! thread_.IsValid()) {
			thread_.Set(::CreateThread(nullptr, 0, ProcessGroup::JobEventTask, this, 0, nullptr));
		}

		return true;
	}

	bool close()
	{
		if (port_.IsValid()) {
			::PostQueuedCompletionStatus(port_.Get(), 0, THREAD_CTRL_QUIT, nullptr);
			if (thread_.IsValid() &&
			    WAIT_TIMEOUT == ::WaitForSingleObject(thread_.Get(), 1000)) {
				 return false;
			}
			thread_.Close();
			port_.Close();
		}
		return true;
	}

	bool track(const ScopedProcessId &processid)
	{
		if (port_.IsValid()) {
			Process *process = new Process;

			process->pid_.Clone(processid);
			::PostQueuedCompletionStatus(port_.Get(), 0, THREAD_CTRL_TRACK, reinterpret_cast<LPOVERLAPPED>(process));
				// JobEventTask assumes onwership
			return true;
		}
		return false;
	}

	HANDLE job_handle()
	{
		return job_.Get();
	}

public:
//	int
//	waitpid(pid_t pid, int *status, int options)
//	{
//		return wait_common(pid, status, options, NULL, NULL);
//	}

//	int
//	wait(int *status)
//	{
//		if (status) {
//			return wait(false, *status);
//		}
//		int t_status = 0;
//		return wait(false, t_status);
//	}

//	pid_t
//	wait3(int * status, int options, struct rusage *rusage)
//	{
//		return wait_common(-1, status, options, NULL, rusage);
//	}

//	pid_t
//	wait4(pid_t pid, int * status, int options, struct rusage *rusage)
//	{
//		return wait_common(pid, status, options, NULL, rusage);
//	}

	int wait(bool nohang, int &status)
	{
		while (true) {
			std::unique_ptr<Process> process;

			{	inetd::CriticalSection::Guard guard(completelock_);
				if (! complete_.empty()) {
					process.swap(complete_.front());
					complete_.pop_front();
				}
			}

			if (process) {
				if (wait_handle(process->process_handle(), nohang, status)) {
					return process->process_id();
				}

				if (errno == EAGAIN && ++process->attempts < 3) {
					// XXX: incomplete termination
					inetd::CriticalSection::Guard guard(completelock_);
					complete_.push_back(std::move(process));
					sigchld(); //retrigger
				}

				return -1;
			}

			if (nohang) {
				errno = ECHILD;
				break;
			}

			if (WAIT_OBJECT_0 == ::WaitForSingleObject(waitevent_.Get(), INFINITE)) {
				errno = EINVAL;
				break;
			}
		}
		return -1;
	}

private:
	static DWORD WINAPI JobEventTask(PVOID param)
	{
		std::unordered_map<DWORD, std::unique_ptr<Process>> processes;
		std::set<DWORD> childids;

		ProcessGroup* self = reinterpret_cast<ProcessGroup*>(param);
		assert(self);

		for (HANDLE port = self->port_.Get(); self;) {
			DWORD events = 0;
			ULONG_PTR key = 0;
			LPOVERLAPPED ovl = nullptr;

			if (! ::GetQueuedCompletionStatus(port, &events, &key, &ovl, INFINITE)) {
				return 1;
			}

			if (key > THREAD_CTRL_LAST) {
				switch (events) {
				case JOB_OBJECT_MSG_NEW_PROCESS: {
						const DWORD process_id = static_cast<DWORD>(reinterpret_cast<uintptr_t>(ovl));
						childids.insert(process_id);
					}
					break;
				case JOB_OBJECT_MSG_EXIT_PROCESS:
				case JOB_OBJECT_MSG_ABNORMAL_EXIT_PROCESS: {
						const DWORD process_id = static_cast<DWORD>(reinterpret_cast<uintptr_t>(ovl));
						bool gensignal = true;

						auto it = processes.find(process_id);
						if (it != processes.end()) {
							assert(::GetExitCodeProcess(it->second->pid_.process_handle(), &it->second->exitcode_));
							{	inetd::CriticalSection::Guard guard(self->completelock_);
								self->complete_.push_back(std::move(it->second));
							}
							processes.erase(it);
							gensignal = true;
						} else {
							++self->unmanaged_;
						}
						childids.erase(process_id);
						if (gensignal) self->sigchld();
					}
					break;
				case JOB_OBJECT_MSG_END_OF_JOB_TIME:
				case JOB_OBJECT_MSG_END_OF_PROCESS_TIME:
				case JOB_OBJECT_MSG_ACTIVE_PROCESS_LIMIT:
				case JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO:
					break;
				case JOB_OBJECT_MSG_PROCESS_MEMORY_LIMIT:
				case JOB_OBJECT_MSG_JOB_MEMORY_LIMIT:
				case JOB_OBJECT_MSG_NOTIFICATION_LIMIT:
				case JOB_OBJECT_MSG_JOB_CYCLE_TIME_LIMIT:
				default:
					break;
				}
			} else {
				switch (key) {
				case THREAD_CTRL_TRACK: {
						std::unique_ptr<Process> process;
						process.reset(reinterpret_cast<Process *>(ovl));
						DWORD process_id = process->process_id();

						if (childids.find(process_id) != childids.end()) {
							bool is_unique = processes.insert(std::make_pair(process->process_id(), std::move(process))).second;
							assert(is_unique);
						} else { // assume process has already terminated
							assert(::GetExitCodeProcess(process->pid_.process_handle(), &process->exitcode_));
							{	inetd::CriticalSection::Guard guard(self->completelock_);
								self->complete_.push_back(std::move(process));
							}
							self->sigchld();
						}
					}
					break;
				case THREAD_CTRL_QUIT: {
					    self = nullptr;
					}
					break;
				}
			}
		}
		return 0;
	}

	void sigchld()
	{
		// XXX: consider using a timer to trigger, allowing complete process termination.
		if (sigchld_) { 		// optional
			sigchld_();
		}

		if (waitevent_.IsValid()) {	// optional
			::SetEvent(waitevent_.Get());
		}
	}

	static bool wait_handle(HANDLE handle, bool nohang, int &status)
	{
		DWORD dwStatus = 0, rc;

		if (0 == handle) {
			errno = EINVAL;		// nul handle

		} else if (handle == (HANDLE)-1 || handle == (HANDLE)-2) {
			errno = ECHILD;		// special handle, ignore

		} else if ((rc = ::WaitForSingleObject(handle, (nohang ? 0 : INFINITE))) == WAIT_OBJECT_0 &&
					::GetExitCodeProcess(handle, (LPDWORD)&dwStatus)) {
			/*
			 *  Normal termination:     lo-byte = 0,            hi-byte = child exit code.
			 *  Abnormal termination:   lo-byte = term status,  hi-byte = 0.
			 */
			if (0 == (dwStatus & 0xff)) {
				status = (int)dwStatus >> 8;
			} else {
				status = (int)dwStatus;
			}
			return true;

		} else if (WAIT_TIMEOUT == rc) {
			errno = EAGAIN;		// nohang

		} else {
			errno = ECHILD;		// other
		}
		return false;
	}

private:
	inetd::CriticalSection completelock_;
	std::list<std::unique_ptr<Process>> complete_;
	void (*sigchld_)();
	unsigned unmanaged_;
	ScopedHandle job_;
	ScopedHandle thread_;
	ScopedHandle waitevent_;
	ScopedHandle port_;
};

};  //namespace inetd

//end