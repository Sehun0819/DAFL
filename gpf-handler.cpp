#include "gpf-handler.h"

#include <sys/shm.h>
#include <sys/stat.h>
#include <time.h>

#include <optional>
#include <string>
#include <vector>

#include "alloc-inl.h"
#include "debug.h"
#include "gpf/cfg.h"
#include "gpf/trace_pc.h"
#include "gpf/utils.h"

namespace gpf_handler {

std::vector<std::string> tc_from_pf;
std::vector<std::string> tc_to_pf;

char terminal[GPF_TERMINAL_SIZE];

struct SHMEntry {
  SHMEntry(size_t size_) : size(size_) {}
  u32 size;
  s32 id;
  void* addr;
};

size_t n_bit_to_n_byte(size_t n_bit) {
  return (n_bit / 8) + (n_bit % 8 == 0 ? 0 : 1);
}

std::map<std::string, SHMEntry>& shm_table() {
  static bool initialized = false;
  static std::map<std::string, SHMEntry> tbl;
  if (!initialized) {
    tbl.insert(
        {"__GPF_SHM_PATHLOG", SHMEntry(sizeof(u8) * PF_EXECPATH_MAX_BYTE)});
    tbl.insert({"__GPF_SHM_PATHLOG_SIZE", SHMEntry(sizeof(size_t))});
    tbl.insert({"__GPF_SHM_NUM_GUARDS", SHMEntry(sizeof(size_t))});
    tbl.insert({"__GPF_SHM_COVERED_BM_RAW",
                SHMEntry(sizeof(u8) * PF_BITMAP_MAX_BYTE)});
    tbl.insert(
        {"__GPF_SHM_ND_BM_RAW", SHMEntry(sizeof(u8) * PF_BITMAP_MAX_BYTE)});
    tbl.insert(
        {"__GPF_SHM_PCID_TO_PC_RAW", SHMEntry(sizeof(void*) * PF_NUM_PC_MAX)});

    tbl.insert({"__GPF_CFG_SHM_TRACE_TARGET_BM",
                SHMEntry(sizeof(u8) *
                         n_bit_to_n_byte(gpf::cfg_static_all().n_nodes()))});
    tbl.insert(
        {"__GPF_CFG_SHM_TRACE_TARGET_BM_SIZE", SHMEntry(sizeof(size_t))});
    tbl.insert({"__GPF_CFG_SHM_REACHABILITY_BM",
                SHMEntry(sizeof(u8) *
                         n_bit_to_n_byte(gpf::cfg_static_all().n_nodes()))});
    tbl.insert(
        {"__GPF_CFG_SHM_REACHABILITY_BM_SIZE", SHMEntry(sizeof(size_t))});
    tbl.insert({"__GPF_CFG_SHM_COVERED_EDGES",
                SHMEntry(sizeof(u8) * PF_EXECPATH_MAX_BYTE)});
    tbl.insert({"__GPF_CFG_SHM_COVERED_EDGES_SIZE", SHMEntry(sizeof(size_t))});

    tbl.insert(
        {"__GPF_CG_SHM_REACHABILITY_BM",
         SHMEntry(sizeof(u8) * n_bit_to_n_byte(gpf::cg_static().n_nodes()))});
    tbl.insert({"__GPF_CG_SHM_REACHABILITY_BM_SIZE", SHMEntry(sizeof(size_t))});
    tbl.insert({"__GPF_CG_SHM_COVERED_EDGES",
                SHMEntry(sizeof(u8) * PF_EXECPATH_MAX_BYTE)});
    tbl.insert({"__GPF_CG_SHM_COVERED_EDGES_SIZE", SHMEntry(sizeof(size_t))});

    initialized = true;
  }
  return tbl;
}

u64 total_execs;

void remove_shm(void) {
  for (auto& p : shm_table()) {
    SHMEntry& shm = p.second;
    shmctl(shm.id, IPC_RMID, NULL);
  }
}

void setup_shm(void) {
  for (auto& p : shm_table()) {
    std::string env_var = p.first;
    SHMEntry& shm = p.second;
    shm.id = shmget(IPC_PRIVATE, shm.size, IPC_CREAT | IPC_EXCL | 0600);
    if (shm.id < 0) PFATAL("shmget() failed");

    u8* shm_str = alloc_printf("%d", shm.id);

    setenv(env_var.c_str(), shm_str, 1);
    ck_free(shm_str);

    shm.addr = shmat(shm.id, NULL, 0);
    if (shm.addr == (void*)-1) PFATAL("shmat() failed");
  }

  atexit(remove_shm);
}

void init() {
  gpf::TPCAFLMain().InitPathLogSHM(
      (gpf::PCID*)shm_table().at("__GPF_SHM_PATHLOG").addr,
      (size_t*)shm_table().at("__GPF_SHM_PATHLOG_SIZE").addr);
  gpf::TPCAFLMain().InitBitMapSHM(
      (size_t*)shm_table().at("__GPF_SHM_NUM_GUARDS").addr,
      (uint8_t*)shm_table().at("__GPF_SHM_COVERED_BM_RAW").addr,
      (uint8_t*)shm_table().at("__GPF_SHM_ND_BM_RAW").addr,
      (void**)shm_table().at("__GPF_SHM_PCID_TO_PC_RAW").addr);

  gpf::TargetLoc target_loc = gpf::get_target_loc();

  size_t* cfg_shm_trace_target_bm_size =
      (size_t*)shm_table().at("__GPF_CFG_SHM_TRACE_TARGET_BM_SIZE").addr;
  *cfg_shm_trace_target_bm_size = gpf::cfg_static_all().n_nodes();
  size_t* cfg_shm_reachability_bm_size =
      (size_t*)shm_table().at("__GPF_CFG_SHM_REACHABILITY_BM_SIZE").addr;
  *cfg_shm_reachability_bm_size = gpf::cfg_static_all().n_nodes();
  size_t* cg_shm_reachability_bm_size =
      (size_t*)shm_table().at("__GPF_CG_SHM_REACHABILITY_BM_SIZE").addr;
  *cg_shm_reachability_bm_size = gpf::cg_static().n_nodes();

  auto cfg_shm = std::make_unique<gpf::IntraCFGSharedMemory>(
      (uint8_t*)shm_table().at("__GPF_CFG_SHM_TRACE_TARGET_BM").addr,
      *cfg_shm_trace_target_bm_size,
      (uint8_t*)shm_table().at("__GPF_CFG_SHM_REACHABILITY_BM").addr,
      *cfg_shm_reachability_bm_size,
      (uint64_t*)shm_table().at("__GPF_CFG_SHM_COVERED_EDGES").addr,
      (size_t*)shm_table().at("__GPF_CFG_SHM_COVERED_EDGES_SIZE").addr);

  auto cg_shm = std::make_unique<gpf::CFGSharedMemory>(
      (uint8_t*)shm_table().at("__GPF_CG_SHM_REACHABILITY_BM").addr,
      *cg_shm_reachability_bm_size,
      (uint64_t*)shm_table().at("__GPF_CG_SHM_COVERED_EDGES").addr,
      (size_t*)shm_table().at("__GPF_CG_SHM_COVERED_EDGES_SIZE").addr);

  gpf::set_cfg_analyzer(std::move(cfg_shm), gpf::cfg_static_all(),
                        gpf::cfg_static_target(), target_loc.get_bb_ids());
  gpf::set_cg_analyzer(std::move(cg_shm), gpf::cg_static(),
                       target_loc.get_func_ids());
}

}  // namespace gpf_handler

#define NS_PER_SECOND 1000000000

void sub_timespec(struct timespec t1, struct timespec t2, struct timespec* td) {
  // https://stackoverflow.com/questions/53708076/what-is-the-proper-way-to-use-clock-gettime
  td->tv_nsec = t2.tv_nsec - t1.tv_nsec;
  td->tv_sec = t2.tv_sec - t1.tv_sec;
  if (td->tv_sec > 0 && td->tv_nsec < 0) {
    td->tv_nsec += NS_PER_SECOND;
    td->tv_sec--;
  } else if (td->tv_sec < 0 && td->tv_nsec > 0) {
    td->tv_nsec -= NS_PER_SECOND;
    td->tv_sec++;
  }
}

int to_nsec(const struct timespec& t) {
  return NS_PER_SECOND * t.tv_sec + t.tv_nsec;
}

void rsrc_schedule(uint32_t& afl_iter, uint32_t& afl_tmout, uint32_t& pf_iter,
                   uint32_t& pf_tmout) {
  afl_iter = 1;
  afl_tmout = 4294967295;
  pf_iter = 100;
  pf_tmout = 4294967295;
}

std::string replace_all(std::string str, std::string from, std::string to) {
  size_t pos = str.find(from);
  while (pos != std::string::npos) {
    str.replace(pos, from.size(), to);
    pos = str.find(from);
  }
  return str;
}

fs::path common_path(const fs::path& left, const fs::path& right) {
  fs::path common;

  auto it_left = left.begin();
  auto it_right = right.begin();
  while (it_left != left.end() && it_right != right.end()) {
    if (*it_left != *it_right) break;

    common /= *it_left;

    ++it_left;
    ++it_right;
  }

  return common;
}

fs::path common_path(const std::vector<fs::path>& paths) {
  if (paths.empty()) return fs::path();

  fs::path common = paths[0];
  for (size_t i = 1; i < paths.size(); i++)
    common = common_path(common, paths[i]);

  return common;
}

struct Loc {
  Loc(std::string loc_str) {
    loc_str = gpf::strip(loc_str);
    std::vector<std::string> words = gpf::split_all(loc_str, '\n');
    for (size_t i = 0; i < words.size(); i += 2) {
      assert(i + 1 < words.size());
      std::pair<std::string, fs::path> entry =
          std::make_pair(gpf::strip(words[i]), gpf::strip(words[i + 1]));
      entries.push_back(entry);
    }
  }
  fs::path common_path() const {
    std::vector<fs::path> paths;
    for (auto& entry : entries) paths.push_back(entry.second);
    return ::common_path(paths);
  }
  std::string to_string(size_t pos, bool dot) const {
    std::string sep = dot ? "\\n" : " from ";

    std::string str;
    for (size_t i = 0; i < entries.size(); i++) {
      auto entry = entries[i];
      auto func_name = entry.first;
      auto filepath_line = entry.second;
      str += func_name + " (" + std::string(filepath_line).substr(pos) + ")";
      if (i != entries.size() - 1) str += sep;
    }
    return str;
  }

  std::vector<std::pair<std::string, fs::path>> entries;
};

std::optional<Loc> run_symbolizer(const std::string& symbolizer_path,
                                  const std::string& bin_path,
                                  const std::string& pc) {
  // stackoverflow.com/questions/52164723

  static std::string bin_path_seen;
  if (bin_path_seen.empty()) {
    bin_path_seen = bin_path;
  } else if (bin_path_seen != bin_path) {
    PFATAL("Symbolizer is called on different binaries, %s and %s",
           bin_path_seen, bin_path);
  }

  static std::map<std::string, std::optional<Loc>> dict;
  auto it = dict.find(pc);
  if (it != dict.end()) return it->second;

  std::string cmd = symbolizer_path + " --obj " + bin_path + " " + pc;

  std::array<char, 128> buffer;
  std::string result;

  auto pipe = popen(cmd.data(), "r");

  if (!pipe) throw std::runtime_error("popen() failed!");

  while (!feof(pipe)) {
    if (fgets(buffer.data(), buffer.size(), pipe) != nullptr)
      result += buffer.data();
  }

  int exit_code = pclose(pipe);

  std::optional<Loc> loc;

  if (exit_code != EXIT_SUCCESS) {  // == 0
    loc = std::nullopt;
  } else {
    loc = Loc(result);
  }

  dict[pc] = loc;
  return loc;
}

std::string substitute_pcs(u8* bin_path, std::string target_str,
                           std::map<size_t, std::string> pcid_to_pc,
                           bool rm_common_path, bool dot) {
  static std::string symbolizer_path;

  static u8 initialized = 0;
  if (!initialized) {
    struct stat st;
    u32 f_len = 0;

    if (getenv("ASAN_SYMBOLIZER_PATH")) {
      symbolizer_path = getenv("ASAN_SYMBOLIZER_PATH");
    } else {
      PF_ACTF("'ASAN_SYMBOLIZER_PATH' is not given, try 'llvm-symbolizer'.");
      symbolizer_path = "llvm-symbolizer";
    }

    if (stat(symbolizer_path.data(), &st) || !S_ISREG(st.st_mode) ||
        !(st.st_mode & 0111) || (f_len = st.st_size) < 4) {
      PF_ACTF("Invalid symbolizer '%s', skip printing PCs.",
              symbolizer_path.data());
      symbolizer_path = "";
    }

    initialized = 1;
  }

  if (symbolizer_path == "") return target_str;

  std::vector<std::tuple<size_t, size_t, Loc>> edits;

  std::string pcid_prefix = "<PF_PCID:";
  std::string pcid_suffix = ">";
  size_t pos = target_str.find(pcid_prefix);
  while (pos != std::string::npos) {
    size_t pcid_pos = pos + pcid_prefix.size();
    size_t pcid_len = target_str.find(pcid_suffix, pcid_pos) - pcid_pos;
    std::string pcid_str = target_str.substr(pcid_pos, pcid_len);
    size_t edit_len = pcid_prefix.size() + pcid_str.size() + pcid_suffix.size();

    std::stringstream sstream(pcid_str);
    size_t pcid;
    sstream >> pcid;
    std::string pc = pcid_to_pc[pcid];
    auto loc = run_symbolizer(symbolizer_path, (char*)bin_path, pc);
    if (loc) edits.push_back({pos, edit_len, loc.value()});

    if (pos + edit_len >= target_str.size()) break;

    pos = target_str.find(pcid_prefix, pos + edit_len);
  }

  if (edits.empty()) return target_str;

  // Find and remove longest common path.
  size_t filepath_pos = 0;
  if (rm_common_path) {
    fs::path common = std::get<2>(edits[0]).common_path();
    for (size_t i = 1; i < edits.size(); i++)
      common = common_path(common, std::get<2>(edits[i]).common_path());
    if (common == fs::current_path().root_path()) {
      filepath_pos = 0;
    } else {
      filepath_pos = std::string(common).size() + 1;
    }
  }

  for (int i = edits.size() - 1; i >= 0; i--) {
    auto edit = edits[i];
    size_t edit_pos = std::get<0>(edit);
    size_t edit_len = std::get<1>(edit);
    Loc edit_loc = std::get<2>(edit);

    target_str.replace(edit_pos, edit_len,
                       edit_loc.to_string(filepath_pos, dot));
  }

  return target_str;
}

void substitute_pcs(u8* bin_path, const fs::path& dir,
                    std::map<size_t, std::string> pcid_to_pc,
                    bool rm_common_path, bool dot) {
  std::vector<fs::path> files = gpf::list_files_in_dir(dir);
  const std::string inputset_prefix = "inputset_";
  for (auto& file : files) {
    std::string filename = file.filename().string();
    if (filename.compare(0, inputset_prefix.size(), inputset_prefix) == 0)
      continue;

    std::string contents = gpf::read_file(file.string());
    std::string substituted =
        substitute_pcs(bin_path, contents, pcid_to_pc, rm_common_path, dot);
    gpf::write_to_file(file.string(), substituted);
  }
}

void print_nd_branches(u8* bin_path, std::map<size_t, std::string> pcid_to_pc,
                       std::vector<size_t> nd_pcids) {
  static std::string symbolizer_path;

  static u8 initialized = 0;
  if (!initialized) {
    struct stat st;
    u32 f_len = 0;

    if (getenv("ASAN_SYMBOLIZER_PATH")) {
      symbolizer_path = getenv("ASAN_SYMBOLIZER_PATH");
    } else {
      PF_ACTF("'ASAN_SYMBOLIZER_PATH' is not given, try 'llvm-symbolizer'.");
      symbolizer_path = "llvm-symbolizer";
    }

    if (stat(symbolizer_path.data(), &st) || !S_ISREG(st.st_mode) ||
        !(st.st_mode & 0111) || (f_len = st.st_size) < 4) {
      PF_ACTF("Invalid symbolizer '%s', skip printing PCs.",
              symbolizer_path.data());
      symbolizer_path = "";
    }

    initialized = 1;
  }

  assert(!symbolizer_path.empty());

  PF_ACTF("Printing ND branches...");

  std::cout << "\n# ND PCs: " << nd_pcids.size() << std::endl;

  for (auto& nd_pcid : nd_pcids) {
    std::string nd_pc = pcid_to_pc[nd_pcid];
    // std::cout << "ND PC: " << nd_pc << std::endl;
    auto loc = run_symbolizer(symbolizer_path, (char*)bin_path, nd_pc);
    if (!loc) {
      std::cout << "Failed to symbolize" << std::endl;
      continue;
    }
    std::cout << loc->to_string(0, false);
  }
}