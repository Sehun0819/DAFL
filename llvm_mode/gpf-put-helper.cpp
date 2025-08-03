#include "gpf-put-helper.h"

#include <sys/shm.h>
#include <unistd.h>

#include "../types.h"

namespace gpf_handler {

std::map<std::string, void*>& shm_table() {
  static bool initialized = false;
  static std::map<std::string, void*> tbl;
  if (!initialized) {
    tbl.insert({"__GPF_SHM_PATHLOG", nullptr});
    tbl.insert({"__GPF_SHM_PATHLOG_SIZE", nullptr});
    tbl.insert({"__GPF_SHM_NUM_GUARDS", nullptr});
    tbl.insert({"__GPF_SHM_COVERED_BM_RAW", nullptr});
    tbl.insert({"__GPF_SHM_ND_BM_RAW", nullptr});
    tbl.insert({"__GPF_SHM_PCID_TO_PC_RAW", nullptr});

    tbl.insert({"__GPF_SHM_COVERED_EDGES", nullptr});
    tbl.insert({"__GPF_SHM_COVERED_EDGES_SIZE", nullptr});
    tbl.insert({"__GPF_SHM_NEWLY_COVERED_INDIRECT_EDGES", nullptr});
    tbl.insert({"__GPF_SHM_NEWLY_COVERED_INDIRECT_EDGES_SIZE", nullptr});
    tbl.insert({"__GPF_SHM_FUNC_ID_BM", nullptr});
    tbl.insert({"__GPF_SHM_FUNC_ID_BM_SIZE", nullptr});

    initialized = true;
  }
  return tbl;
}

static bool launched_by_afl = false;

void map_shm(void) {
  for (auto& p : shm_table()) {
    std::string env_var = p.first;
    u8* id_str = (u8*)getenv(env_var.c_str());
    if (id_str) {
      launched_by_afl = true;
      u32 shm_id = atoi((char*)id_str);
      p.second = shmat(shm_id, NULL, 0);

      if (p.second == (void*)-1) _exit(1);
    }
  }

  if (launched_by_afl) {
    gpf::TPCAFLPUT().InitPathLogSHM(
        (gpf::PCID*)shm_table().at("__GPF_SHM_PATHLOG"),
        (size_t*)shm_table().at("__GPF_SHM_PATHLOG_SIZE"));
    gpf::TPCAFLPUT().InitBitMapSHM(
        (size_t*)shm_table().at("__GPF_SHM_NUM_GUARDS"),
        (uint8_t*)shm_table().at("__GPF_SHM_COVERED_BM_RAW"),
        (uint8_t*)shm_table().at("__GPF_SHM_ND_BM_RAW"),
        (void**)shm_table().at("__GPF_SHM_PCID_TO_PC_RAW"));

    gpf::CGAFLPUT().init_shm_covered_edges(
        (uint64_t*)shm_table().at("__GPF_SHM_COVERED_EDGES"),
        (size_t*)shm_table().at("__GPF_SHM_COVERED_EDGES_SIZE"));
    gpf::CGAFLPUT().init_shm_newly_covered_indirect_edges(
        (uint64_t*)shm_table().at("__GPF_SHM_NEWLY_COVERED_INDIRECT_EDGES"),
        (size_t*)shm_table().at("__GPF_SHM_NEWLY_COVERED_INDIRECT_EDGES_SIZE"));
    gpf::CGAFLPUT().init_shm_func_id_bm(
        (uint8_t*)shm_table().at("__GPF_SHM_FUNC_ID_BM"),
        (size_t*)shm_table().at("__GPF_SHM_FUNC_ID_BM_SIZE"));
  }
}

void trace_on(void) {
  if (launched_by_afl) {
    gpf::TPCAFLPUT().TraceOn();
    gpf::TPCAFLPUT().ClearPathLog();
  }
}

}  // namespace gpf_handler
