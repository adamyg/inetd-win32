#pragma once
/* -*- mode: c; indent-width: 8; -*- */
/*
 * inetd::CPULoadInfo
 * windows inetd service.
 *
 * Copyright (c) 2021 - 2022, Adam Young.
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

#include "WindowStd.h"
#include "SimpleLock.h"

#include <pdh.h>

#pragma comment(lib, "Pdh.lib")

namespace inetd {

class CPULoadInfo {
	CPULoadInfo(const CPULoadInfo &) = delete;
	CPULoadInfo& operator=(const CPULoadInfo &) = delete;

    private:
	struct SystemTimes
	{
		SystemTimes() : idleTime(0), kernelTime(0), userTime(0)
		{
		}

		bool update()
		{
			FILETIME t_idleTime, t_kernelTime, t_userTime;

			if (! GetSystemTimes(&t_idleTime, &t_kernelTime, &t_userTime))
				return false;

			idleTime   = convert(t_idleTime);
			kernelTime = convert(t_kernelTime);
			userTime   = convert(t_userTime);
			return true;
		}

		static uint64_t
		convert(const FILETIME &ft)
		{
			return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) + static_cast<uint64_t>(ft.dwLowDateTime);
		}

		uint64_t idleTime;
		uint64_t kernelTime;
		uint64_t userTime;
	};

    public:
	CPULoadInfo() : cpu_number_(cpu_physical_number()), query_(0), last_tick_(0), last_percentage_(0.0)
	{
		counter_init();
		getloadavg();
		getusage();
	}

	double getloadavg()
	{
		CriticalSection::Guard guard(critical_section_);
		if (! next_update(last_tick_))
			return last_percentage_;

		double t_percentage;
		if (counter_read(t_percentage))
			last_percentage_ = t_percentage;

		return last_percentage_;
	}

	double getusage()
	{
		CriticalSection::Guard guard(critical_section_);
		if (! next_update(last_tick2_))
			return last_percentage2_;

		SystemTimes t_system_times;
		if (! t_system_times.update())
			return last_percentage2_;

		SystemTimes delta(t_system_times);

		delta.idleTime	 -= system_times_.idleTime;
		delta.kernelTime -= system_times_.kernelTime;
		delta.userTime	 -= system_times_.userTime;

		if (0 == (delta.userTime + delta.kernelTime))
			return last_percentage2_;

	    //	loadavg = 1.0 - (double) delta.idleTime / (delta.kernelTime + delta.userTime);

		system_times_ = t_system_times;

		const uint64_t totalTime = delta.userTime + delta.kernelTime,
			activeTime = totalTime - delta.idleTime;

		last_percentage2_ = activeTime * 100.0f / totalTime;
		return last_percentage2_;
	}

    private:
	static unsigned
	cpu_physical_number()
	{
		SYSTEM_INFO si = {0};
		SYSTEM_LOGICAL_PROCESSOR_INFORMATION *pi = NULL;
		DWORD len = 0, i;
		int count = 0;

		GetSystemInfo(&si);		// logical cpu's
		count = si.dwNumberOfProcessors;

		while (! GetLogicalProcessorInformation(pi, &len)) {
			if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
				if (NULL == (pi = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION *)malloc(len))) {
					return count;
				}
			} else {
				return count;
			}
		}

		count = 0;
		for (i = 0; i < len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION); ++i) {
			if (pi[i].Relationship == RelationProcessorCore) {
				++count;	// physical cpu's
			}
		}
		free(pi);
		return count;
	}

	static bool
	next_update(ULONGLONG &last)
	{
		const ULONGLONG tick = GetTickCount64();
		if ((tick + 250) >= last) {
			last = tick;
			return true;
		}
		return false;
	}

	bool
	counter_init()
	{
		PDH_STATUS status;

		if (query_)
			return true;

		status = ::PdhOpenQueryW(NULL, 0, &query_);
		if (ERROR_SUCCESS != status)
			return false;

		status = ::PdhAddEnglishCounterW(query_, L"\\Processor(_Total)\\% Processor Time", 0, &counter_time_);
		if (ERROR_SUCCESS != status)
			return false;

		status = ::PdhAddEnglishCounterW(query_, L"\\System\\Processor Queue Length", 0, &counter_queue_);
		if (ERROR_SUCCESS != status)
			return false;

		::Sleep(10);
		return true;
	}

	bool
	counter_read(double &load)
	{
		PDH_STATUS status;

		load = 0.0;

		status = ::PdhCollectQueryData(query_);
		if (ERROR_SUCCESS != status)
			return false;

		PDH_FMT_COUNTERVALUE value_time;
		status = ::PdhGetFormattedCounterValue(counter_time_, PDH_FMT_DOUBLE, NULL, &value_time);
		if (ERROR_SUCCESS != status)
			return false;

		PDH_FMT_COUNTERVALUE value_queue;
		status = ::PdhGetFormattedCounterValue(counter_queue_, PDH_FMT_LONG, NULL, &value_queue);
		if (ERROR_SUCCESS != status)
			return false;

		const double running = value_time.doubleValue * cpu_number_ / 100;
		load = value_queue.longValue + running;

		return true;
	}

    private:
	const unsigned cpu_number_;
	CriticalSection critical_section_;
	SystemTimes system_times_;
	PDH_HQUERY query_;
	PDH_HCOUNTER counter_time_;
	PDH_HCOUNTER counter_queue_;
	ULONGLONG last_tick_;
	double last_percentage_;
	ULONGLONG last_tick2_;
	double last_percentage2_;
};

}  //namespace inetd

//end
