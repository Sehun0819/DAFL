#include "gpf-put-helper.h"

#include <sys/shm.h>
#include <unistd.h>

#include "../types.h"

namespace gpf_handler {

std::map<std::string, void*>& shm_table() {
  static bool initialized = false;
  static std::map<std::string, void*> tbl;
  if (!initialized) {
    tbl.insert({"__GPF_CFG_SHM_TRACE_TARGET_BM", nullptr});
    tbl.insert({"__GPF_CFG_SHM_TRACE_TARGET_BM_SIZE", nullptr});
    tbl.insert({"__GPF_CFG_SHM_REACHABILITY_BM", nullptr});
    tbl.insert({"__GPF_CFG_SHM_REACHABILITY_BM_SIZE", nullptr});
    tbl.insert({"__GPF_CFG_SHM_COVERED_EDGES", nullptr});
    tbl.insert({"__GPF_CFG_SHM_COVERED_EDGES_SIZE", nullptr});

    tbl.insert({"__GPF_CG_SHM_REACHABILITY_BM", nullptr});
    tbl.insert({"__GPF_CG_SHM_REACHABILITY_BM_SIZE", nullptr});
    tbl.insert({"__GPF_CG_SHM_COVERED_EDGES", nullptr});
    tbl.insert({"__GPF_CG_SHM_COVERED_EDGES_SIZE", nullptr});

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
    size_t* cfg_shm_trace_target_bm_size =
        (size_t*)shm_table().at("__GPF_CFG_SHM_TRACE_TARGET_BM_SIZE");
    size_t* cfg_shm_reachability_bm_size =
        (size_t*)shm_table().at("__GPF_CFG_SHM_REACHABILITY_BM_SIZE");
    size_t* cg_shm_reachability_bm_size =
        (size_t*)shm_table().at("__GPF_CG_SHM_REACHABILITY_BM_SIZE");

    auto cfg_shm = std::make_unique<gpf::IntraCFGSharedMemory>(
        (uint8_t*)shm_table().at("__GPF_CFG_SHM_TRACE_TARGET_BM"),
        *cfg_shm_trace_target_bm_size,
        (uint8_t*)shm_table().at("__GPF_CFG_SHM_REACHABILITY_BM"),
        *cfg_shm_reachability_bm_size,
        (uint64_t*)shm_table().at("__GPF_CFG_SHM_COVERED_EDGES"),
        (size_t*)shm_table().at("__GPF_CFG_SHM_COVERED_EDGES_SIZE"));

    auto cg_shm = std::make_unique<gpf::CFGSharedMemory>(
        (uint8_t*)shm_table().at("__GPF_CG_SHM_REACHABILITY_BM"),
        *cg_shm_reachability_bm_size,
        (uint64_t*)shm_table().at("__GPF_CG_SHM_COVERED_EDGES"),
        (size_t*)shm_table().at("__GPF_CG_SHM_COVERED_EDGES_SIZE"));

    gpf::set_cfg_tracer(std::move(cfg_shm));
    gpf::set_cg_tracer(std::move(cg_shm));
  }
}

void trace_on(void) {
  if (launched_by_afl) {
    gpf::cfg_tracer().clear_trace();
    gpf::cg_tracer().clear_trace();
  }
}

}  // namespace gpf_handler
