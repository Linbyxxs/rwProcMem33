﻿#include <cstdio>
#include <string.h> 
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <memory>
#include <dirent.h>
#include <cinttypes>
#include "MapRegionHelper.h"
#include "MemSearchHelper.h"
#include "ReverseSearchAddrLinkMapHelper.h"



int findPID(const char *lpszCmdline, CMemoryReaderWriter *pDriver)
{
	int nTargetPid = 0;

	//驱动_获取进程PID列表
	std::vector<int> vPID;
	BOOL bOutListCompleted;
	BOOL b = pDriver->GetProcessPidList(vPID, FALSE, bOutListCompleted);
	printf("调用驱动 GetProcessPidList 返回值:%d\n", b);

	//打印进程列表信息
	for (int pid : vPID)
	{
		//驱动_打开进程
		uint64_t hProcess = pDriver->OpenProcess(pid);
		if (!hProcess) { continue; }

		//驱动_获取进程命令行
		char cmdline[100] = { 0 };
		pDriver->GetProcessCmdline(hProcess, cmdline, sizeof(cmdline));

		//驱动_关闭进程
		pDriver->CloseHandle(hProcess);

		if (strcmp(lpszCmdline, cmdline) == 0)
		{
			nTargetPid = pid;
			break;
		}
	}
	return nTargetPid;
}


int main(int argc, char *argv[])
{
	printf(
		"======================================================\n"
		"本驱动名称: Linux ARM64 硬件读写进程内存驱动37\n"
		"本驱动接口列表：\n"
		"\t1.	 驱动_设置驱动设备接口文件允许同时被使用的最大值: SetMaxDevFileOpen\n"
		"\t2.	 驱动_隐藏驱动（卸载驱动需重启机器）: HideKernelModule\n"
		"\t3.	 驱动_打开进程: OpenProcess\n"
		"\t4.	 驱动_读取进程内存: ReadProcessMemory\n"
		"\t5.	 驱动_写入进程内存: WriteProcessMemory\n"
		"\t6.	 驱动_关闭进程: CloseHandle\n"
		"\t7.	 驱动_获取进程内存块列表: VirtualQueryExFull（可选：显示全部内存、只显示在物理内存中的内存）\n"
		"\t8.	 驱动_获取进程PID列表: GetProcessPidList\n"
		"\t9.	 驱动_获取进程权限等级: GetProcessGroup\n"
		"\t10.驱动_提升进程权限到Root: SetProcessRoot\n"
		"\t11.驱动_获取进程占用物理内存大小: GetProcessRSS\n"
		"\t12.驱动_获取进程命令行: GetProcessCmdline\n"
		"\t以上所有功能不注入、不附加进程，不打开进程任何文件，所有操作均为内核操作\n"
		"======================================================\n"
	);


	CMemoryReaderWriter rwDriver;


	//驱动默认文件名
	std::string devFileName = RWPROCMEM_FILE_NODE;
	if (argc > 1)
	{
		//如果用户自定义输入驱动名
		devFileName = argv[1];
	}
	printf("Connecting rwDriver:%s\n", devFileName.c_str());


	//连接驱动
	int err = 0;
	if (!rwDriver.ConnectDriver(devFileName.c_str(), err))
	{
		printf("Connect rwDriver failed. error:%d\n", err);
		fflush(stdout);
		return 0;
	}



	//获取目标进程PID
	const char *name = "com.miui.calculator";
	pid_t pid = findPID(name, &rwDriver);
	if (pid == 0)
	{
		printf("找不到进程\n");
		fflush(stdout);
		return 0;
	}
	printf("目标进程PID:%d\n", pid);
	//打开进程
	uint64_t hProcess = rwDriver.OpenProcess(pid);
	printf("调用驱动 OpenProcess 返回值:%" PRIu64 "\n", hProcess);
	if (!hProcess)
	{
		printf("调用驱动 OpenProcess 失败\n");
		fflush(stdout);
		return 0;
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


	//获取进程ANONMYOUS内存区域
	std::vector<MEM_SECTION_INFO> vScanMemMaps;
	GetMemRegion(&rwDriver, hProcess, 
		A_ANONMYOUS, 
		TRUE/*FALSE为全部内存，TRUE为只在物理内存中的内存*/, 
		vScanMemMaps);
	if (!vScanMemMaps.size())
	{
		printf("无内存可搜索\n");
		//关闭进程
		rwDriver.CloseHandle(hProcess);
		printf("调用驱动 CloseHandle:%" PRIu64 "\n", hProcess);
		fflush(stdout);
		return 0;
	}

	//首次搜索
	SafeVector<ADDR_RESULT_INFO> vSearchResult; //搜索结果
	{
		SafeVector<MEM_SECTION_INFO> vScanMemMapsList(vScanMemMaps); //搜索结果
		SafeVector<MEM_SECTION_INFO> vSearcMemSecError;


		//非阻塞模式
		//std::thread td1(SearchMemoryThread<float>,.....);
		//td1.detach();


		//阻塞模式
		SearchMemoryThread<float>(
			&rwDriver,
			hProcess,
			vScanMemMapsList, //待搜索的内存区域
			0.33333334327f, //搜索数值
			0.0f,
			0.01, //误差范围
			SCAN_TYPE::ACCURATE_VAL, //搜索类型: 精确搜索
			std::thread::hardware_concurrency() - 1, //搜索线程数
			4, //快速扫描的对齐位数，CE默认为4
			vSearchResult, //搜索后的结果
			vSearcMemSecError);


		printf("共搜索出%zu个地址\n", vSearchResult.size());
	}
	//再次搜索
	while (vSearchResult.size()) {
		//将每个地址往后偏移20
		SafeVector<ADDR_RESULT_INFO> vWaitSearchAddr; //待搜索的内存地址列表

		ADDR_RESULT_INFO addr;
		while (vSearchResult.get_val(addr))
		{
			addr.addr += 20;
			vWaitSearchAddr.push_back(addr);
		}

		//再次搜索
		vSearchResult.clear();

		SafeVector<ADDR_RESULT_INFO> vSearcAddrError;
		SearchNextMemoryThread<float>(
			&rwDriver,
			hProcess,
			vWaitSearchAddr, //待搜索的内存地址列表
			1.19175350666f, //搜索数值
			0.0f,
			0.01f, //误差范围
			SCAN_TYPE::ACCURATE_VAL, //搜索类型: 精确搜索
			std::thread::hardware_concurrency() - 1, //搜索线程数
			vSearchResult,  //搜索后的结果
			vSearcAddrError);
		break;
	}

	//再次搜索
	while(vSearchResult.size()) {
		//将每个地址往后偏移952
		SafeVector<ADDR_RESULT_INFO> vWaitSearchAddr; //待搜索的内存地址列表

		ADDR_RESULT_INFO addr;
		while (vSearchResult.get_val(addr))
		{
			addr.addr += 952;
			vWaitSearchAddr.push_back(addr);
		}

		//再次搜索
		vSearchResult.clear();

		SafeVector<ADDR_RESULT_INFO> vSearcAddrError;
		SearchNextMemoryThread<int>(
			&rwDriver,
			hProcess,
			vWaitSearchAddr, //待搜索的内存地址列表
			-2147483648, //搜索数值
			0,
			0.01, //误差范围
			SCAN_TYPE::ACCURATE_VAL, //搜索类型: 精确搜索
			std::thread::hardware_concurrency() - 1, //搜索线程数
			vSearchResult,  //搜索后的结果
			vSearcAddrError);
		break;
	}

	size_t count = 0;
	for (size_t i = 0; i < vSearchResult.size(); i++) {
		ADDR_RESULT_INFO addr = vSearchResult.at(i);
		printf("addr:%p\n", (void*)addr.addr);
		count++;
		if (count > 100) {
			printf("只显示前100个地址\n");
			break;
		}
	}
	printf("共偏移搜索出%zu个地址\n", vSearchResult.size());
	if (vSearchResult.size()) {
		printf("第一个地址为:%p\n", (void*)vSearchResult.at(0).addr);
	}


	//反向搜索演示
	std::map<uint64_t, std::shared_ptr<baseOffsetInfo>> baseAddrOffsetRecordMap; //记录搜索到的地址偏移

	SafeVector<BATCH_BETWEEN_VAL_ADDR_RESULT<uint64_t>> vBetweenValSearchResult; //反向搜索结果
	while (vSearchResult.size()) {

		SafeVector<MEM_SECTION_INFO> vScanMemMapsList(vScanMemMaps);

		std::vector<BATCH_BETWEEN_VAL<uint64_t>> vWaitSearchBetweenVal; //待搜索的两值之间的数值数组

		//开始填充待搜索的两值之间的数值数组
		std::vector<ADDR_RESULT_INFO> vLastAddrResult;
		vSearchResult.get_vals(vSearchResult.size(), vLastAddrResult);
		for (auto & addrResult : vLastAddrResult) {
			BATCH_BETWEEN_VAL<uint64_t> newBeteenVal;
			memset(&newBeteenVal, 0, sizeof(newBeteenVal));
			newBeteenVal.val1 = addrResult.addr - 0x300;
			newBeteenVal.val2 = addrResult.addr + 0x300;
			newBeteenVal.markContext.u64Ctx = addrResult.addr;//传递保存一个标值地址
			vWaitSearchBetweenVal.push_back(newBeteenVal);
		}

		SafeVector<MEM_SECTION_INFO> vSearcMemSecError; //错误内存段
		//阻塞模式
		SearchMemoryBatchBetweenValThread<uint64_t>(
			&rwDriver,
			1,
			vScanMemMapsList, //待搜索的内存区域
			vWaitSearchBetweenVal, //待搜索的两值之间的数值数组
			std::thread::hardware_concurrency() - 1, //搜索线程数
			4, //快速扫描的对齐位数，CE默认为4
			vBetweenValSearchResult, //搜索后的结果
			vSearcMemSecError);


		//记录搜索到的偏移
		for (size_t i = 0; i < vBetweenValSearchResult.size(); i++) {
			auto & addrResult = vBetweenValSearchResult.at(i);

			uint64_t curAddrVal = *(uint64_t*)addrResult.addrInfo.spSaveData.get();

			std::shared_ptr<baseOffsetInfo> spBaseOffset = std::make_shared<baseOffsetInfo>();
			memset(spBaseOffset.get(), 0, sizeof(baseOffsetInfo));
			spBaseOffset->addrId = addrResult.addrInfo.addr;
			spBaseOffset->offset = addrResult.markContext.u64Ctx - curAddrVal;
			baseAddrOffsetRecordMap[spBaseOffset->addrId] = spBaseOffset;
		}
		break;
	}
	//再次反向搜索
	while (vBetweenValSearchResult.size()) {

		SafeVector<MEM_SECTION_INFO> vScanMemMapsList(vScanMemMaps);

		std::vector<BATCH_BETWEEN_VAL<uint64_t>> vWaitSearchBetweenVal; //待搜索的两值之间的数值数组


		std::vector<BATCH_BETWEEN_VAL_ADDR_RESULT<uint64_t>> vLastAddrResult;
		vBetweenValSearchResult.get_vals(vBetweenValSearchResult.size(), vLastAddrResult);
		for (auto & addrResult : vLastAddrResult) {
			BATCH_BETWEEN_VAL<uint64_t> newBeteenVal;
			memset(&newBeteenVal, 0, sizeof(newBeteenVal));
			newBeteenVal.val1 = addrResult.addrInfo.addr - 0x300;
			newBeteenVal.val2 = addrResult.addrInfo.addr + 0x300;
			newBeteenVal.markContext.u64Ctx = addrResult.addrInfo.addr;//传递保存一个标值地址
			vWaitSearchBetweenVal.push_back(newBeteenVal);
		}

		vBetweenValSearchResult.clear();
		SafeVector<MEM_SECTION_INFO> vSearcMemSecError;

		//阻塞模式
		SearchMemoryBatchBetweenValThread<uint64_t>(
			&rwDriver,
			1,
			vScanMemMapsList, //待搜索的内存区域
			vWaitSearchBetweenVal, //待搜索的两值之间的数值数组
			std::thread::hardware_concurrency() - 1, //搜索线程数
			4, //快速扫描的对齐位数，CE默认为4
			vBetweenValSearchResult, //搜索后的结果
			vSearcMemSecError);

		//记录搜索到的偏移
		for (size_t i = 0; i < vBetweenValSearchResult.size(); i++) {
			auto & addrResult = vBetweenValSearchResult.at(i);

			uint64_t curAddrVal = *(uint64_t*)addrResult.addrInfo.spSaveData.get();

			std::shared_ptr<baseOffsetInfo> spBaseOffset = std::make_shared<baseOffsetInfo>();
			memset(spBaseOffset.get(), 0, sizeof(baseOffsetInfo));
			spBaseOffset->addrId = addrResult.addrInfo.addr;
			spBaseOffset->offset = addrResult.markContext.u64Ctx - curAddrVal;
			baseAddrOffsetRecordMap[spBaseOffset->addrId] = spBaseOffset;

			//将反向搜索到的地址偏移串联起来
			auto spLastAddrNode = baseAddrOffsetRecordMap[addrResult.markContext.u64Ctx];
			spLastAddrNode->vwpNextNode.push_back(spBaseOffset);
			spBaseOffset->vwpLastNode.push_back(spLastAddrNode);
		}

		break;
	}
	//再次反向搜索
	while (vBetweenValSearchResult.size()) {

		SafeVector<MEM_SECTION_INFO> vScanMemMapsList(vScanMemMaps);

		std::vector<BATCH_BETWEEN_VAL<uint64_t>> vWaitSearchBetweenVal; //待搜索的两值之间的数值数组

		std::vector<BATCH_BETWEEN_VAL_ADDR_RESULT<uint64_t>> vTempAddrResult;
		vBetweenValSearchResult.get_vals(vBetweenValSearchResult.size(), vTempAddrResult);
		for (auto & addrResult : vTempAddrResult) {
			BATCH_BETWEEN_VAL<uint64_t> newBeteenVal;
			memset(&newBeteenVal, 0, sizeof(newBeteenVal));
			newBeteenVal.val1 = addrResult.addrInfo.addr - 0x300;
			newBeteenVal.val2 = addrResult.addrInfo.addr + 0x300;
			newBeteenVal.markContext.u64Ctx = addrResult.addrInfo.addr;//传递保存一个标值地址
			vWaitSearchBetweenVal.push_back(newBeteenVal);
		}

		vBetweenValSearchResult.clear();
		SafeVector<MEM_SECTION_INFO> vSearcMemSecError;

		//阻塞模式
		SearchMemoryBatchBetweenValThread<uint64_t>(
			&rwDriver,
			1,
			vScanMemMapsList, //待搜索的内存区域
			vWaitSearchBetweenVal, //待搜索的两值之间的数值数组
			std::thread::hardware_concurrency() - 1, //搜索线程数
			4, //快速扫描的对齐位数，CE默认为4
			vBetweenValSearchResult, //搜索后的结果
			vSearcMemSecError);


		//记录搜索到的偏移
		for (size_t i = 0; i < vBetweenValSearchResult.size(); i++) {
			auto & addrResult = vBetweenValSearchResult.at(i);

			uint64_t curAddrVal = *(uint64_t*)addrResult.addrInfo.spSaveData.get();

			std::shared_ptr<baseOffsetInfo> spBaseOffset = std::make_shared<baseOffsetInfo>();
			memset(spBaseOffset.get(), 0, sizeof(baseOffsetInfo));
			spBaseOffset->addrId = addrResult.addrInfo.addr;
			spBaseOffset->offset = addrResult.markContext.u64Ctx - curAddrVal;
			baseAddrOffsetRecordMap[spBaseOffset->addrId] = spBaseOffset;

			//将反向搜索到的地址偏移串联起来
			auto spLastAddrNode = baseAddrOffsetRecordMap[addrResult.markContext.u64Ctx];
			spLastAddrNode->vwpNextNode.push_back(spBaseOffset);
			spBaseOffset->vwpLastNode.push_back(spLastAddrNode);
		}

		//打印整个链路
		std::string strOffsetLink;
		for (auto iter = baseAddrOffsetRecordMap.begin(); iter != baseAddrOffsetRecordMap.end(); iter++) {
			auto spBaseAddrOffsetInfo = iter->second;
			if (spBaseAddrOffsetInfo->vwpLastNode.size()) {
				continue; //不是头部节点
			}
			strOffsetLink += PrintfAddrOffsetLinkMap(baseAddrOffsetRecordMap, spBaseAddrOffsetInfo->addrId);
		}
		printf("OffsetLink:\n%s\n", strOffsetLink.c_str());
		break;
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//关闭进程
	rwDriver.CloseHandle(hProcess);
	printf("调用驱动 CloseHandle:%" PRIu64 "\n", hProcess);
	fflush(stdout);
	return 0;
}