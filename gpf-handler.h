#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "debug.h"
#include "types.h"

namespace fs = std::filesystem;

#define PF_SAYF(x...) \
  do {                \
    SAYF("[GPF] " x); \
  } while (0)

#define PF_ACTF(x...)           \
  do {                          \
    SAYF(cLBL "[GPF] " cRST x); \
    SAYF(cRST "\n");            \
  } while (0)

namespace gpf_handler {

extern u64 total_execs;

extern std::vector<std::string> tc_from_pf;
extern std::vector<std::string> tc_to_pf;

#define GPF_TERMINAL_SIZE 1 << 15
extern char terminal[GPF_TERMINAL_SIZE];

void setup_shm(void);
void init();

}  // namespace gpf_handler

void sub_timespec(struct timespec t1, struct timespec t2, struct timespec* td);
int to_nsec(const struct timespec& t);
void rsrc_schedule(uint32_t& afl_iter, uint32_t& afl_tmout, uint32_t& pf_iter,
                   uint32_t& pf_tmout);
std::string substitute_pcs(u8* bin_path, std::string target_str,
                           std::map<size_t, std::string> pcid_to_pc,
                           bool rm_common_path, bool dot);
void substitute_pcs(u8* bin_path, const fs::path& dir,
                    std::map<size_t, std::string> pcid_to_pc,
                    bool rm_common_path, bool dot);

void print_nd_branches(u8* bin_path, std::map<size_t, std::string> pcid_to_pc,
                       std::vector<size_t> nd_pcids);
