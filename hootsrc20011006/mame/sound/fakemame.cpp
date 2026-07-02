#include "StdAfx.h"
#include "ssConfig.h"
#include "ssSoundDriverManager.h"
#include "driver.h"

#define MAX_REGION 4

struct RunningMachine *Machine;
struct RunningMachine MachineInstance;

void *mem_region[MAX_REGION];

void init_mame(void)
{
	Machine = &MachineInstance;

	ssSoundDriverManager *manager = ssSoundDriverManager::Instance();
	const ssConfig &config = manager->GetConfig();
	Machine->sample_rate = config.sampling_rate;
}


void *memory_region(int _region)
{
	return mem_region[_region];
}

void set_memory_region(int _region, void *_addr)
{
	mem_region[_region] = _addr;
}
